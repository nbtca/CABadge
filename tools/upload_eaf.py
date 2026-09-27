"""Import an EAF as an ordinary deletable wallpaper; apply it after upload."""
import argparse
from pathlib import Path
import zlib
from install_wallpapers import state, select, upload
from map_perf_monitor import connect
from eaf_timing import read

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path)
    parser.add_argument('--port', default='COM3')
    args = parser.parse_args()
    raw = args.file.read_bytes()
    if not 24 <= len(raw) <= 3145728 or raw[:4] != b'\x89EAF':
        raise ValueError('Expected an EAF file <=3 MiB; see docs/EAF_TIMING.md')
    sidecar = args.file.with_suffix(args.file.suffix + '.timing')
    timing = sidecar.read_bytes() if sidecar.exists() else b''
    info = read(timing, raw) if timing else {'mode': 'LEGACY', 'crc': 0}
    link = connect(args.port)
    try:
        s = state(link)
        if 'types' not in s:
            raise RuntimeError('Flash the EAF firmware first')
        if timing and 'timing_crcs' not in s:
            raise RuntimeError('Timing metadata requires 7.9.15-eaf-timing or newer')
        existing = next((int(i) for i, crc in s['crcs'].items()
                         if crc == zlib.crc32(raw) and s['types'][i] == 'EAF'
                         and s.get('timing_crcs', {}).get(i, 0) == info['crc']), None)
        if existing is None:
            existing = upload(link, raw, eaf=True, timing=timing)
        else:
            select(link, existing)
        print(f"EAF wallpaper id={existing}, source={len(raw)} bytes in Flash, timing={info['mode']}")
    finally:
        link.port.close()
