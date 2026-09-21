"""USB display/touch replacement. The ESP32 owns every pixel of the badge UI."""
import queue
import json
import sys
from pathlib import Path
import struct
import threading
import time
import zlib
import tkinter as tk
from tkinter import ttk, filedialog
from tkinter.scrolledtext import ScrolledText
import serial
from serial.tools import list_ports
from PIL import Image, ImageTk, ImageDraw
from wallpaper import WallpaperPanel

if not getattr(sys, 'frozen', False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'flasher'))
from app import firmware_files, flash_worker, PRESETS, ANSI

FIRMWARES = [('CABadge v5 · 圆屏界面', 'usb-screen-v5-20260920', 'Wi-Fi / 蓝牙 · 摇晃唤醒 · 壁纸上传')] + PRESETS


def firmware_root():
    if getattr(sys, 'frozen', False):
        return Path(sys.executable).parent / 'firmwares'
    return Path(__file__).resolve().parents[2] / 'outputs'

WIDTH = HEIGHT = 360
MAX_PAYLOAD = 8 + WIDTH * 16 * 2
HELLO, READY, RECT, TOUCH, COMMAND, FRAME, HEARTBEAT, TEXT, KEY = range(1, 10)
WALL_BEGIN, WALL_CHUNK, WALL_FINISH, WALL_CANCEL, WALL_ACK, WALL_INFO, WALL_HOTSPOT = range(10, 17)


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


class Screen:
    def __init__(self):
        self.pixels = bytearray(WIDTH * HEIGHT * 2)
        self.ready = False
        self.complete = False

    def accept(self, kind, data):
        if kind == READY:
            if len(data) != 8 or struct.unpack('<HHI', data) != (360, 360, 0xc7c59dff):
                raise ValueError('固件尺寸或硬件版本不匹配')
            self.ready = True
            self.complete = False
            self.pixels[:] = b'\0' * len(self.pixels)
        elif kind == RECT and self.ready:
            if len(data) < 8:
                raise ValueError('画面区域数据不完整')
            x, y, w, h = struct.unpack_from('<HHHH', data)
            if not (w and h and h <= 16 and x + w <= WIDTH and y + h <= HEIGHT and len(data) == 8 + w * h * 2):
                raise ValueError('画面区域越界')
            for row in range(h):
                start = ((y + row) * WIDTH + x) * 2
                self.pixels[start:start + w * 2] = data[8 + row * w * 2:8 + (row + 1) * w * 2]
        elif kind == FRAME and self.ready and not data:
            self.complete = True
            return bytes(self.pixels)
        return None


def worker(port_name, commands, events, frames, stop):
    port = serial.Serial(port=None, baudrate=115200, timeout=0.02, write_timeout=0.2)
    port.dtr = port.rts = False
    port.port = port_name
    try:
        with port:
            decoder, screen = Decoder(), Screen()
            port.write(packet(HELLO))
            events.put(('status', '正在连接…'))
            last_receive = last_request = time.monotonic()
            started = last_receive
            while not stop.is_set():
                for _ in range(32):
                    try:
                        outgoing = commands.get_nowait()
                    except queue.Empty:
                        break
                    if screen.ready:
                        port.write(outgoing)
                data = port.read(min(max(port.in_waiting, 1), 65536))
                for kind, payload in decoder.feed(data):
                    image = screen.accept(kind, payload)
                    last_receive = time.monotonic()
                    if kind == WALL_ACK:
                        events.put(('wall_ack', payload))
                    if kind == WALL_INFO:
                        events.put(('wall_info', payload))
                    if kind == READY:
                        events.put(('ready', True))
                        events.put(('status', '已连接'))
                    if image is not None:
                        try:
                            frames.get_nowait()
                        except queue.Empty:
                            pass
                        frames.put_nowait(image)
                now = time.monotonic()
                if now - last_request > 2:
                    request = HELLO if not screen.ready or not screen.complete else HEARTBEAT
                    port.write(packet(request))
                    last_request = now
                if not screen.ready and now - started > 5:
                    events.put(('status', '当前固件不支持屏幕预览，请更新固件。'))
                    started = now
                if screen.ready and now - last_receive > 8:
                    raise TimeoutError('板端 8 秒无响应，请重新连接')
    except (serial.SerialException, OSError, ValueError, TimeoutError) as exc:
        events.put(('error', str(exc)))
    finally:
        events.put(('closed', None))


class App:
    def __init__(self, root):
        self.root = root
        self.events, self.frames, self.commands = queue.Queue(), queue.Queue(maxsize=1), queue.Queue(maxsize=256)
        self.stop = threading.Event()
        self.thread = self.flash_thread = self.pending_flash = None
        self.ready = self.flashing = False
        self.pointer = self.picture = self.last_pixels = self.transfer = None
        self.count = 0
        self.canvas_scale = 1.0
        self.canvas_origin = (0, 0)
        self.info_time = 0
        self.ignored_begins = 0
        self.nav_buttons = []
        root.title('NBTCA Badge TOOL')
        root.configure(bg='#f6f7f8')
        root.geometry('940x680')
        root.minsize(880, 650)
        style = ttk.Style(root)
        style.theme_use('clam')
        style.configure('.', font=('Microsoft YaHei UI', 10), background='#f6f7f8', foreground='#232931')
        style.configure('TFrame', background='#f6f7f8')
        style.configure('TLabel', background='#f6f7f8')
        style.configure('Muted.TLabel', foreground='#707884')
        style.configure('Title.TLabel', font=('Microsoft YaHei UI', 21, 'bold'))
        style.configure('Section.TLabel', font=('Microsoft YaHei UI', 11, 'bold'))
        style.configure('TButton', padding=(14, 9), borderwidth=0, background='#e5eaf0', foreground='#354c68', focusthickness=1)
        style.map('TButton', background=[('pressed', '#cbd8e7'), ('active', '#dce5ef')], foreground=[('disabled', '#a1a9b3')])
        style.configure('Accent.TButton', background='#365b86', foreground='white')
        style.configure('Small.TButton', padding=(0, 8), font=('Segoe UI Symbol', 12))
        style.map('Accent.TButton', background=[('pressed', '#294767'), ('active', '#476c95')], foreground=[('disabled', '#aab8c8'), ('!disabled', 'white')])
        style.configure('TCombobox', padding=7, fieldbackground='#ffffff', bordercolor='#d9dfe6', arrowcolor='#6b7888')
        style.configure('TEntry', padding=8, fieldbackground='#ffffff', bordercolor='#d9dfe6')
        style.configure('TProgressbar', background='#6485aa', troughcolor='#e4e8ed', borderwidth=0)
        style.configure('TScale', background='#f6f7f8', troughcolor='#d9e0e8', borderwidth=0)
        style.configure('TNotebook', background='#f6f7f8', borderwidth=0)
        style.configure('TNotebook', bordercolor='#f6f7f8', lightcolor='#f6f7f8', darkcolor='#f6f7f8')
        self.style_images = []
        for name, colors in [('TButton', ('#e5eaf0', '#cfdae7', '#dce5ef')), ('Accent.TButton', ('#365b86', '#294767', '#476c95'))]:
            images = []
            for color in (*colors, colors[0]):
                image = Image.new('RGBA', (32, 32))
                ImageDraw.Draw(image).rounded_rectangle((0, 0, 31, 31), radius=9, fill=color)
                if len(images) == 3:
                    ImageDraw.Draw(image).rounded_rectangle((1, 1, 30, 30), radius=8, outline='#789abe', width=2)
                images.append(ImageTk.PhotoImage(image))
            self.style_images.extend(images)
            element = name+'.round'
            style.element_create(element, 'image', images[0], ('pressed', images[1]), ('focus', images[3]), ('active', images[2]), border=10, sticky='nsew')
            style.layout(name, [(element, {'sticky': 'nsew', 'children': [('Button.padding', {'sticky': 'nsew', 'children': [('Button.label', {'sticky': 'nsew'})]})]})])
        knob = Image.new('RGBA', (18, 18))
        ImageDraw.Draw(knob).ellipse((1, 1, 16, 16), fill='#597da8')
        self.style_images.append(ImageTk.PhotoImage(knob))
        style.element_create('Tool.slider', 'image', self.style_images[-1])
        style.layout('Horizontal.TScale', [('Horizontal.Scale.trough', {'sticky': 'we', 'children': [('Tool.slider', {'side': 'left', 'sticky': ''})]})])
        style.layout('TNotebook.Tab', [])
        sidebar = tk.Frame(root, bg='#e9edf2', width=174)
        sidebar.pack(side='left', fill='y')
        sidebar.pack_propagate(False)
        tk.Label(sidebar, text='NBTCA', bg='#e9edf2', fg='#124689', font=('Segoe UI', 20, 'bold')).pack(anchor='w', padx=22, pady=(26, 0))
        tk.Label(sidebar, text='BADGE TOOL', bg='#e9edf2', fg='#6e7c8d', font=('Segoe UI', 9)).pack(anchor='w', padx=23, pady=(0, 28))
        self.tabs = ttk.Notebook(root)
        self.tabs.pack(side='right', fill='both', expand=True)
        self.screen_tab = ttk.Frame(self.tabs, padding=24)
        self.wallpaper = WallpaperPanel(self.tabs, self.send, self.start_wallpaper, self.cancel_wallpaper)
        self.flash_tab = ttk.Frame(self.tabs, padding=28)
        for page, title in [(self.screen_tab, '屏幕'), (self.wallpaper, '壁纸'), (self.flash_tab, '固件')]:
            self.tabs.add(page, text=title)
            button = tk.Button(sidebar, text=title, anchor='w', padx=16, pady=11, relief='flat', bd=0,
                               bg='#e9edf2', activebackground='#d5dfeb', fg='#46566b', font=('Microsoft YaHei UI', 11),
                               command=lambda p=page: self.tabs.select(p))
            button.pack(fill='x', padx=12, pady=3)
            self.nav_buttons.append((button, page))
        self.tabs.bind('<<NotebookTabChanged>>', self.tab_changed)
        device = tk.Frame(sidebar, bg='#e9edf2')
        device.pack(side='bottom', fill='x', padx=16, pady=22)
        tk.Label(device, text='设备', bg='#e9edf2', fg='#758191', font=('Microsoft YaHei UI', 9)).pack(anchor='w', pady=(0, 8))
        self.port = ttk.Combobox(device, width=12, state='readonly')
        self.port.pack(fill='x')
        row = tk.Frame(device, bg='#e9edf2')
        row.pack(fill='x', pady=8)
        self.connect_button = ttk.Button(row, text='连接', width=5, command=self.connect, style='Accent.TButton')
        self.connect_button.pack(side='left', fill='x', expand=True)
        self.refresh_button = ttk.Button(row, text='↻', width=2, command=self.refresh, style='Small.TButton')
        self.refresh_button.pack(side='right', padx=(4, 0))
        self.status = tk.StringVar(value='未连接')
        tk.Label(device, textvariable=self.status, wraplength=138, justify='left', bg='#e9edf2', fg='#657489', font=('Microsoft YaHei UI', 9)).pack(anchor='w', pady=(5, 0))
        ttk.Label(self.screen_tab, text='屏幕', style='Title.TLabel').pack(anchor='w')
        self.canvas = tk.Canvas(self.screen_tab, width=430, height=430, bg='#f6f7f8', highlightthickness=0)
        self.canvas.pack(fill='both', expand=True, pady=12)
        self.image_item = self.canvas.create_image(0, 0, anchor='nw')
        self.placeholder = self.canvas.create_text(215, 215, text='尚未连接', fill='#8692a1', font=('Microsoft YaHei UI', 14))
        self.canvas.bind('<Configure>', self.resize_canvas)
        nav = ttk.Frame(self.screen_tab)
        nav.pack(pady=(0, 10))
        for title, index in [('展示', 0), ('菜单', 1), ('壁纸', 2), ('设置', 3), ('连接', 4)]:
            ttk.Button(nav, text=title, width=6, command=lambda i=index: self.send(COMMAND, bytes((1, i)))).pack(side='left', padx=3)
        controls = ttk.Frame(self.screen_tab)
        controls.pack()
        ttk.Button(controls, text='息屏 / 唤醒', command=lambda: self.send(COMMAND, bytes((2, 0)))).pack(side='left', padx=4)
        ttk.Button(controls, text='模拟摇晃', command=lambda: self.send(COMMAND, bytes((3, 0)))).pack(side='left', padx=4)
        ttk.Label(self.flash_tab, text='固件', style='Title.TLabel').pack(anchor='w', pady=(0, 24))
        self.preset = ttk.Combobox(self.flash_tab, values=[item[0] for item in FIRMWARES], state='readonly')
        self.preset.pack(fill='x', pady=(0, 8))
        self.preset.current(0)
        self.preset.bind('<<ComboboxSelected>>', self.select_firmware)
        self.firmware_note = tk.StringVar()
        ttk.Label(self.flash_tab, textvariable=self.firmware_note, style='Muted.TLabel', wraplength=560).pack(anchor='w', pady=(0, 24))
        self.folder = tk.StringVar()
        self.choose_button = ttk.Button(self.flash_tab, text='选择本地固件…', command=self.choose_firmware)
        self.choose_button.pack(anchor='w', pady=6)
        self.auto_screen = tk.BooleanVar(value=True)
        self.auto_button = ttk.Checkbutton(self.flash_tab, text='完成后连接设备', variable=self.auto_screen)
        self.auto_button.pack(anchor='w', pady=(12, 18))
        self.flash_button = ttk.Button(self.flash_tab, text='安装固件', command=self.start_flash, style='Accent.TButton')
        self.flash_button.pack(fill='x', pady=(0, 14))
        self.progress = ttk.Progressbar(self.flash_tab, mode='indeterminate')
        self.progress.pack(fill='x')
        self.log_button = ttk.Button(self.flash_tab, text='查看记录', command=self.toggle_log)
        self.log_button.pack(anchor='w', pady=(24, 8))
        self.flash_log = ScrolledText(self.flash_tab, height=12, width=55, font=('Consolas', 9), wrap='word', state='disabled',
                                     relief='flat', bd=0, bg='#edf0f4', fg='#53637a', padx=12, pady=12)
        self.logs_visible = False
        self.select_firmware()
        self.canvas.bind('<ButtonPress-1>', self.press)
        self.canvas.bind('<B1-Motion>', self.move)
        root.bind('<ButtonRelease-1>', self.release)
        root.bind('<FocusOut>', self.release)
        self.canvas.bind('<KeyPress>', self.key)
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.refresh()
        root.after(30, self.pump)

    def tab_changed(self, event=None):
        for button, page in self.nav_buttons:
            selected = self.tabs.select() == str(page)
            button.configure(bg='#d5dfeb' if selected else '#e9edf2', fg='#2a4568' if selected else '#657489')
        if self.tabs.select() == str(self.wallpaper) and self.ready:
            self.send(WALL_INFO)

    def resize_canvas(self, event):
        size = max(1, min(event.width, event.height, 500))
        self.canvas_scale = size / 360
        self.canvas_origin = ((event.width-size)//2, (event.height-size)//2)
        self.canvas.coords(self.image_item, *self.canvas_origin)
        self.canvas.coords(self.placeholder, event.width/2, event.height/2)
        if self.last_pixels is not None:
            self.draw_frame(self.last_pixels)

    def draw_frame(self, data):
        size = max(1, round(360*self.canvas_scale))
        image = Image.frombytes('RGB', (360, 360), data, 'raw', 'BGR;16').resize((size, size), Image.Resampling.LANCZOS)
        mask = Image.new('L', (size, size))
        ImageDraw.Draw(mask).ellipse((0, 0, size-1, size-1), fill=255)
        circular = Image.new('RGB', (size, size), '#f6f7f8')
        circular.paste(image, (0, 0), mask)
        self.picture = ImageTk.PhotoImage(circular)
        self.canvas.itemconfigure(self.image_item, image=self.picture)
        self.canvas.itemconfigure(self.placeholder, state='hidden')

    def start_wallpaper(self, pixels):
        if self.transfer or self.flashing:
            return
        if not self.ready:
            self.wallpaper.note.set('请先连接设备')
            return
        if len(pixels) not in (0, 259200):
            raise ValueError('Invalid wallpaper size')
        self.transfer = {'pixels': pixels, 'session': 0, 'offset': 0, 'expected': WALL_BEGIN, 'sent': time.monotonic()}
        self.wallpaper.progress['value'] = 0
        self.wallpaper.set_busy(True)
        self.wallpaper.note.set('正在上传…')
        self.send(WALL_BEGIN, struct.pack('<II', len(pixels), zlib.crc32(pixels)))

    def cancel_wallpaper(self, message='上传已取消'):
        if self.transfer:
            if not self.transfer['session']:
                self.ignored_begins += 1
            self.send(WALL_CANCEL, struct.pack('<I', self.transfer['session']))
            self.transfer = None
            self.wallpaper.set_busy(False)
            self.wallpaper.note.set(message)

    def wallpaper_ack(self, data):
        t = self.transfer
        if len(data) != 10:
            return
        kind, result, session, offset = struct.unpack('<BBII', data)
        if kind == WALL_BEGIN and self.ignored_begins:
            self.ignored_begins -= 1
            return
        if t is None:
            return
        if kind != t['expected'] or (t['session'] and session != t['session']):
            return
        if result:
            self.cancel_wallpaper({1: '设备忙，请稍后重试', 2: '图片校验失败，请重试', 3: '壁纸保存失败', 4: '传输中断，请重试'}.get(result, '上传失败'))
            return
        if kind == WALL_BEGIN:
            t['session'] = session
        elif kind == WALL_CHUNK:
            if offset != t['offset']:
                self.cancel_wallpaper('传输位置不一致，请重试')
                return
        elif kind == WALL_FINISH:
            self.transfer = None
            self.wallpaper.set_busy(False)
            self.wallpaper.progress['value'] = 259200
            self.wallpaper.note.set('已保存并应用' if t['pixels'] else '已恢复默认壁纸')
            return
        self.wallpaper.progress['value'] = t['offset']
        if t['offset'] < len(t['pixels']):
            chunk = t['pixels'][t['offset']:t['offset']+1024]
            self.send(WALL_CHUNK, struct.pack('<II', session, t['offset'])+chunk)
            t['offset'] += len(chunk)
            t['expected'] = WALL_CHUNK
        else:
            self.send(WALL_FINISH, struct.pack('<I', session))
            t['expected'] = WALL_FINISH
            self.wallpaper.note.set('正在保存…')
        t['sent'] = time.monotonic()

    def refresh(self):
        ports = [p.device for p in list_ports.comports()]
        self.port['values'] = ports
        if self.port.get() not in ports:
            self.port.set(ports[0] if ports else '')

    def toggle_log(self):
        self.logs_visible = not self.logs_visible
        self.log_button.configure(text='收起记录' if self.logs_visible else '查看记录')
        if self.logs_visible:
            self.flash_log.pack(fill='both', expand=True)
        else:
            self.flash_log.pack_forget()

    def connect(self):
        if self.flashing:
            return
        if self.thread and self.thread.is_alive():
            self.cancel_wallpaper()
            self.stop.set()
            self.connect_button.configure(state='disabled')
            return
        if not self.port.get():
            self.status.set('未发现串口，请接上板子并点击刷新。')
            return
        self.stop.clear()
        self.ignored_begins = 0
        self.ready = False
        while not self.commands.empty():
            self.commands.get_nowait()
        self.connect_button.configure(text='断开')
        self.port.configure(state='disabled')
        self.refresh_button.configure(state='disabled')
        self.thread = threading.Thread(target=worker, args=(self.port.get(), self.commands, self.events, self.frames, self.stop), daemon=True)
        self.thread.start()

    def select_firmware(self, event=None):
        _, directory, description = FIRMWARES[self.preset.current()]
        self.folder.set(str(firmware_root() / directory))
        self.firmware_note.set(description)

    def choose_firmware(self):
        folder = filedialog.askdirectory(parent=self.root, title='选择包含 verification.json 和三个 bin 的固件目录')
        if folder:
            self.folder.set(folder)
            self.preset.set('自选固件目录')
            self.firmware_note.set('烧录前会校验 c7c59dff 硬件版本、文件哈希与分区。')

    def log(self, text):
        self.flash_log.configure(state='normal')
        self.flash_log.insert('end', ANSI.sub('', text).replace('\r', '\n'))
        self.flash_log.see('end')
        self.flash_log.configure(state='disabled')

    def flash_controls(self, busy):
        for widget in (self.flash_button, self.choose_button, self.auto_button, self.refresh_button, self.connect_button):
            widget.configure(state='disabled' if busy else 'normal')
        for widget in (self.preset, self.port):
            widget.configure(state='disabled' if busy else 'readonly')

    def start_flash(self):
        if self.flashing:
            return
        if self.transfer:
            self.status.set('请先完成或取消壁纸上传')
            return
        if not self.port.get():
            self.status.set('未选择串口，请接上板子并刷新。')
            return
        folder = Path(self.folder.get())
        try:
            firmware_files(folder)
            meta = json.loads((folder / 'verification.json').read_text(encoding='utf-8-sig'))
        except (OSError, ValueError) as exc:
            self.status.set(f'固件检查失败：{exc}')
            self.log(f'固件检查失败：{exc}\n')
            return
        stream = meta.get('display') == '360x360 RGB565; USB screen protocol v1'
        self.pending_flash = (folder, self.port.get(), stream and self.auto_screen.get())
        self.flashing = True
        self.flash_controls(True)
        self.flash_log.configure(state='normal')
        self.flash_log.delete('1.0', 'end')
        self.flash_log.configure(state='disabled')
        self.log(f'固件：{folder.name}\n端口：{self.port.get()}\n文件和分区校验通过。正在释放屏幕串口…\n')
        self.status.set('正在准备烧录…')
        self.release()
        self.ready = False
        self.stop.set()
        self.progress.start(15)
        self.root.after(30, self.begin_flash)

    def begin_flash(self):
        if self.thread and self.thread.is_alive():
            self.root.after(30, self.begin_flash)
            return
        folder, port, _ = self.pending_flash
        self.picture = None
        self.canvas.itemconfigure(self.image_item, image='')
        self.canvas.itemconfigure(self.placeholder, text='正在烧录固件', state='normal')
        self.status.set('正在烧录，请保持 USB 连接。')
        self.flash_thread = threading.Thread(target=flash_worker, args=(self.events, folder, port), daemon=True)
        self.flash_thread.start()

    def finish_flash(self, success):
        if self.flash_thread and self.flash_thread.is_alive():
            self.root.after(30, lambda: self.finish_flash(success))
            return
        _, port, auto = self.pending_flash
        self.pending_flash = None
        self.flashing = False
        self.progress.stop()
        self.flash_controls(False)
        self.connect_button.configure(text='连接')
        self.canvas.itemconfigure(self.placeholder, text='等待 ESP32 画面', state='normal')
        if not success:
            if not self.logs_visible:
                self.toggle_log()
            self.status.set('烧录失败，请查看日志；不会自动连接屏幕。')
            return
        self.log('\n烧录与写入校验完成。\n')
        self.status.set('烧录完成。' + ('正在等待串口恢复…' if auto else '需要投屏时，板端必须是 USB 屏幕固件。'))
        if auto:
            self.root.after(800, lambda: self.auto_connect(port, 10))

    def auto_connect(self, port, attempts):
        if self.flashing or (self.thread and self.thread.is_alive()):
            return
        ports = [p.device for p in list_ports.comports()]
        if port in ports:
            self.port['values'] = ports
            self.port.set(port)
            self.tabs.select(self.screen_tab)
            self.connect()
        elif attempts:
            self.root.after(500, lambda: self.auto_connect(port, attempts - 1))
        else:
            self.status.set('烧录成功，原串口尚未恢复。按 RESET，再刷新并连接屏幕。')

    def send(self, kind, payload=b''):
        if not self.ready:
            return
        try:
            self.commands.put_nowait(packet(kind, payload))
        except queue.Full:
            self.stop.set()
            self.status.set('输入队列拥堵，已停止连接，请重连。')

    def touch(self, x, y, down):
        scale = getattr(self, 'canvas_scale', 1.5)
        ox, oy = getattr(self, 'canvas_origin', (0, 0))
        x, y = min(359, max(0, int((x-ox) / scale))), min(359, max(0, int((y-oy) / scale)))
        self.send(TOUCH, struct.pack('<HHB', x, y, down))

    def press(self, event):
        self.canvas.focus_set()
        self.pointer = (event.x, event.y)
        self.touch(*self.pointer, 1)

    def move(self, event):
        self.pointer = (event.x, event.y)

    def release(self, event=None):
        if self.pointer:
            self.touch(*self.pointer, 0)
            self.pointer = None

    def key(self, event):
        special = {'BackSpace': 8, 'Return': 13, 'Escape': 27}
        if event.keysym in special:
            self.send(KEY, bytes((special[event.keysym],)))
        elif event.char and all(32 <= ord(c) <= 126 for c in event.char):
            self.send(TEXT, event.char.encode('ascii'))
        return 'break'

    def pump(self):
        while not self.events.empty():
            kind, value = self.events.get_nowait()
            if kind in ('status', 'error'):
                self.status.set(value)
                if self.flashing and kind == 'error':
                    self.log(value + '\n')
            elif kind == 'log':
                self.log(value)
            elif kind == 'flashed':
                self.finish_flash(value)
            elif kind == 'ready':
                self.ready = value
                self.send(WALL_INFO)
            elif kind == 'wall_ack':
                self.wallpaper_ack(value)
            elif kind == 'wall_info':
                try:
                    self.wallpaper.set_info(json.loads(value))
                except (ValueError, KeyError, TypeError):
                    self.wallpaper.note.set('设备返回的信息无效')
            elif kind == 'closed':
                self.cancel_wallpaper('连接已断开，上传未确认')
                self.ready = False
                self.last_pixels = None
                self.wallpaper.url.set('')
                self.wallpaper.network_note.set('连接设备后可用')
                self.pointer = None
                self.picture = None
                self.canvas.itemconfigure(self.image_item, image='')
                self.canvas.itemconfigure(self.placeholder, text='未连接 ESP32', state='normal')
                if not self.flashing:
                    self.connect_button.configure(text='连接', state='normal')
                    self.port.configure(state='readonly')
                    self.refresh_button.configure(state='normal')
        if not self.frames.empty():
            data = self.frames.get_nowait()
            if self.ready:
                self.last_pixels = data
                self.draw_frame(data)
                self.count += 1
                self.status.set('已连接 · '+self.port.get())
        if self.transfer and time.monotonic()-self.transfer['sent'] > 20:
            self.cancel_wallpaper('上传超时，请确认设备使用 v4 或更新固件后重试')
        if self.ready and time.monotonic()-self.info_time > 5:
            self.info_time = time.monotonic()
            self.send(WALL_INFO)
        if self.pointer:
            self.touch(*self.pointer, 1)
        self.root.after(30, self.pump)

    def close(self):
        if self.flashing:
            self.status.set('烧录正在进行，完成或报错后即可关闭。')
            return
        self.cancel_wallpaper()
        self.stop.set()
        self.root.destroy()


if __name__ == '__main__':
    window = tk.Tk()
    application = App(window)
    if len(sys.argv) == 3 and sys.argv[1] == '--smoke-check':
        def smoke():
            result = {'hardware_access': False}
            try:
                from esptool.loader import StubFlasher
                from esptool.targets.esp32s3 import ESP32S3ROM
                result['esp32s3_stub_bytes'] = len(StubFlasher(ESP32S3ROM).text)
                result['firmwares'] = [directory for _, directory, _ in FIRMWARES
                                       if firmware_files(firmware_root() / directory)]
                application.tabs.select(application.flash_tab)
                application.log('软件检查：固件、烧录依赖及界面已加载；未访问板子。\n')
                window.update_idletasks()
                result['flash_tab_visible'] = bool(application.flash_button.winfo_ismapped())
                result['default_firmware'] = Path(application.folder.get()).name
                result['auto_screen'] = application.auto_screen.get()
                result['title'] = window.title()
                application.tabs.select(application.wallpaper)
                window.update_idletasks()
                result['wallpaper_tab_visible'] = bool(application.wallpaper.apply_button.winfo_ismapped())
            except Exception as exc:
                result['error'] = str(exc)
            Path(sys.argv[2]).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
            window.after(500, window.destroy)
        window.after(300, smoke)
    window.mainloop()
