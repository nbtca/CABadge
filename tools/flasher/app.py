"""Firmware package checks and esptool flashing used by firmware-v7/workbench.py."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import struct

REGIONS = [('bootloader.bin', 0x0), ('partitions.bin', 0x8000), ('firmware.bin', 0x10000)]
BOARD_HASH = 'c7c59dff5e6f2065a2157a81414d9d21c5cb1196a7b277656e15007753801a8d'
ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')


def firmware_files(folder):
    """Fail before touching a port if a release is incomplete or altered."""
    folder = Path(folder)
    manifest = folder / 'verification.json'
    if not manifest.is_file():
        raise ValueError('固件目录缺少 verification.json；请选择本项目导出的完整固件目录。')
    meta = json.loads(manifest.read_text(encoding='utf-8-sig'))
    if not isinstance(meta, dict):
        raise ValueError('verification.json 须为本项目的固件发布清单。')
    board = meta.get('pcb_sha256', meta.get('board'))
    if board not in (BOARD_HASH, 'c7c59dff'):
        raise ValueError('固件清单与已打板 c7c59dff 不匹配。')
    hashes = meta.get('hashes', meta.get('artifacts', {}))
    if not isinstance(hashes, dict):
        raise ValueError('固件清单中的哈希表无效。')
    paths = []
    for name, offset in REGIONS:
        path = folder / name
        if not path.is_file():
            raise ValueError(f'缺少文件：{name}')
        if path.stat().st_size > 0x1000000:
            raise ValueError(f'{name} 超出本板 16 MB 容量。')
        data = path.read_bytes()
        expected = hashes.get(name)
        if not isinstance(expected, str) or hashlib.sha256(data).hexdigest() != expected.lower():
            raise ValueError(f'{name} 校验不一致，请重新导出完整固件。')
        if name != 'partitions.bin' and (not data or data[0] != 0xE9):
            raise ValueError(f'{name} 不是 ESP 镜像。')
        paths.append((offset, path))
    if paths[0][1].stat().st_size > 0x8000 or paths[1][1].stat().st_size > 0x1000:
        raise ValueError('引导程序或分区表超出本板的写入区域。')
    table = paths[1][1].read_bytes()
    app_size = None
    for pos in range(0, len(table) - 31, 32):
        magic, kind, subtype, offset, size = struct.unpack_from('<HBBII', table, pos)
        if magic == 0x50AA and kind == 0 and subtype == 0 and offset == 0x10000:
            app_size = size
    if app_size is None or 0x10000 + app_size > 0xfff000 or paths[2][1].stat().st_size > app_size:
        raise ValueError('应用大小或地址不符合本板 factory 分区。')
    return paths


def flash_args(port, paths):
    return ['--chip', 'esp32s3', '--port', port, '--baud', '115200', 'write-flash'] + [
        value for offset, path in paths for value in (hex(offset), str(path))]


class ToolOutput(io.TextIOBase):
    def __init__(self, events):
        self.events = events

    def write(self, text):
        if text:
            self.events.put(('log', text))
        return len(text)

    def flush(self):
        pass

    def isatty(self):
        return False


def flash_worker(events, folder, port):
    try:
        paths = firmware_files(folder)
        import esptool
        output = ToolOutput(events)
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            esptool.main(flash_args(port, paths))
        events.put(('flashed', True))
    except BaseException as exc:
        events.put(('error', f'烧录失败：{exc}\n关闭其他串口窗口；若无法进入下载模式，按住 BOOT，按下并松开 RESET，再松开 BOOT，然后重试。'))
        events.put(('flashed', False))
