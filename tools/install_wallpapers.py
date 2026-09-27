"""Explicitly import supplied artwork into the ordinary wallpaper library.

Not run at boot: deleted pictures stay deleted. Requires 7.9.11 or newer.
"""
import argparse
import json
from pathlib import Path
import struct
import time
import zlib

from map_perf_monitor import connect


def state(link):
    return json.loads(link.query(34, b'', 34))['wallpaper']


def settled(link, ready=lambda s: True):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        s = state(link)
        if s['phase'] not in (-1, 1, 2, 3):
            if s['error']:
                raise RuntimeError(f"Wallpaper operation failed: {s['error']}")
            if ready(s):
                return s
        time.sleep(.1)
    raise TimeoutError('Wallpaper apply did not complete')


def select(link, page):
    request = time.monotonic_ns() & 0xffffffff
    ack = link.query(35, struct.pack('<I', request) + bytes([1, 12, page]), 36)
    if len(ack) != 5 or ack[4]:
        raise RuntimeError(f'Select failed: {ack.hex()}')
    return settled(link, lambda s: s['selected'] == page)


def upload(link, raw, eaf=False, timing=b''):
    def command(kind, payload, timeout=3):
        deadline = time.monotonic() + timeout
        while True:
            ack = link.query(kind, payload, 14, max(.1, deadline-time.monotonic()))
            if len(ack) != 10 or ack[0] != kind:
                raise RuntimeError(f'Invalid upload reply: {ack.hex()}')
            if ack[1] == 1 and time.monotonic() < deadline:
                # BUSY explicitly means not accepted; never replay a timed-out write.
                time.sleep(.05)
                continue
            if ack[1]:
                raise RuntimeError(f'Upload command {kind} failed: {ack.hex()}')
            return struct.unpack_from('<II', ack, 2)
    session, _ = command(10, struct.pack('<II', len(raw) | (0x80000000 if eaf else 0), zlib.crc32(raw)) + timing, 30)
    try:
        # Board RX ring is 4096 bytes; leave room for protocol header and polling.
        for offset in range(0, len(raw), 1024):
            data = raw[offset:offset + 1024]
            _, received = command(11, struct.pack('<II', session, offset) + data)
            if eaf and (offset == 0 or received // 262144 != offset // 262144 or received == len(raw)):
                print(f'Flash upload: {received}/{len(raw)} B', flush=True)
            if received != offset + len(data):
                raise RuntimeError('Unexpected upload offset')
        command(12, struct.pack('<I', session), 15)
        return settled(link, lambda s: s['crc'] == zlib.crc32(raw) and s['size'] == len(raw))['selected']
    except Exception:
        try:
            command(13, struct.pack('<I', session))
        except Exception:
            pass
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--select', choices=['sunny', 'ribbons'], help='Select imported artwork instead of restoring current selection')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1] / 'firmware-v7/resources/wallpapers'
    pictures = [(key, (root / name).read_bytes()) for key, name in
                [('sunny', 'badge_wallpaper.rgb565'), ('ribbons', 'badge_ribbons.rgb565')]]
    if any(len(raw) != 360 * 360 * 2 for _, raw in pictures):
        raise ValueError('Expected 360 x 360 little-endian RGB565')
    link = connect(args.port)
    initial = state(link)
    if 'crcs' not in initial:
        link.port.close()
        raise RuntimeError('Flash 7.9.11-wallpaper or newer first')
    selected = initial['selected']
    imported = {}
    missing = sum(zlib.crc32(raw) not in initial['crcs'].values() for _, raw in pictures)
    try:
        if len(initial['items']) + missing > initial['capacity']:
            raise RuntimeError('Not enough free wallpaper slots; nothing was deleted')
        for key, raw in pictures:
            current = state(link)
            existing = next((int(i) for i, crc in current['crcs'].items() if crc == zlib.crc32(raw)), None)
            imported[key] = existing if existing is not None else upload(link, raw)
            print(f'{key}: id={imported[key]} {"already stored" if existing is not None else "imported"}')
        selected = imported[args.select] if args.select else selected
    finally:
        try:
            select(link, selected)
        finally:
            link.port.close()


if __name__ == '__main__':
    main()
