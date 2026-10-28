"""Shared locations for host checks; no device access or global tool discovery."""
import hashlib
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
REPOSITORY = ROOT.parent
BUILD_ROOT = Path((os.environ.get('CABADGE_BUILD_ROOT') or 'F:/CABadgeBuild')).resolve()
CHECKOUT_ID = hashlib.sha256(str(REPOSITORY).lower().encode()).hexdigest()[:12]
TEST_TEMP = BUILD_ROOT / 'temp' / ('host-' + CHECKOUT_ID)
TEST_TEMP.mkdir(parents=True, exist_ok=True)
ZIG = Path((os.environ.get('CABADGE_ZIG') or str(BUILD_ROOT/'venv/Lib/site-packages/ziglang/zig.exe')))
STAGE_ROOT = BUILD_ROOT / 'staging' / CHECKOUT_ID / 'usb-screen-default'
STAGES = sorted(path for path in STAGE_ROOT.glob('build-*') if path.is_dir())
STAGE = STAGES[-1] if STAGES else STAGE_ROOT
sys.path.insert(0, str(ROOT))
