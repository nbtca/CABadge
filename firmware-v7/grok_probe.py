"""USB-only native Grok lifecycle check; physical touch/visual acceptance separate."""
import json
import time
from pathlib import Path
from runtime_probe import Link

out = Path(__file__).resolve().parent.parent / 'outputs/cabadge-v7-grok-full-20260923'
link = Link('COM3')
rows = []
try:
    link.request(32, b'\1', 33)
    before = link.state()
    assert link.snap()['firmware'] == '7.6.2-grok'
    link.command(10, b'\0')
    for name, page, duration in [('idle', 9, 3), ('grok', 12, 12), ('released', 9, 3),
                                 ('grok_again', 12, 6), ('sleep', None, 3), ('awake', None, 3), ('released_again', 9, 3)]:
        if page is not None:
            link.snap(bytes([1, page]))
        if name == 'sleep': link.command(10, b'\1')
        if name == 'awake': link.command(10, b'\0')
        time.sleep(.5)
        start = time.monotonic()
        while time.monotonic() - start < duration:
            row = link.snap()
            rows.append({'case': name, 'snapshot': row})
            print(name, row['apps']['page'], row['heap'][1]['free'], flush=True)
            time.sleep(.5)
    after = link.state()
    grouped = {name: [r['snapshot'] for r in rows if r['case'] == name] for name in {r['case'] for r in rows}}
    a, b = grouped['grok'][0], grouped['grok'][-1]
    elapsed = (b['us']-a['us'])/1e6
    result = {'rows': rows, 'checks': {
        'grok_open': all(r['apps']['page'] == 5 for r in grouped['grok']),
        'sleep_no_render': grouped['sleep'][-1]['rendered'] == grouped['sleep'][0]['rendered'],
        'resume_render': grouped['awake'][-1]['rendered'] > grouped['awake'][0]['rendered'],
        'psram_returned': abs(grouped['released'][-1]['heap'][1]['free']-grouped['released_again'][-1]['heap'][1]['free']) < 4096,
        'wallpapers_preserved': all(before['wallpaper'].get(k) == after['wallpaper'].get(k) for k in ('items','selected','crc')),
    }, 'render_fps': (b['rendered']-a['rendered'])/elapsed,
       'lcd_submission_fps': (b['submitted']-a['submitted'])/elapsed,
       'limits': 'USB counters, not LCD scan Hz. No physical finger or visual acceptance.'}
    out.joinpath('hardware-verification.json').write_text(json.dumps(result, indent=2), 'utf-8')
    print({k:v for k,v in result.items() if k!='rows'}, flush=True)
    assert all(result['checks'].values())
    link.snap(bytes([1,12]))
finally:
    link.c.close()
