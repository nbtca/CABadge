"""Check actual fixed button rectangles; scroll rows and keyboard are separate cases."""
from pathlib import Path
import math
import re

ROOT = Path(__file__).resolve().parent
count = 0
for name in ('badge_ui.c', 'wifi_panel.c'):
    source = (ROOT / 'ui' / name).read_text(encoding='utf-8')
    pattern = r'button\((p|wall_panel|panel|ble_panel),"([^"]*)",(\d+),(\d+),(\d+),(\d+),'
    for parent, title, x, y, w, h in re.findall(pattern, source):
        x, y, w, h = map(int, (x, y, w, h))
        assert min(w, h) >= 44, (name, title, 'touch area', w, h)
        radius = max(math.hypot(a - 180, b - 180) for a in (x, x + w - 1) for b in (y, y + h - 1))
        assert radius <= 176, (name, title, 'outside R176', radius)
        count += 1
assert count >= 20
print(f'{count} fixed button rectangles inside R176, minimum 44px: PASS')

# Make missing static Chinese glyphs a build failure. Dynamic SSIDs remain a
# separate limitation of the subset font; arbitrary Chinese is not promised.
generated = (ROOT / 'ui' / 'assets.c').read_text(encoding='utf-8')
points = re.search(r'f18_unicode\[\]=\{([^}]+)', generated).group(1)
available = {int(n) + 32 for n in points.split(',')}
for name in ('ui/badge_ui.c', 'ui/wifi_panel.c', 'bridge.c', 'simulator.c'):
    source = (ROOT / name).read_text(encoding='utf-8')
    for text in re.findall(r'"([^"\n]*)"', source):
        missing = {c for c in text if 127 < ord(c) < 65535 and ord(c) not in available}
        assert not missing, (name, missing)
print('Static UI font coverage: PASS')
