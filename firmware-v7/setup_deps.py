"""Fetch LVGL and OFL font sources for the physical firmware build."""
from pathlib import Path
import hashlib, io, urllib.request, zipfile

ROOT = Path(__file__).resolve().parent
VENDOR = ROOT / 'vendor'
VENDOR.mkdir(exist_ok=True)
FONTS = 'https://raw.githubusercontent.com/google/fonts/2894aab31764f10f29c421bdfd2340d3b382d384/ofl/notosanssc/'

def fetch(url, sha256):
    print('Downloading', url, flush=True)
    data = urllib.request.urlopen(url, timeout=120).read()
    if hashlib.sha256(data).hexdigest() != sha256:
        raise SystemExit(f'SHA-256 mismatch for {url}')
    return data

if not (VENDOR / 'lvgl-9.4.0').exists():
    zipfile.ZipFile(io.BytesIO(fetch('https://codeload.github.com/lvgl/lvgl/zip/refs/tags/v9.4.0',
        '1d2f63e1e66c0d24e82b14e42656b88f0ae12ea5efdb5962341f3dcade753ef5'))).extractall(VENDOR)
fonts = VENDOR / 'fonts'
fonts.mkdir(exist_ok=True)
if not (fonts / 'NotoSansSC.ttf').exists():
    (fonts / 'NotoSansSC.ttf').write_bytes(fetch(FONTS + 'NotoSansSC%5Bwght%5D.ttf', 'a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da'))
    (fonts / 'OFL.txt').write_bytes(fetch(FONTS + 'OFL.txt', '1c05c68c34f9708415aada51f17e1b0092d2cea709bf4a94cd38114f9e73d7d9'))
print('Dependencies ready.')
