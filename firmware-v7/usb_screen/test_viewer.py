import struct
import unittest
from unittest.mock import Mock, patch
import queue
import threading
from viewer import packet, Decoder, Screen, App, worker, HELLO, READY, RECT, FRAME, TOUCH, TEXT, KEY, HEARTBEAT
from PIL import Image
import tkinter as tk
import time
import tempfile
from pathlib import Path
import viewer


class USBScreenTest(unittest.TestCase):
    def test_protocol_and_pixels(self):
        wire = packet(TOUCH, struct.pack('<HHB', 123, 234, 1))
        self.assertEqual(wire.hex(), '4a585549010405008e95707a7b00ea0001')
        decoder, result = Decoder(), []
        for value in b'boot log\r\n' + wire:
            result += decoder.feed(bytes([value]))
        self.assertEqual(result, [(TOUCH, struct.pack('<HHB', 123, 234, 1))])
        corrupt = bytearray(wire)
        corrupt[-1] ^= 1
        self.assertEqual(decoder.feed(corrupt + wire), result)
        with self.assertRaises(ValueError):
            packet(RECT, b'x' * 12000)
        screen = Screen()
        with self.assertRaises(ValueError):
            screen.accept(READY, struct.pack('<HHI', 360, 360, 0))
        screen.accept(READY, struct.pack('<HHI', 360, 360, 0xc7c59dff))
        screen.accept(RECT, struct.pack('<HHHHHHH', 0, 0, 3, 1, 0xf800, 0x07e0, 0x001f))
        frame = screen.accept(FRAME, b'')
        image = Image.frombytes('RGB', (360, 360), frame, 'raw', 'BGR;16')
        self.assertEqual([image.getpixel((i, 0)) for i in range(3)], [(255, 0, 0), (0, 255, 0), (0, 0, 255)])
        with self.assertRaises(ValueError):
            screen.accept(RECT, struct.pack('<HHHHH', 359, 0, 2, 1, 0))

    def test_input_and_worker(self):
        app = App.__new__(App)
        app.ready, app.commands, app.stop = True, queue.Queue(), threading.Event()
        app.touch(540, -10, 1)
        app.key(Mock(keysym='a', char='a'))
        app.key(Mock(keysym='Return', char='\r'))
        decoded = Decoder().feed(b''.join(app.commands.get() for _ in range(3)))
        self.assertEqual(decoded, [(TOUCH, struct.pack('<HHB', 359, 0, 1)), (TEXT, b'a'), (KEY, b'\r')])
        events, frames, stop, commands = queue.Queue(), queue.Queue(maxsize=1), threading.Event(), queue.Queue()
        incoming = packet(READY, struct.pack('<HHI', 360, 360, 0xc7c59dff))
        incoming += packet(RECT, struct.pack('<HHHHH', 0, 0, 1, 1, 0xf800)) + packet(FRAME)
        port = Mock()
        port.__enter__ = Mock(return_value=port)
        port.__exit__ = Mock(return_value=False)
        port.in_waiting = len(incoming)
        def read(_):
            stop.set()
            return incoming
        port.read.side_effect = read
        with patch('viewer.serial.Serial', return_value=port):
            worker('COM-TEST', commands, events, frames, stop)
        port.write.assert_called_with(packet(HELLO))
        self.assertEqual(frames.get_nowait()[:2], b'\0\xf8')
        results = list(events.queue)
        self.assertIn(('ready', True), results)
        self.assertFalse(any(kind == 'error' for kind, value in results))

    def test_integrated_flash_handoff(self):
        root = tk.Tk()
        root.withdraw()
        with patch('viewer.list_ports.comports', return_value=[]):
            app = App(root)
        app.port.set('COM-TEST')
        released = threading.Event()
        calls = []
        def screen_thread():
            app.stop.wait()
            time.sleep(0.08)
            released.set()
            app.events.put(('closed', None))
        def flash(events, folder, port):
            self.assertTrue(released.is_set())
            viewer.firmware_files(folder)
            calls.append(port)
            events.put(('log', 'Hash of data verified.\n'))
            events.put(('flashed', True))
        def settle():
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and (app.flashing or not calls):
                root.update()
                time.sleep(0.01)
            root.update()
        try:
            # Invalid release must not stop the existing display connection.
            with tempfile.TemporaryDirectory() as folder:
                app.folder.set(folder)
                app.start_flash()
                self.assertFalse(app.flashing)
                self.assertFalse(app.stop.is_set())
            app.select_firmware()
            app.thread = threading.Thread(target=screen_thread)
            app.thread.start()
            app.auto_screen.set(False)
            with patch('viewer.flash_worker', side_effect=flash):
                app.start_flash()
                self.assertTrue(app.flashing)
                app.close()
                self.assertTrue(root.winfo_exists())
                app.start_flash()  # Double click must not start a second writer.
                settle()
            self.assertEqual(calls, ['COM-TEST'])
            self.assertFalse(app.flashing)
            self.assertIn('Hash of data verified.', app.flash_log.get('1.0', 'end'))
            self.assertEqual(str(app.flash_button['state']), 'normal')
            with patch('viewer.flash_worker', side_effect=flash), patch.object(app, 'auto_connect') as reconnect:
                app.auto_screen.set(True)
                app.start_flash()
                settle()
                deadline = time.monotonic() + 2
                while not reconnect.called and time.monotonic() < deadline:
                    root.update()
                    time.sleep(0.01)
                reconnect.assert_called_once_with('COM-TEST', 10)
            def fail(events, folder, port):
                events.put(('error', 'simulated failure'))
                events.put(('flashed', False))
            with patch('viewer.flash_worker', side_effect=fail), patch.object(app, 'auto_connect') as reconnect:
                app.auto_screen.set(True)
                app.start_flash()
                settle()
                reconnect.assert_not_called()
                self.assertIn('simulated failure', app.flash_log.get('1.0', 'end'))
                self.assertIn('烧录失败', app.status.get())
            with patch('viewer.list_ports.comports', return_value=[Mock(device='COM-TEST')]), patch.object(app, 'connect') as connect:
                app.auto_connect('COM-TEST', 0)
                connect.assert_called_once()
                self.assertEqual(app.port.get(), 'COM-TEST')
                self.assertEqual(app.tabs.select(), str(app.screen_tab))
        finally:
            app.stop.set()
            for job in root.tk.call('after', 'info'):
                root.after_cancel(job)
            root.destroy()


if __name__ == '__main__':
    unittest.main()
