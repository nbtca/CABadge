"""Check the installed flashing runtime and missing-dependency handling without a board."""
import queue
from types import SimpleNamespace
from unittest.mock import patch

import esptool
import workbench


assert workbench.check_flash_runtime() == esptool.__version__
with patch.dict('sys.modules', {'esptool': None}):
    try:
        workbench.check_flash_runtime()
    except RuntimeError as exc:
        assert 'run.cmd' in str(exc) and 'BOOT' not in str(exc)
    else:
        raise AssertionError('Missing esptool was accepted')

status = []
ui = SimpleNamespace(flashing=False, probing=False, port=SimpleNamespace(get=lambda: 'COM3'),
                     status=SimpleNamespace(set=status.append))
with patch.object(workbench, 'check_flash_runtime', side_effect=RuntimeError('missing esptool')), \
        patch.object(workbench.messagebox, 'showerror') as error:
    workbench.Workbench.start_flash(ui)
    error.assert_called_once()
    assert not ui.flashing and status

events = queue.Queue()
with patch.object(esptool, 'main') as main:
    workbench.flash_worker(events, workbench.RELEASE, 'COM3')
    args = main.call_args.args[0]
    assert args[:7] == ['--chip', 'esp32s3', '--port', 'COM3', '--baud', '115200', 'write-flash']
    assert args[7::2] == ['0x0', '0x8000', '0x10000']
    assert events.get_nowait() == ('flashed', True)
print('Runtime, missing dependency, preflight guard and flash dispatch: PASS; no serial port opened')
