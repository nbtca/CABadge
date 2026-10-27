"""Regression checks for repository integrity; synthetic files, no CAD or device access."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import unittest
import zipfile

CHECKER = Path(__file__).with_name('check_repository.py')


class RepositoryChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=CHECKER.parent.parent)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = dict(os.environ)
        for key in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE'):
            self.env.pop(key, None)
        self.git('init', '-q')
        for name, text in {
            'docs/check_repository.py': CHECKER.read_text(encoding='utf-8-sig'),
            'firmware-v7/usb_screen/device/src/CMakeLists.txt':
                'idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")',
            'firmware-v7/usb_screen/device/src/main.c': '/* test fixture */',
            'hardware/pcb/archive/V1_c7c59dff/JXBadge_V1.kicad_pcb': '(test_fixture)',
            'hardware/fabrication/JXBadge_EVT_20260909_c7c59dff/MANIFEST.json':
                '{"files": []}',
        }.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding='utf8')
        self.release = self.root / 'hardware/pcb/current'
        self.release.mkdir(parents=True)
        self.payload = self.release / 'board.epro2'
        with zipfile.ZipFile(self.payload, 'w') as archive:
            archive.writestr('test.epru', 'synthetic archive payload')
        self.write_checksums()
        self.git('add', '.')

    def git(self, *args):
        return subprocess.run(['git', *args], cwd=self.root, env=self.env,
                              capture_output=True, check=True)

    def write_checksums(self):
        raw = self.payload.read_bytes()
        digest = hashlib.sha256(raw).hexdigest()
        (self.release / 'MANIFEST.json').write_text(json.dumps({'files': [
            {'path': self.payload.name, 'bytes': len(raw), 'sha256': digest}
        ]}), encoding='utf8')
        (self.release / 'SHA256SUMS.txt').write_text(
            digest + '  ' + self.payload.name + '\n', encoding='utf8')

    def run_check(self, success, expected=''):
        result = subprocess.run(
            [os.sys.executable, '-B', str(self.root / 'docs/check_repository.py')],
            cwd=self.root, env=self.env, capture_output=True, text=True,
            encoding='utf8', errors='replace')
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode == 0, success, output)
        self.assertIn(expected, output)
        return output

    def test_archived_model_uses_its_project_directory(self):
        project = self.root / 'hardware/pcb/archive/V1_c7c59dff'
        (project / 'JXBadge_V1.kicad_pcb').write_text(
            '(model "${KIPRJMOD}/JXBadge.3dshapes/test.step")', encoding='utf8')
        model = project / 'JXBadge.3dshapes/test.step'
        model.parent.mkdir()
        model.write_text('synthetic model', encoding='utf8')
        self.git('add', '.')
        self.run_check(True, 'PASS:')
        model.unlink()
        self.run_check(False, 'Missing local 3D model: JXBadge.3dshapes/test.step')

    def test_valid_release(self):
        self.run_check(True, 'PASS:')

    def test_missing_manifest_target_is_reported_without_traceback(self):
        self.payload.unlink()
        output = self.run_check(False, 'board.epro2')
        self.assertNotIn('Traceback', output)

    def test_valid_zip_with_changed_bytes_fails_hash_check(self):
        with self.payload.open('ab') as stream:
            stream.write(b'changed bytes')
        with zipfile.ZipFile(self.payload) as archive:
            self.assertIsNone(archive.testzip())
        self.run_check(False, 'mismatch')

    def test_corrupt_archive_rejected_even_with_matching_hashes(self):
        self.payload.write_bytes(b'invalid native project archive')
        self.write_checksums()
        self.run_check(False, 'board.epro2')

    def test_manifest_size_is_checked(self):
        path = self.release / 'MANIFEST.json'
        doc = json.loads(path.read_text(encoding='utf8'))
        doc['files'][0]['bytes'] += 1
        path.write_text(json.dumps(doc), encoding='utf8')
        self.run_check(False, 'mismatch')

    def test_sha_list_is_checked_without_manifest(self):
        (self.release / 'MANIFEST.json').write_text('{"files": []}', encoding='utf8')
        (self.release / 'SHA256SUMS.txt').write_text(
            '0' * 64 + '  board.epro2\n', encoding='utf8')
        self.run_check(False, 'mismatch')

    def test_untracked_payload_does_not_mask_incomplete_release(self):
        self.git('rm', '--cached', 'hardware/pcb/current/board.epro2')
        self.run_check(False, 'not tracked')


if __name__ == '__main__':
    unittest.main()
