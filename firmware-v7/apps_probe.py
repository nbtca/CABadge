"""USB-only app smoke test on the real LCD; does not change the PC's network."""
import json
import time
from pathlib import Path
from runtime_probe import Link


def main():
    out = Path(__file__).resolve().parent.parent / 'outputs/cabadge-v7-apps-20260923'
    rows = []
    link = Link('COM3')
    try:
        link.request(32, b'\1', 33)
        before = link.state()
        link.command(10, b'\0')
        for name, page, seconds in [('map', 10, 100), ('game', 11, 10), ('released', 0, 5)]:
            link.snap(bytes([1, page]))
            started = time.monotonic()
            while time.monotonic() - started < seconds:
                row = link.snap()
                rows.append({'case': name, 'snapshot': row})
                out.joinpath('apps-runtime.json').write_text(json.dumps(rows, indent=2), 'utf-8')
                print(name, row.get('apps'), 'internal=', row['heap'][0]['free'],
                      'psram=', row['heap'][1]['free'], flush=True)
                if name == 'map' and not row['apps']['loading']:
                    break
                time.sleep(2)
        after = link.state()
        checks = {
            'firmware': rows[-1]['snapshot']['firmware'] == '7.6.0-apps',
            'map_loaded': any(r['case'] == 'map' and not r['snapshot']['apps']['loading']
                              and r['snapshot']['apps']['error'] == 0
                              and r['snapshot']['apps']['tiles'] > 0 for r in rows),
            'game_running': any(r['case'] == 'game' and r['snapshot']['apps']['page'] == 3 for r in rows),
            'wallpapers_preserved': all(before['wallpaper'].get(k) == after['wallpaper'].get(k)
                                        for k in ('items', 'selected', 'crc')),
        }
        out.joinpath('apps-checks.json').write_text(json.dumps(checks, indent=2), 'utf-8')
        print(checks, flush=True)
        assert all(checks.values()), checks
    finally:
        try:
            link.snap(bytes([1, 0]))
        finally:
            link.c.close()


if __name__ == '__main__':
    main()
