"""Read BlueMap diagnostics through CABadge's framed USB query protocol."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import struct
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'firmware-v7' / 'usb_screen'))
from framing import Decoder, packet

WORLDS = ('world', 'world_the_nether', 'world_the_end')


def ms(value):
    return f'{value / 1000:.2f}ms'


def interval(record, end, start):
    a, b = record.get(start, 0), record.get(end, 0)
    return ms(b - a) if a and b and b >= a else 'N/A'


def describe(r):
    world = WORLDS[r['world']] if 0 <= r['world'] < len(WORLDS) else str(r['world'])
    prefix = f"seq={r['seq']} refresh={r['serial']} world={world}"
    if r['kind'] == 'tile':
        lines = [f"[MAP TILE] {prefix} x={r['x']} z={r['z']} lod={r['lod']}",
                 f"cache={'HIT' if r['hit'] else 'MISS'} http={r['http_status'] or 'N/A'} bytes={r['bytes']} result={r['result']} "
                 f"error={r.get('error_category', 'NONE')} detail={r.get('error_detail', 'NONE')} errno={r.get('http_errno', 0)}",
                 f"ttfb~={interval(r, 'first_byte_us', 'request_start_us')} "
                 f"download_wall={interval(r, 'download_complete_us', 'first_byte_us')} "
                 f"decode/downsample={ms(r['decode_downsample_us'])} "
                 f"http_api={ms(r['network_us'])} total={interval(r, 'end_us', 'start_us')}"]
        # Exact monotonic timestamps allow later correlation without per-chunk logging.
        milestones = ('request_start', 'first_byte', 'download_complete', 'png_complete', 'rgb565_complete')
        lines.append(f"lane={r.get('lane', 'N/A')} connects={r.get('connects', 'N/A')} reused={r.get('reused', 'N/A')} persistent={r.get('persistent', 'N/A')}")
        lines.append('boot_us: ' + ' '.join(f"{key}={r.get(key + '_us') or 'N/A'}" for key in milestones))
    else:
        lines = [f"[MAP REFRESH] {prefix} center=({r['center_x']},{r['center_z']}) zoom={r['zoom']} result={r['result']}",
                 f"tiles={r['tiles_needed']} hit={r['cache_hits']} miss={r['cache_misses']} downloaded={r['tiles_downloaded']}",
                 f"network_phase_wall={ms(r.get('network_wall_us', r['network_us']))} http_api_sum={ms(r['network_us'])} decode/downsample={ms(r['decode_downsample_us'])} "
                 f"compose={ms(r['compose_us'])} players_wall={ms(r['players_us'])}",
                 f"start_wait={interval(r, 'request_start_us', 'start_us')} "
                 f"worker_wall={interval(r, 'worker_done_us', 'request_start_us')} "
                 f"gui_queue={interval(r, 'gui_start_us', 'worker_done_us')} gui_submit={ms(r['gui_submit_us'])} "
                 f"refresh_wall/total={interval(r, 'end_us', 'start_us')}"]
        if 'requested_lanes' in r:
            lines.append('lanes: ' + ' '.join(f"{key}={r[key]}" for key in
                ('requested_lanes', 'actual_lanes', 'fallback_reason', 'internal_free', 'dma_free', 'psram_free')) +
                f" expired_tiles={r.get('expired_tiles', 0)}")
        lines.append(f"max_scheduled={r.get('max_inflight', 'N/A')} connects={r.get('connects', 'N/A')} reused_requests={r.get('reuse_count', 'N/A')} tile_updates={r.get('updates', 'N/A')}")
    return '\n'.join(lines)


class Link:
    def __init__(self, port):
        self.port = port
        self.decoder = Decoder()

    def query(self, kind, payload, expected, timeout=3):
        self.port.write(packet(kind, payload))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for reply, data in self.decoder.feed(self.port.read(self.port.in_waiting or 1)):
                if reply == expected:
                    return data
        raise TimeoutError('CABadge did not reply; close the workbench/other USB readers and retry.')


def connect(port_name):
    import serial
    from serial.tools import list_ports
    candidates = [port_name] if port_name else [p.device for p in list_ports.comports()
        if p.vid == 0x303A or 'CABadge' in p.description]
    failures = []
    for name in candidates:
        port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2)
        port.dtr = False
        port.rts = False
        port.port = name
        try:
            port.open()
            link = Link(port)
            ready = link.query(32, b'\x01', 33)
            if len(ready) != 12 or ready[0] != 1 or struct.unpack_from('<I', ready, 8)[0] != 0xc7c59dff:
                raise ValueError('Not a compatible CABadge')
            if not struct.unpack_from('<H', ready, 6)[0] & 32:
                raise ValueError('This firmware has no BlueMap diagnostic endpoint; flash the mapdiag build first.')
            return link
        except (OSError, ValueError, TimeoutError) as exc:
            port.close()
            failures.append(f'{name}: {exc}')
    raise RuntimeError('\n'.join(failures) or 'No CABadge USB port found. Connect a data cable, or pass --port COM3.')


def self_test():
    wire = packet(42, b'{"map_perf":1}')
    decoder = Decoder()
    assert decoder.feed(wire[:9]) == []
    assert decoder.feed(wire[9:]) == [(42, b'{"map_perf":1}')]
    r = dict(seq=1, serial=2, world=0, kind='tile', x=12, z=-8, lod=1,
             hit=False, http_status=200, bytes=184523, result=1, start_us=1000,
             request_start_us=2000, first_byte_us=414000, download_complete_us=540000,
             png_complete_us=577000, rgb565_complete_us=577000, end_us=584000,
             decode_downsample_us=37000, network_us=538000)
    text = describe(r)
    assert 'ttfb~=412.00ms' in text and 'download_wall=126.00ms' in text and 'total=583.00ms' in text
    r.update(hit=True, http_status=0, first_byte_us=0, request_start_us=0, download_complete_us=0)
    assert 'ttfb~=N/A' in describe(r) and 'cache=HIT' in describe(r)
    r.update(kind='refresh', center_x=0, center_z=0, zoom=1, tiles_needed=6,
             cache_hits=2, cache_misses=4, tiles_downloaded=4, compose_us=21000,
             players_us=12000, worker_done_us=570000, gui_start_us=580000, gui_submit_us=4000)
    assert 'tiles=6 hit=2 miss=4 downloaded=4' in describe(r) and 'gui_queue=10.00ms' in describe(r)
    print('Protocol framing, timing math, HIT/missing milestones and refresh formatting PASS')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Optional explicit port, e.g. COM3')
    parser.add_argument('--log-dir', type=Path, default=Path('F:/CABadgeBuild/logs'))
    parser.add_argument('--duration', type=float, default=0, help='Stop after N seconds; default runs until Ctrl+C')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    link = connect(args.port)
    try:
        args.log_dir.mkdir(parents=True, exist_ok=True)
        stem = args.log_dir / ('bluemap-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
        with stem.with_suffix('.log').open('w', encoding='utf-8', buffering=1) as log, \
                stem.with_suffix('.jsonl').open('w', encoding='utf-8', buffering=1) as raw:
            def emit(text):
                line = f'[{datetime.now().isoformat(timespec="seconds")}] {text}'
                print(line, flush=True)
                log.write(line + '\n')
            emit(f'Connected {link.port.port}. Log: {stem}.log (raw records: .jsonl)')
            emit('7.9.4+: up to 2 download lanes, memory-pressure fallback to 1; completed HTTP bodies retain persistent sockets. Per-record connects/reused proves actual reuse. '
                 'TTFB ~= first parsed header, not wire-level first byte; DNS/connect/TLS are not split. '
                 'PNG + RGB565 sampling are fused; both complete timestamps mark decoder-validated output. download_wall includes interleaved decode; do not add them. '
                 'http_api_sum adds calls across lanes (overlapping); network_phase_wall includes streaming decode/patch handoff. players_wall overlaps the network phase. '
                 'GUI submit = source/state update, not LCD DMA completion. Missing milestones = N/A. '
                 'Tile result: 1 OK, 0 missing, -2 cancelled, -3 memory-pressure retry, -4 allocation unavailable; '
                 '-10 HTTP, -11 READ, -12 TIMEOUT, -13 CONNECTION, -14 PNG_PARSE, -15 CRC, -16 DECODE, -17 SIZE, -18 OTHER; '
                 'refresh: 0 OK, positive app error, -2 discarded.')
            emit('Open BlueMap on the badge, wait for loading, drag once and zoom once. Ctrl+C stops. History may appear first.')
            cursor = 0
            deadline = time.monotonic() + args.duration if args.duration > 0 else float('inf')
            while time.monotonic() < deadline:
                reply = json.loads(link.query(41, b'\x0a' + struct.pack('<I', cursor), 42))
                if reply.get('map_perf') != 1 or not reply.get('available'):
                    raise RuntimeError('BlueMap diagnostic ring unavailable (firmware mismatch or allocation failure).')
                if reply['latest'] < cursor:
                    emit('Device history reset; restarting cursor.')
                    cursor = 0
                    continue
                if reply['lost']:
                    emit(f"History overflow: {reply['lost']} records no longer available.")
                record = reply['record']
                if record:
                    cursor = record['seq']
                    raw.write(json.dumps(record, ensure_ascii=False) + '\n')
                    emit(describe(record))
                else:
                    time.sleep(0.25)
    finally:
        link.port.close()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nStopped; logs saved.')
    except (OSError, ValueError, RuntimeError, TimeoutError, ImportError) as exc:
        sys.exit(str(exc))
