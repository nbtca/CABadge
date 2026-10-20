"""Existing USB management protocol: real-board HR smoke test, not BLE simulation."""
import argparse
import json
from pathlib import Path
import struct
import time
import serial
from framing import Decoder, packet


class Board:
    def __init__(self, port):
        self.wire = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=1)
        self.wire.dtr = self.wire.rts = False
        self.wire.port = port
        self.wire.open()
        self.decoder, self.request_id = Decoder(), int(time.time()) & 0xffffffff
        self.exchange(32, b'\x01', 33)

    def exchange(self, kind, data=b'', expected=None):
        self.wire.write(packet(kind, data))
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            for received, payload in self.decoder.feed(self.wire.read(8192)):
                if received == expected:
                    return payload
        raise TimeoutError(f'USB response {expected} missing')

    def state(self):
        return json.loads(self.exchange(34, expected=34))

    def runtime(self):
        return json.loads(self.exchange(41, expected=42))

    def command(self, operation, arguments=b''):
        self.request_id = (self.request_id + 1) & 0xffffffff or 1
        reply = self.exchange(35, struct.pack('<I', self.request_id) + bytes((1, operation)) + arguments, 36)
        if struct.unpack_from('<I', reply)[0] != self.request_id or reply[4] != 0:
            raise RuntimeError(f'command {operation}: {reply.hex()}')

    def scan(self):
        self.command(6)
        deadline = time.monotonic() + 12
        started = time.monotonic()
        while time.monotonic() < deadline:
            result = self.state()
            if time.monotonic()-started > 1 and not result['ble']['scanning']:
                return result
            time.sleep(.25)
        raise TimeoutError('scan completion missing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--action', choices=('state', 'scan', 'connect', 'disconnect', 'watch'), default='state')
    parser.add_argument('--name', default='')
    parser.add_argument('--seconds', type=int, default=12)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    board = Board(args.port)
    evidence = {'action': args.action, 'samples': []}
    try:
        evidence['before'] = board.state()
        evidence['runtime_before'] = board.runtime()
        if args.action in ('scan', 'connect'):
            evidence['scan'] = board.scan()
        if args.action == 'connect':
            # Explicit name selection, never silently connect to the first nearby band.
            candidates = [d for d in evidence['scan']['ble']['devices']
                          if d.get('heart_rate') and d['connectable'] and args.name and args.name.lower() in d['name'].lower()]
            if len(candidates) != 1:
                raise RuntimeError(f'exactly one HR device must match --name; found {len(candidates)}')
            device = candidates[0]
            evidence['selected'] = device
            board.command(7, bytes((device['type'],)) + bytes.fromhex(device['address']))
        if args.action == 'disconnect':
            board.command(8, b'\x01')
        if args.action in ('connect', 'watch'):
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                state = board.state()
                evidence['samples'].append(state)
                print(json.dumps({'firmware': state['firmware'], 'heart_rate': state.get('heart_rate')}, ensure_ascii=False), flush=True)
                time.sleep(.5)
        evidence['after'] = board.state()
        evidence['runtime_after'] = board.runtime()
        if args.action == 'connect':
            rates = [s.get('heart_rate', {}) for s in evidence['samples']]
            assert any(h.get('connected') and not h.get('stale') and h.get('bpm') is not None and h.get('notifications', 0) for h in rates), 'no real HR Notify measurement received'
            assert len({h['last_update_ms'] for h in rates if not h.get('stale', True)}) >= 2, 'no real-time timestamp progression'
            evidence['notify_smoke_pass'] = True
    except Exception as exc:
        evidence['error'] = str(exc)
        raise
    finally:
        board.wire.close()
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
        if args.action not in ('watch', 'connect'):
            print(json.dumps(evidence, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
