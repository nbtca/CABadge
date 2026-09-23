"""USB framing used by the current workbench; no legacy display GUI dependency."""
import struct
import zlib

MAX_PAYLOAD = 8 + 360 * 16 * 2

def packet(kind, payload=b''):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('packet too large')
    fields = struct.pack('<BBH', 1, kind, len(payload))
    return b'JXUI' + fields + struct.pack('<I', zlib.crc32(payload, zlib.crc32(fields))) + payload


class Decoder:
    def __init__(self):
        self.pending = bytearray()

    def feed(self, data):
        self.pending.extend(data)
        output = []
        while True:
            index = self.pending.find(b'JXUI')
            if index < 0:
                self.pending[:] = self.pending[-3:]
                break
            if index:
                del self.pending[:index]
            if len(self.pending) < 12:
                break
            version, kind, length, checksum = struct.unpack_from('<BBHI', self.pending, 4)
            if version != 1 or length > MAX_PAYLOAD:
                del self.pending[0]
                continue
            if len(self.pending) < 12 + length:
                break
            payload = bytes(self.pending[12:12 + length])
            valid = zlib.crc32(payload, zlib.crc32(self.pending[4:8])) == checksum
            if not valid:
                del self.pending[0]
                continue
            del self.pending[:12 + length]
            output.append((kind, payload))
        return output


if __name__ == '__main__':
    payload = b'CABadge'
    wire = packet(41, payload)
    decoder = Decoder()
    assert decoder.feed(b'noise' + wire[:5]) == []
    assert decoder.feed(wire[5:]) == [(41, payload)]
    corrupt = bytearray(wire)
    corrupt[-1] ^= 1
    assert Decoder().feed(corrupt + wire) == [(41, payload)]
    assert Decoder().feed(packet(1) + packet(2)) == [(1, b''), (2, b'')]
    try:
        packet(1, b'x' * (MAX_PAYLOAD + 1))
    except ValueError:
        pass
    else:
        raise AssertionError('oversized payload accepted')
    print('USB framing and fragmented/CRC recovery PASS')
