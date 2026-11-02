"""Generate immutable eye geometry from the MIT Grok Ball snapshot."""
import json
from pathlib import Path

p = Path(__file__).resolve().parent
d = json.loads((p / 'grok_source/data.json').read_text('utf-8'))
assert len(d['rings']) == 25 and len(d['emotions']) == 32
lines = ['/* Grok Ball geometry, Copyright (c) 2026 tycoding, MIT. See grok_source/LICENSE. */',
         'static const int16_t rings[25][2][48][2] = {']
for pair in d['rings']:
    assert len(pair) == 2 and all(len(r) == 48 for r in pair)
    lines.append('{' + ','.join('{' + ','.join('{%d,%d}' % tuple(round(v * 100) for v in xy) for xy in r) + '}' for r in pair) + '},')
lines.append('};\nstatic const emotion_t emotions[] = {')
for e in d['emotions']:
    pool = e.get('pool', [0])
    assert 1 <= len(pool) <= 8 and all(0 <= n < 25 for n in pool)
    color = e.get('body', {}).get('color', '#F3F0EA')
    blink = e.get('blinkMs') or [0, 0]
    lines.append('{%s,{%s},%d,%d,%d,%d,0x%s},' % (
        json.dumps(e['name'], ensure_ascii=False), ','.join(map(str, pool)), len(pool),
        e.get('transition', 500), blink[0], blink[1], color.lstrip('#')))
lines.append('};')
(p / 'grok_data.h').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('PASS: 25 paired contours, 32 bounded emotion records')
