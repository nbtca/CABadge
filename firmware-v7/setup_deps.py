"""Fetch LVGL and OFL font sources for the physical firmware build."""
from pathlib import Path
import io, urllib.request, zipfile

ROOT = Path(__file__).resolve().parent
VENDOR = ROOT / 'vendor'
VENDOR.mkdir(exist_ok=True)

def fetch(url):
    print('Downloading', url, flush=True)
    return urllib.request.urlopen(url, timeout=120).read()

if not (VENDOR / 'lvgl-9.4.0').exists():
    zipfile.ZipFile(io.BytesIO(fetch('https://codeload.github.com/lvgl/lvgl/zip/refs/tags/v9.4.0'))).extractall(VENDOR)
fonts = VENDOR / 'fonts'
fonts.mkdir(exist_ok=True)
if not (fonts / 'NotoSansSC.ttf').exists():
    (fonts / 'NotoSansSC.ttf').write_bytes(fetch('https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf'))
    (fonts / 'OFL.txt').write_bytes(fetch('https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/OFL.txt'))
print('Dependencies ready.')
