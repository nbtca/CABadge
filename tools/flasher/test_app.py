"""No board access: real release checks, protocol boundaries and mocked I/O."""
from pathlib import Path
import queue
import tempfile
import threading
from unittest.mock import patch

import app


def run():
    for _, directory, _ in app.PRESETS:
        paths = app.firmware_files(app.firmware_root() / directory)
        args = app.flash_args('COM3', paths)
        assert args[:7] == ['--chip', 'esp32s3', '--port', 'COM3', '--baud', '115200', 'write-flash']
        assert args[7::2] == ['0x0', '0x8000', '0x10000']
    assert app.credential('a'*32) == b'a'*32 + b'\n'
    assert app.credential('p'*63, True) == b'p'*63 + b'\n'
    for value, password in [('', False), ('a'*33, False), ('\x00a', False), ('x\ny', False), ('x'*7, True), ('p'*64, True), ('\u4e2d'*11, False)]:
        try:
            app.credential(value, password)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid credentials accepted')
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        source = app.firmware_root() / app.PRESETS[0][1]
        for name in ['verification.json'] + [r[0] for r in app.REGIONS]:
            (directory / name).write_bytes((source / name).read_bytes())
        (directory / 'firmware.bin').write_bytes(b'corrupted')
        try:
            app.firmware_files(directory)
        except ValueError:
            pass
        else:
            raise AssertionError('Corrupt firmware accepted')
        (directory / 'verification.json').write_text('[]')
        try:
            app.firmware_files(directory)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid manifest accepted')

    events = queue.Queue()
    with patch('esptool.main') as flash:
        app.flash_worker(events, app.firmware_root() / app.PRESETS[0][1], 'COM3')
        assert flash.call_count == 1 and events.get_nowait() == ('flashed', True)
    with patch('esptool.main', side_effect=RuntimeError('simulated disconnect')):
        app.flash_worker(events, app.firmware_root() / app.PRESETS[0][1], 'COM3')
        assert events.get_nowait()[0] == 'error'
        assert events.get_nowait() == ('flashed', False)

    stop = threading.Event()
    commands = queue.Queue()
    commands.put(b'x'*40 + b'\n')
    class Port:
        def __init__(self, **kwargs):
            self.parts = iter([b'ENTER SS', b'ID\r\n', b'ENTER PASSWORD\n', b'WIFI LINK=UP\n'])
            self.written = bytearray()
            self.closed = False
        def __enter__(self):
            assert self.dtr is False and self.rts is False
            return self
        def __exit__(self, *args):
            self.close()
        def write(self, data):
            assert len(data) <= 16
            self.written.extend(data)
            return len(data)
        def flush(self):
            pass
        def read(self, count):
            data = next(self.parts, b'')
            if not data:
                stop.set()
            return data
        def close(self):
            self.closed = True
    port = Port()
    with patch.object(app.serial, 'Serial', return_value=port):
        app.monitor_worker(events, commands, stop, 'COM3')
    records = list(events.queue)
    assert ('prompt', 'ENTER SSID') in records and ('prompt', 'ENTER PASSWORD') in records
    assert records[-1] == ('closed', None) and port.closed
    assert port.written == b'x'*40 + b'\n'

    # Exercise esptool's real output path with a firmware file, no serial device.
    import esptool
    with app.contextlib.redirect_stdout(app.ToolOutput(queue.Queue())):
        esptool.main(['image-info', str(app.firmware_root() / app.PRESETS[0][1] / 'firmware.bin')])
    print('PASS: four releases, write addresses, credential boundaries, tampering refusal, flash success/failure, serial framing/close, esptool output; no board access')


if __name__ == '__main__':
    run()
