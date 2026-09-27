"""CABadge JXWT v1 resource sidecar; official .eaf bytes remain unchanged."""
import struct
import zlib

HEADER = struct.Struct('<4sHHIIIII')
DEFAULT_US = 33333
MIN_US, MAX_US = 10000, 60000000


def make(raw, durations_us):
    count = struct.unpack_from('<I', raw, 4)[0]
    if raw[:4] != b'\x89EAF' or not 1 <= count <= 360 or len(durations_us) != count:
        raise ValueError('Timing count must match EAF (1..360 frames)')
    delays = [max(MIN_US, min(MAX_US, round(d if d and d > 0 else DEFAULT_US))) for d in durations_us]
    constant = len(set(delays)) == 1
    table = b'' if constant else struct.pack('<' + 'H'*count, *(max(10, min(60000, round(d/1000))) for d in delays))
    prefix = struct.pack('<4sHHIIII', b'JXWT', 1, 1 if constant else 2, count,
                         delays[0] if constant else DEFAULT_US, len(table), zlib.crc32(raw))
    return prefix + struct.pack('<I', zlib.crc32(prefix + table)) + table


def read(data, raw):
    if len(data) < HEADER.size:
        raise ValueError('Truncated timing metadata')
    magic, version, mode, count, period, size, source_crc, crc = HEADER.unpack_from(data)
    if (magic != b'JXWT' or version != 1 or not 1 <= count <= 360
            or count != struct.unpack_from('<I', raw, 4)[0] or source_crc != zlib.crc32(raw)
            or not MIN_US <= period <= MAX_US or len(data) != HEADER.size + size
            or not ((mode == 1 and size == 0) or (mode == 2 and size == count*2))
            or zlib.crc32(data[:24] + data[HEADER.size:]) != crc):
        raise ValueError('Invalid timing metadata or wrong EAF sidecar')
    delays = [period]*count if mode == 1 else [ms*1000 for ms in struct.unpack_from('<'+'H'*count, data, HEADER.size)]
    if any(not MIN_US <= d <= MAX_US for d in delays):
        raise ValueError('Frame delay out of range')
    return {'mode': 'CONSTANT' if mode == 1 else 'PER_FRAME', 'frames': count,
            'delays_us': delays, 'crc': crc}
