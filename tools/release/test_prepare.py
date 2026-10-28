"""Release contract checks using synthetic packages; never access a device."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import unittest
import zipfile

import prepare


def digest(data):
    return hashlib.sha256(data).hexdigest()


class ReleaseChecks(unittest.TestCase):
    def setUp(self):
        temp_root = Path(os.environ.get('CABADGE_BUILD_ROOT', 'F:/CABadgeBuild')) / 'temp/release-tests'
        temp_root.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=temp_root)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.release = self.root / 'V11'
        (self.release / 'verification').mkdir(parents=True)
        (self.release / 'print').mkdir()
        (self.release / 'cad').mkdir()
        self.stl, self.step, self.native = b'R2 STL', b'R2 STEP', b'R2 native'
        (self.release / 'print/V11_Button_R2.stl').write_bytes(self.stl)
        self.zip('SolidWorks', {
            'CABadge_V11_V2_1.SLDASM': b'assembly',
            'V11_Button_R2.SLDPRT': self.native,
            **{f'V11_{name}.SLDPRT': b'unchanged' for name in ['Cup', 'Keeper', 'Screen', 'Battery', 'Insulator', 'BackLiner', 'PCB_reference']},
        })
        self.zip('STEP', {'CABadge_V11_V2_1_Assembly.step': b'assembly', 'V11_Button_R2.step': self.step, 'V11_Cup.step': b'cup', 'V11_Keeper.step': b'keeper'})
        proof = {'status': 'BUTTON_CAD_PASS_PRINTED_FIT_PENDING', 'physical_acceptance': False, 'input_sha256': {'V11_Button_R2.stl': digest(self.stl), 'V11_Button_R2.step': digest(self.step), 'V11_Button_R2.SLDPRT': digest(self.native)}}
        (self.release / 'verification/button-r2.json').write_text(json.dumps(proof))
        self.rehash()

    def zip(self, kind, members):
        with zipfile.ZipFile(self.release / f'cad/CABadge_V11_V2_1-{kind}.zip', 'w') as z:
            for name, data in members.items():
                z.writestr(name, data)

    def rehash(self):
        files = [{'path': p.relative_to(self.release).as_posix(), 'bytes': p.stat().st_size, 'sha256': digest(p.read_bytes())} for p in sorted(self.release.rglob('*')) if p.is_file() and p.relative_to(self.release).as_posix() not in ['MANIFEST.json', 'SHA256SUMS.txt']]
        manifest = self.release / 'MANIFEST.json'
        manifest.write_text(json.dumps({'button_revision': 'R2', 'files': files}))
        (self.release / 'SHA256SUMS.txt').write_text(''.join(f"{p['sha256']}  {p['path']}\n" for p in files) + digest(manifest.read_bytes()) + '  MANIFEST.json\n')

    def test_valid_r2_retains_pending_physical_acceptance(self):
        self.assertFalse(prepare.verify_enclosure(self.release)['physical_acceptance'])

    def test_rehashed_wrong_native_revision_is_rejected(self):
        with zipfile.ZipFile(self.release / 'cad/CABadge_V11_V2_1-SolidWorks.zip') as z:
            members = {n: z.read(n) for n in z.namelist()}
        members['V11_Button_R2.SLDPRT'] = b'old button with new filename'
        self.zip('SolidWorks', members)
        self.rehash()
        with self.assertRaisesRegex(ValueError, 'R2 native'):
            prepare.verify_enclosure(self.release)

    def test_rehashed_wrong_stl_is_rejected(self):
        (self.release / 'print/V11_Button_R2.stl').write_bytes(b'old mesh')
        self.rehash()
        with self.assertRaisesRegex(ValueError, 'R2 STL'):
            prepare.verify_enclosure(self.release)

    def test_rehashed_corrupt_archive_is_rejected(self):
        (self.release / 'cad/CABadge_V11_V2_1-STEP.zip').write_bytes(b'broken zip')
        self.rehash()
        with self.assertRaises(ValueError):
            prepare.verify_enclosure(self.release)

    def test_rehashed_missing_companion_part_is_rejected(self):
        with zipfile.ZipFile(self.release / 'cad/CABadge_V11_V2_1-SolidWorks.zip') as z:
            members = {n: z.read(n) for n in z.namelist()}
        members['unrelated.txt'] = members.pop('V11_Cup.SLDPRT')
        self.zip('SolidWorks', members)
        self.rehash()
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            prepare.verify_enclosure(self.release)

    def test_missing_payload_is_rejected(self):
        (self.release / 'print/V11_Button_R2.stl').unlink()
        with self.assertRaisesRegex(ValueError, 'missing|Missing'):
            prepare.verify_enclosure(self.release)

    def test_manifest_cannot_escape_release_directory(self):
        manifest = self.release / 'MANIFEST.json'
        data = json.loads(manifest.read_text())
        data['files'][0]['path'] = '../outside'
        manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, 'outside|escape'):
            prepare.verify_enclosure(self.release)

    def test_export_is_deterministic_and_does_not_modify_sources(self):
        before = {p: p.read_bytes() for p in self.release.rglob('*') if p.is_file()}
        first, second = self.root / 'first.zip', self.root / 'second.zip'
        prepare.export_package(self.release, first, 'enclosure')
        prepare.export_package(self.release, second, 'enclosure')
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertEqual(before, {p: p.read_bytes() for p in before})
        self.assertEqual(first.with_suffix('.zip.sha256').read_text().split()[0], digest(first.read_bytes()))
        with zipfile.ZipFile(first) as z:
            self.assertIsNone(z.testzip())

    def test_output_cannot_be_inside_source(self):
        with self.assertRaisesRegex(ValueError, 'inside'):
            prepare.export_package(self.release, self.release / 'out.zip', 'enclosure')

    def test_refresh_covers_new_document_and_preserves_revision(self):
        (self.release / 'README.md').write_text('Updated download links')
        prepare.refresh_manifest(self.release)
        self.assertFalse(prepare.verify_enclosure(self.release)['physical_acceptance'])
        self.assertEqual(json.loads((self.release / 'MANIFEST.json').read_text())['button_revision'], 'R2')

    def test_refresh_rejects_wrong_revision_before_changing_checksums(self):
        manifest = self.release / 'MANIFEST.json'
        before = manifest.read_bytes()
        (self.release / 'print/V11_Button_R2.stl').write_bytes(b'wrong version')
        with self.assertRaisesRegex(ValueError, 'R2 STL'):
            prepare.refresh_manifest(self.release)
        self.assertEqual(manifest.read_bytes(), before)

    def test_nested_metadata_names_are_not_exempt_from_coverage(self):
        (self.release / 'extras').mkdir()
        (self.release / 'extras/MANIFEST.json').write_text('unlisted data')
        with self.assertRaisesRegex(ValueError, 'complete release payload'):
            prepare.verify_enclosure(self.release)

    def test_refresh_includes_nested_metadata_names(self):
        (self.release / 'extras').mkdir()
        (self.release / 'extras/SHA256SUMS.txt').write_text('nested data')
        prepare.refresh_manifest(self.release)
        names = {item['path'] for item in json.loads((self.release / 'MANIFEST.json').read_text())['files']}
        self.assertIn('extras/SHA256SUMS.txt', names)

    def firmware(self):
        source = self.root / 'source'
        (source / 'firmware-v7').mkdir(parents=True)
        (source / 'firmware-v7/version.json').write_text('{"version":"7.9.23-timetiles-fix"}')
        subprocess.run(['git', 'init', '-q', str(source)], check=True)
        subprocess.run(['git', '-C', str(source), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(source), '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD']).decode().strip()
        folder = self.root / 'firmware'
        folder.mkdir()
        hashes = {}
        for name in ['bootloader.bin', 'partitions.bin', 'firmware.bin', 'firmware.elf']:
            (folder / name).write_bytes(name.encode())
            hashes[name] = digest(name.encode())
        meta = {'source_commit': commit, 'source_dirty': False, 'firmware_version': '7.9.23-timetiles-fix', 'build_variant': '', 'hashes': hashes, 'hardware_test': 'PENDING', 'source_inputs': {'firmware-v7/version.json': digest((source / 'firmware-v7/version.json').read_bytes())}}
        (folder / 'verification.json').write_text(json.dumps(meta))
        return folder, source, meta

    def test_firmware_commit_version_and_hashes(self):
        folder, source, meta = self.firmware()
        self.assertEqual(prepare.verify_firmware(folder, source)['source_commit'], meta['source_commit'])

    def test_dirty_build_cannot_be_published(self):
        folder, source, meta = self.firmware()
        meta['source_dirty'] = True
        (folder / 'verification.json').write_text(json.dumps(meta))
        with self.assertRaisesRegex(ValueError, 'dirty'):
            prepare.verify_firmware(folder, source)

    def test_wrong_source_commit_is_rejected(self):
        folder, source, meta = self.firmware()
        meta['source_commit'] = '0' * 40
        (folder / 'verification.json').write_text(json.dumps(meta))
        with self.assertRaisesRegex(ValueError, 'commit'):
            prepare.verify_firmware(folder, source)

    def test_version_drift_is_rejected(self):
        folder, source, meta = self.firmware()
        meta['firmware_version'] = '7.9.16'
        (folder / 'verification.json').write_text(json.dumps(meta))
        with self.assertRaisesRegex(ValueError, 'version'):
            prepare.verify_firmware(folder, source)

    def test_changed_binary_is_rejected(self):
        folder, source, meta = self.firmware()
        (folder / 'firmware.bin').write_bytes(b'different build')
        with self.assertRaisesRegex(ValueError, 'hash'):
            prepare.verify_firmware(folder, source)

    def test_build_inputs_must_match_source_commit(self):
        folder, source, meta = self.firmware()
        meta['source_inputs']['firmware-v7/version.json'] = '0' * 64
        (folder / 'verification.json').write_text(json.dumps(meta))
        with self.assertRaisesRegex(ValueError, 'input'):
            prepare.verify_firmware(folder, source)


if __name__ == '__main__':
    unittest.main()
