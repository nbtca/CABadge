import sys
import struct
import tkinter as tk
import unittest
from unittest.mock import patch
from PIL import Image
import viewer
from wallpaper import crop_image, rgb565


class WallpaperTests(unittest.TestCase):
    def test_crop_and_color(self):
        image = Image.new('RGB', (720, 360), 'red')
        image.paste('blue', (360, 0, 720, 360))
        left, cx, cy = crop_image(image, 1, -50, 80)
        self.assertEqual((cx, cy), (180, 180))
        self.assertEqual(rgb565(left), b'\0\xf8' * (360*360))
        right, cx, cy = crop_image(image, 2, 999, 999)
        self.assertEqual((cx, cy), (630, 270))
        self.assertEqual(rgb565(right), b'\x1f\0' * (360*360))

    def test_upload_acknowledgements_and_failure(self):
        root = tk.Tk()
        root.withdraw()
        with patch('viewer.list_ports.comports', return_value=[]):
            app = viewer.App(root)
        try:
            app.ready = True
            app.start_wallpaper(b'\0\xf8' * (360*360))
            messages = viewer.Decoder().feed(app.commands.get_nowait())
            self.assertEqual(messages[0][0], viewer.WALL_BEGIN)
            app.wallpaper_ack(struct.pack('<BBII', viewer.WALL_BEGIN, 0, 99, 0))
            collected = bytearray()
            while app.transfer and app.transfer['expected'] == viewer.WALL_CHUNK:
                kind, payload = viewer.Decoder().feed(app.commands.get_nowait())[0]
                self.assertEqual(kind, viewer.WALL_CHUNK)
                session, offset = struct.unpack_from('<II', payload)
                self.assertEqual((session, offset), (99, len(collected)))
                collected.extend(payload[8:])
                app.wallpaper_ack(struct.pack('<BBII', kind, 0, 99, len(collected)))
            self.assertEqual(len(collected), 259200)
            self.assertEqual(viewer.Decoder().feed(app.commands.get_nowait()), [(viewer.WALL_FINISH, struct.pack('<I', 99))])
            self.assertIsNotNone(app.transfer)  # Sending all bytes is not a successful commit.
            app.wallpaper_ack(struct.pack('<BBII', viewer.WALL_FINISH, 0, 99, 0))
            self.assertIsNone(app.transfer)
            self.assertIn('已保存', app.wallpaper.note.get())
            app.start_wallpaper(b'')
            app.commands.get_nowait()
            app.wallpaper_ack(struct.pack('<BBII', viewer.WALL_BEGIN, 0, 100, 0))
            app.commands.get_nowait()
            app.wallpaper_ack(struct.pack('<BBII', viewer.WALL_FINISH, 3, 100, 0))
            self.assertIsNone(app.transfer)
            self.assertIn('失败', app.wallpaper.note.get())
            app.start_wallpaper(b'')
            app.cancel_wallpaper()
            self.assertIsNone(app.transfer)
            self.assertFalse(app.wallpaper.busy)
            app.start_wallpaper(b'')
            app.wallpaper_ack(struct.pack('<BBII', viewer.WALL_BEGIN, 0, 123, 0))  # Late ACK from the cancelled upload.
            self.assertEqual(app.transfer['session'], 0)
            app.wallpaper_ack(struct.pack('<BBII', viewer.WALL_BEGIN, 0, 124, 0))
            self.assertEqual(app.transfer['session'], 124)
            app.cancel_wallpaper()
            app.wallpaper.set_info({'ip': '192.168.1.4', 'key': '0123456789abcdef', 'http': True, 'hotspot': False})
            self.assertEqual(app.wallpaper.url.get(), 'http://192.168.1.4/#key=0123456789abcdef')
            app.tabs.select(app.wallpaper)
            root.update()
            self.assertEqual(root.title(), 'NBTCA Badge TOOL')
        finally:
            for job in root.tk.call('after', 'info'):
                root.after_cancel(job)
            root.destroy()


if __name__ == '__main__':
    unittest.main()
