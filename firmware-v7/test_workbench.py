"""Workbench package selection and labels; never opens a serial port or GUI."""
from pathlib import Path
import json
import tempfile
import unittest
from unittest.mock import patch
import workbench

ROOT = Path(__file__).resolve().parents[1]


class WorkbenchRelease(unittest.TestCase):
    def test_default_matches_formal_firmware(self):
        self.assertEqual(workbench.RELEASE.name, 'cabadge-v7.9.16-eaf-transition')

    def test_version_comes_from_selected_manifest(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as folder:
            path = Path(folder)
            (path / 'verification.json').write_text(
                json.dumps({'firmware_version': 'test-package-version'}), encoding='utf8')
            with patch.object(workbench, 'RELEASE', path):
                self.assertEqual(workbench.release_version(), 'test-package-version')

    def test_missing_package_has_no_stale_version_label(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as folder:
            with patch.object(workbench, 'RELEASE', Path(folder)):
                self.assertEqual(workbench.release_version(), '未准备固件包')

    def test_invalid_manifest_has_no_stale_version_label(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as folder:
            path = Path(folder)
            (path / 'verification.json').write_text('not json', encoding='utf8')
            with patch.object(workbench, 'RELEASE', path):
                self.assertEqual(workbench.release_version(), '未准备固件包')


if __name__ == '__main__':
    with patch('serial.Serial', side_effect=AssertionError('No device access in this test')):
        unittest.main()
