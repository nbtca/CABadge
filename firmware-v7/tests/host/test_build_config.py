import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / 'firmware-v7/usb_screen/build.ps1'

class BuildConfiguration(unittest.TestCase):
    def config(self, *args, env=None, ok=True):
        result = subprocess.run(['pwsh', '-NoProfile', '-File', str(BUILD), '-ConfigurationOnly', *args],
                                text=True, encoding="utf-8", capture_output=True, env=env)
        if not ok:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            return
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_default_identity_and_isolated_paths(self):
        cfg = self.config()
        self.assertEqual(cfg['version'], json.loads((ROOT/'firmware-v7/version.json').read_text())['version'])
        self.assertEqual(cfg['member_name'], 'Egger')
        self.assertEqual(cfg['build_variant'], '')
        self.assertNotEqual(Path(cfg['stage']), Path('F:/CABadgeBuild/staging/usb-screen-v7-idf61'))
        self.assertNotEqual(Path(cfg['export']), ROOT/'outputs/cabadge-v7.9.23-timetiles-fix')
        self.assertTrue(Path(cfg['export']).is_relative_to(ROOT/'outputs'))

    def test_custom_identity_has_separate_stage_and_output(self):
        default = self.config()
        custom = self.config('-BuildVariant', 'm1ngsama', '-MemberName', 'm1ngsama')
        self.assertEqual(custom['member_name'], 'm1ngsama')
        self.assertEqual(custom['version'], default['version'] + '-m1ngsama')
        self.assertEqual(custom['build_variant'], 'm1ngsama')
        self.assertNotEqual(default['stage'], custom['stage'])
        self.assertNotEqual(default['export'], custom['export'])
        alternate = self.config('-BuildVariant', 'm1ngsama', '-MemberName', 'Another')
        self.assertNotEqual(custom['stage'], alternate['stage'])

    def test_each_build_uses_fresh_stage_after_source_deletion(self):
        parent = Path(os.environ.get('CABADGE_BUILD_ROOT') or 'F:/CABadgeBuild') / 'temp'
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='stage-contract-', dir=parent) as temporary:
            env = dict(os.environ, CABADGE_BUILD_ROOT=temporary)
            old = Path(self.config(env=env)['stage'])
            old.mkdir(parents=True)
            stale = old / 'deleted-header.h'
            stale.write_text('old source must never participate in the next build')
            current = Path(self.config(env=env)['stage'])
            self.assertNotEqual(old, current, 'A new build must not reuse deleted staged inputs')
            self.assertFalse(current.exists())
            self.assertTrue(stale.is_file(), 'Keep previous build evidence intact')

    def test_custom_member_requires_variant(self):
        self.config('-MemberName', 'm1ngsama', ok=False)

    def test_rejects_unsafe_variant_and_member(self):
        for name in ['../escape', 'a/b', 'a b', 'CON', 'default', 'variant-too-long-for-idf']:
            with self.subTest(variant=name):
                self.config('-BuildVariant', name, ok=False)
        for name in ['', 'a"b', 'a\\b', '\n', '中', 'x'*33]:
            with self.subTest(member=name):
                self.config('-BuildVariant', 'custom', '-MemberName', name, ok=False)

    def test_build_root_override_and_diagnostics_separation(self):
        env = dict(os.environ, CABADGE_BUILD_ROOT='F:/CABadgeBuild/temp/config-only-' + uuid.uuid4().hex, CABADGE_DISPLAY_PERF='1')
        cfg = self.config(env=env)
        self.assertTrue(Path(cfg['stage']).is_relative_to(Path(env['CABADGE_BUILD_ROOT'])))
        self.assertFalse(Path(env['CABADGE_BUILD_ROOT']).exists(), 'ConfigurationOnly must not create a stage or output')
        self.assertIn('diagnostic', cfg['export'])
        self.assertNotEqual(cfg['stage'], self.config()['stage'])

    def test_no_duplicate_version_and_old_environment(self):
        cmake = (BUILD.parent/'device/CMakeLists.txt').read_text(encoding="utf-8")
        header = (ROOT/'firmware-v7/ui/badge_ui.h').read_text(encoding='utf-8')
        build = BUILD.read_text(encoding='utf-8-sig')
        self.assertNotIn('7.9.23', cmake + header + build)
        self.assertIn('cabadge_build.h', header)
        self.assertNotIn('bringup', build)
        self.assertIn('source_commit', build)
        self.assertIn('source_dirty', build)

if __name__ == '__main__':
    unittest.main()
