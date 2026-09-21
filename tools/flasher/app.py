"""Native Windows flasher for the fabricated JXBadge c7c59dff board."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import queue
import re
import struct
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import serial
from serial.tools import list_ports

PRESETS = [
    ('Wi-Fi + BLE 连接测试', 'wifi-connect-20260919', '手机热点连接 · BLE 连接与重连 · 电池采样。屏幕继续拔着。'),
    ('电池采样 + Wi-Fi 扫描', 'headless-bringup-20260919', '电池 ADC 对表与被动扫描，不连接热点。屏幕继续拔着。'),
    ('主控 / Flash / PSRAM', 'bringup-20260919', '最小系统诊断，检查存储与持续运行。'),
    ('屏幕诊断 r2', 'display-bringup-20260919', '低亮度显示与触摸诊断。当前 FPC 座子待修复，请先保持拔屏。'),
]
REGIONS = [('bootloader.bin', 0x0), ('partitions.bin', 0x8000), ('firmware.bin', 0x10000)]
BOARD_HASH = 'c7c59dff5e6f2065a2157a81414d9d21c5cb1196a7b277656e15007753801a8d'
ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')


def firmware_root():
    if getattr(sys, 'frozen', False):
        return Path(sys.executable).parent / 'firmwares'
    return Path(__file__).resolve().parents[2] / 'outputs'


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


def credential(value, password=False):
    data = value.encode('utf-8')
    minimum, maximum = (8, 63) if password else (1, 32)
    if not minimum <= len(data) <= maximum or any(c < 32 or c == 127 for c in data):
        raise ValueError(f'{"密码" if password else "热点名称"}须为 {minimum}～{maximum} 个 UTF-8 字节，不能包含换行或控制字符。')
    if password and any(c > 126 for c in data):
        raise ValueError('当前诊断固件请使用英文、数字或英文符号密码。')
    return data + b'\n'


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


def monitor_worker(events, commands, stop, port_name):
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2)
    port.dtr = False
    port.rts = False
    port.port = port_name
    try:
        with port:
            events.put(('opened', port_name))
            pending = bytearray()
            while not stop.is_set():
                try:
                    payload = commands.get_nowait()
                except queue.Empty:
                    payload = None
                if payload is not None:
                    # USB console's FIFO is small; match the tested CLI pacing.
                    for offset in range(0, len(payload), 16):
                        if stop.is_set():
                            break
                        port.write(payload[offset:offset + 16])
                        port.flush()
                        time.sleep(0.03)
                    payload = None
                pending.extend(port.read(512))
                while b'\n' in pending:
                    line, _, remainder = pending.partition(b'\n')
                    pending = bytearray(remainder)
                    text = line.decode('utf-8', errors='replace').rstrip('\r')
                    events.put(('log', text + '\n'))
                    if text.strip() in ('ENTER SSID', 'ENTER PASSWORD'):
                        events.put(('prompt', text.strip()))
                if len(pending) > 8192:
                    events.put(('log', '[已丢弃超长无换行串口数据]\n'))
                    pending.clear()
    except (serial.SerialException, OSError) as exc:
        events.put(('error', f'串口连接中断或无法打开：{exc}\n确认串口号，并关闭占用它的其他程序。'))
    finally:
        port.close()
        events.put(('closed', None))


class App:
    def __init__(self, root):
        self.root = root
        self.events = queue.Queue()
        self.commands = queue.Queue()
        self.stop = threading.Event()
        self.mode = 'idle'
        self.pending_flash = None
        self.closing = False
        self.prompt = None
        self.wifi_payload = None
        self.secrets = []
        self.custom_folder = None
        self.status = tk.StringVar(value='就绪 · 选择串口后开始')
        self.port = tk.StringVar()
        self.preset = tk.StringVar(value=PRESETS[0][0])
        self.ssid = tk.StringVar()
        self.password = tk.StringVar()
        self.note = tk.StringVar(value=PRESETS[0][2])
        self.wifi_note = tk.StringVar(value='无线测试固件提示输入后，填写下方信息并发送。密码不会写入文件。')
        self._layout()
        self.refresh_ports()
        self.set_mode('idle')
        self.log('欢迎使用 JXBadge 烧录助手。\n连接电脑 USB 并打开板上电源；当前屏幕继续拔着。\n烧录完成后，在界面提示时按一下板上 RESET。\n')
        root.after(60, self.pump)
        root.protocol('WM_DELETE_WINDOW', self.close)

    def _layout(self):
        root = self.root
        root.title('JXBadge · 烧录助手')
        height = min(1020, root.winfo_screenheight() - 100)
        root.geometry(f'1100x{height}')
        root.minsize(940, min(860, height))
        root.configure(bg='#f2f4f7')
        style = ttk.Style(root)
        style.theme_use('clam')
        style.configure('.', font=('Microsoft YaHei UI', 10), background='#f2f4f7', foreground='#203148')
        style.configure('TButton', padding=(14, 9), background='#e7ebf0', borderwidth=0)
        style.map('TButton', background=[('active', '#d7e0eb')])
        style.configure('Primary.TButton', background='#1769c2', foreground='white', font=('Microsoft YaHei UI', 10, 'bold'))
        style.map('Primary.TButton', background=[('disabled', '#99b5d5'), ('active', '#14579e')], foreground=[('disabled', '#f0f3f7')])
        style.configure('TCombobox', padding=6, fieldbackground='white')
        style.map('TCombobox', fieldbackground=[('readonly', 'white'), ('disabled', '#edf0f4')])
        style.configure('TEntry', padding=7, fieldbackground='white')
        style.configure('Card.TFrame', background='white')
        style.configure('Card.TLabel', background='white')
        style.configure('Hint.TLabel', foreground='#637289', font=('Microsoft YaHei UI', 9))
        style.configure('TProgressbar', background='#1769c2', troughcolor='#dde4ed', borderwidth=0)
        outer = ttk.Frame(root, padding=24)
        outer.pack(fill='both', expand=True)
        title = ttk.Frame(outer)
        title.pack(fill='x', pady=(0, 18))
        ttk.Label(title, text='JXBadge', font=('Segoe UI Semibold', 26)).pack(side='left')
        ttk.Label(title, text='烧录助手', font=('Microsoft YaHei UI', 17)).pack(side='left', padx=16, pady=(8, 0))
        ttk.Label(title, text='ESP32-S3  /  16 MB  /  首板 c7c59dff', style='Hint.TLabel').pack(side='right', pady=(10, 0))
        card = ttk.Frame(outer, padding=18, style='Card.TFrame')
        card.pack(fill='x')
        card.columnconfigure(1, weight=1)
        ttk.Label(card, text='设备串口', style='Card.TLabel').grid(row=0, column=0, padx=(0, 18), sticky='w')
        self.port_box = ttk.Combobox(card, textvariable=self.port, state='readonly', width=48)
        self.port_box.grid(row=0, column=1, sticky='ew')
        self.refresh = ttk.Button(card, text='刷新串口', command=self.refresh_ports)
        self.refresh.grid(row=0, column=2, padx=(12, 0))
        ttk.Label(card, text='测试固件', style='Card.TLabel').grid(row=1, column=0, pady=(14, 0), sticky='w')
        self.preset_box = ttk.Combobox(card, textvariable=self.preset, values=[p[0] for p in PRESETS], state='readonly')
        self.preset_box.grid(row=1, column=1, pady=(14, 0), sticky='ew')
        self.preset_box.bind('<<ComboboxSelected>>', self.select_preset)
        self.browse = ttk.Button(card, text='选择固件目录', command=self.choose_folder)
        self.browse.grid(row=1, column=2, padx=(12, 0), pady=(14, 0))
        ttk.Label(card, textvariable=self.note, style='Card.TLabel', wraplength=720, foreground='#637289').grid(row=2, column=1, columnspan=2, pady=(10, 0), sticky='w')
        actions = ttk.Frame(outer)
        actions.pack(fill='x', pady=14)
        self.flash_button = ttk.Button(actions, text='烧录并监听', style='Primary.TButton', command=self.start_flash)
        self.flash_button.pack(side='left')
        self.monitor_button = ttk.Button(actions, text='仅打开串口', command=self.start_monitor)
        self.monitor_button.pack(side='left', padx=10)
        self.stop_button = ttk.Button(actions, text='关闭串口', command=self.stop_monitor)
        self.stop_button.pack(side='left')
        ttk.Label(actions, textvariable=self.status, style='Hint.TLabel').pack(side='right')
        self.progress = ttk.Progressbar(outer, mode='determinate', maximum=100)
        self.progress.pack(fill='x', pady=(0, 12))
        wifi = ttk.Frame(outer, padding=16, style='Card.TFrame')
        wifi.pack(fill='x', pady=(0, 16))
        wifi.columnconfigure(1, weight=1)
        wifi.columnconfigure(3, weight=1)
        ttk.Label(wifi, text='手机热点', style='Card.TLabel', font=('Microsoft YaHei UI', 11, 'bold')).grid(row=0, column=0, columnspan=5, sticky='w')
        ttk.Label(wifi, text='名称', style='Card.TLabel').grid(row=1, column=0, padx=(0, 8), pady=(12, 0))
        self.ssid_entry = ttk.Entry(wifi, textvariable=self.ssid, width=22)
        self.ssid_entry.grid(row=1, column=1, sticky='ew', pady=(12, 0))
        ttk.Label(wifi, text='密码', style='Card.TLabel').grid(row=1, column=2, padx=(16, 8), pady=(12, 0))
        self.password_entry = ttk.Entry(wifi, textvariable=self.password, show='●', width=22)
        self.password_entry.grid(row=1, column=3, sticky='ew', pady=(12, 0))
        self.send_button = ttk.Button(wifi, text='发送热点信息', command=self.send_wifi)
        self.send_button.grid(row=1, column=4, padx=(12, 0), pady=(12, 0))
        self.password_entry.bind('<Return>', lambda event: self.send_wifi())
        ttk.Label(wifi, textvariable=self.wifi_note, style='Card.TLabel', foreground='#637289', wraplength=880).grid(row=2, column=0, columnspan=5, sticky='w', pady=(10, 0))
        logbar = ttk.Frame(outer)
        logbar.pack(fill='x', pady=(0, 8))
        ttk.Label(logbar, text='运行日志', font=('Microsoft YaHei UI', 11, 'bold')).pack(side='left')
        ttk.Button(logbar, text='保存日志', command=self.save_log).pack(side='right')
        ttk.Button(logbar, text='清空显示', command=self.clear_log).pack(side='right', padx=8)
        self.footer = ttk.Label(outer, text='诊断固件每次启动会测试并擦写 Flash 最后 4 KB。烧录过程中请保持 USB 连接。', style='Hint.TLabel')
        self.footer.pack(side='bottom', anchor='w', pady=(10, 0))
        frame = ttk.Frame(outer)
        frame.pack(fill='both', expand=True)
        self.console = tk.Text(frame, background='#182535', foreground='#dce6f2', insertbackground='white',
                               font=('Consolas', 10), wrap='word', relief='flat', padx=14, pady=12, state='disabled')
        scroll = ttk.Scrollbar(frame, command=self.console.yview)
        self.console.configure(yscrollcommand=scroll.set)
        scroll.pack(side='right', fill='y')
        self.console.pack(side='left', fill='both', expand=True)

    def refresh_ports(self):
        current = self.selected_port()
        ports = sorted(list_ports.comports(), key=lambda p: (p.vid != 0x303a, p.device))
        labels = [f'{p.device}  ·  {p.description}' for p in ports]
        self.port_box['values'] = labels
        chosen = next((label for label in labels if label.split()[0] == current), labels[0] if labels else '')
        self.port.set(chosen)
        if not labels:
            self.status.set('未发现串口 · 检查 USB 后刷新')

    def selected_port(self):
        return self.port.get().split()[0] if self.port.get() else ''

    def select_preset(self, event=None):
        self.custom_folder = None
        self.note.set(next(p[2] for p in PRESETS if p[0] == self.preset.get()))

    def folder(self):
        if self.custom_folder:
            return self.custom_folder
        return firmware_root() / next(p[1] for p in PRESETS if p[0] == self.preset.get())

    def choose_folder(self):
        chosen = filedialog.askdirectory(title='选择含三个 bin 和 verification.json 的固件目录', parent=self.root)
        if chosen:
            try:
                firmware_files(chosen)
            except (ValueError, OSError) as exc:
                messagebox.showerror('固件目录不可用', str(exc), parent=self.root)
                return
            self.custom_folder = Path(chosen)
            self.preset.set('自选固件目录')
            self.note.set(chosen)

    def set_mode(self, mode):
        self.mode = mode
        idle = mode == 'idle'
        for widget in (self.port_box, self.preset_box):
            widget.configure(state='readonly' if idle else 'disabled')
        for widget in (self.refresh, self.browse, self.monitor_button):
            widget.configure(state='normal' if idle else 'disabled')
        self.flash_button.configure(state='normal' if mode in ('idle', 'monitor') else 'disabled')
        self.stop_button.configure(state='normal' if mode == 'monitor' else 'disabled')
        self.send_button.configure(state='normal' if mode == 'monitor' and self.prompt else 'disabled')

    def log(self, text):
        text = ANSI.sub('', str(text)).replace('\r', '\n')
        text = ''.join(c for c in text if c in '\n\t' or c.isprintable())
        for secret in self.secrets:
            text = text.replace(secret, '[密码已隐藏]')
        at_bottom = self.console.yview()[1] >= 0.995
        self.console.configure(state='normal')
        self.console.insert('end', text)
        if int(self.console.index('end-1c').split('.')[0]) > 12000:
            self.console.delete('1.0', '2001.0')
        self.console.configure(state='disabled')
        if at_bottom:
            self.console.see('end')

    def clear_log(self):
        self.console.configure(state='normal')
        self.console.delete('1.0', 'end')
        self.console.configure(state='disabled')

    def save_log(self):
        target = filedialog.asksaveasfilename(parent=self.root, title='保存当前显示的日志', defaultextension='.txt',
                                             initialfile=time.strftime('JXBadge-%Y%m%d-%H%M%S.txt'), filetypes=[('文本日志', '*.txt')])
        if target:
            try:
                Path(target).write_text(self.console.get('1.0', 'end-1c'), encoding='utf-8')
                self.status.set('日志已保存')
            except OSError as exc:
                messagebox.showerror('无法保存', str(exc), parent=self.root)

    def start_flash(self):
        if self.mode not in ('idle', 'monitor'):
            return
        port = self.selected_port()
        if not port:
            messagebox.showinfo('选择串口', '连接板子后点击刷新串口。', parent=self.root)
            return
        try:
            folder = self.folder()
            firmware_files(folder)
        except (OSError, ValueError, StopIteration) as exc:
            messagebox.showerror('固件检查失败', str(exc), parent=self.root)
            return
        self.pending_flash = (folder, port)
        if self.mode == 'monitor':
            self.stop_monitor()
        else:
            self.begin_flash()

    def begin_flash(self):
        folder, port = self.pending_flash
        self.pending_flash = None
        self.set_mode('flash')
        self.status.set('正在烧录 · 请保持连接')
        self.progress.configure(mode='indeterminate')
        self.progress.start(12)
        self.log(f'\n--- 烧录 {folder.name} → {port} ---\n固件文件检查通过。\n')
        threading.Thread(target=flash_worker, args=(self.events, folder, port), daemon=True).start()

    def start_monitor(self):
        if self.mode != 'idle':
            return
        port = self.selected_port()
        if not port:
            messagebox.showinfo('选择串口', '连接板子后点击刷新串口。', parent=self.root)
            return
        self.commands = queue.Queue()
        self.stop = threading.Event()
        self.prompt = None
        self.wifi_payload = None
        self.set_mode('monitor')
        self.status.set('正在打开串口')
        threading.Thread(target=monitor_worker, args=(self.events, self.commands, self.stop, port), daemon=True).start()

    def stop_monitor(self):
        if self.mode == 'monitor':
            self.set_mode('stopping')
            self.status.set('正在释放串口')
            self.stop.set()
            self.wifi_payload = None
            self.prompt = None

    def send_wifi(self):
        if self.mode != 'monitor' or not self.prompt:
            return
        try:
            ssid = credential(self.ssid.get())
            password = credential(self.password.get(), True)
        except ValueError as exc:
            messagebox.showerror('热点信息不完整', str(exc), parent=self.root)
            return
        self.secrets.append(self.password.get())
        if self.prompt == 'ENTER SSID':
            self.wifi_payload = password
            self.commands.put(ssid)
            self.wifi_note.set('热点名称已发送，等待固件请求密码…')
        else:
            self.commands.put(password)
            self.wifi_payload = None
            self.wifi_note.set('密码已发送，等待连接结果。')
            self.password.set('')
        self.prompt = None
        self.set_mode(self.mode)

    def handle_event(self, kind, value):
        if kind == 'log':
            self.log(value)
        elif kind == 'error':
            self.log('\n' + value + '\n')
            self.status.set('操作失败 · 请查看日志')
        elif kind == 'flashed':
            self.progress.stop()
            self.progress.configure(mode='determinate', value=0)
            self.set_mode('idle')
            if value:
                self.log('\n烧录与校验完成。串口打开后请按一下板上 RESET。\n')
                self.start_monitor()
        elif kind == 'opened':
            if self.mode == 'monitor':
                self.status.set(f'{value} 已连接 · 可按 RESET 重跑')
                self.log(f'\n--- {value} 串口已打开，115200 ---\n')
        elif kind == 'prompt' and self.mode == 'monitor':
            if value == 'ENTER PASSWORD' and self.wifi_payload is not None:
                self.commands.put(self.wifi_payload)
                self.wifi_payload = None
                self.password.set('')
                self.wifi_note.set('热点信息已发送，等待连接结果。')
                self.prompt = None
            else:
                self.prompt = value
                if value == 'ENTER SSID':
                    self.wifi_payload = None
                self.wifi_note.set('设备等待热点信息。填写名称和密码后点击“发送热点信息”。')
            self.set_mode(self.mode)
        elif kind == 'closed':
            self.prompt = None
            self.wifi_payload = None
            self.set_mode('idle')
            if self.closing:
                self.root.destroy()
            elif self.pending_flash:
                self.begin_flash()
            else:
                self.status.set('串口已关闭 · 日志已保留')

    def pump(self):
        for _ in range(150):
            try:
                kind, value = self.events.get_nowait()
            except queue.Empty:
                break
            self.handle_event(kind, value)
            if self.closing and self.mode == 'idle':
                return
        self.root.after(60, self.pump)

    def close(self):
        if self.mode == 'flash':
            messagebox.showinfo('正在烧录', '请等待本次烧录结束后关闭，避免中断写入。', parent=self.root)
            return
        self.pending_flash = None
        self.closing = True
        if self.mode == 'monitor':
            self.stop_monitor()
        elif self.mode == 'idle':
            self.root.destroy()


def main():
    if os.name == 'nt':
        import ctypes
        with contextlib.suppress(OSError, AttributeError):
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
    root = tk.Tk()
    app = App(root)
    if '--smoke-test' in sys.argv:
        target = Path(sys.argv[sys.argv.index('--smoke-test') + 1])
        def smoke():
            result = {'presets': [], 'ui': False, 'board_accessed': False}
            try:
                for _, directory, _ in PRESETS:
                    firmware_files(firmware_root() / directory)
                    result['presets'].append(directory)
                app.ssid.set('UI-test')
                app.password.set('test-only-123')
                app.set_mode('monitor')
                app.handle_event('prompt', 'ENTER SSID')
                app.send_wifi()
                assert app.commands.get_nowait() == b'UI-test\n'
                app.handle_event('prompt', 'ENTER PASSWORD')
                assert app.commands.get_nowait() == b'test-only-123\n'
                assert app.password.get() == ''
                app.log('test-only-123\n')
                assert 'test-only-123' not in app.console.get('1.0', 'end')
                # Validate monitor-to-flash handoff without invoking a device.
                app.port.set('COM3')
                app.set_mode('monitor')
                app.start_flash()
                assert app.mode == 'stopping' and app.stop.is_set() and app.pending_flash
                began = []
                original_begin = app.begin_flash
                app.begin_flash = lambda: began.append(True)
                app.handle_event('closed', None)
                assert began and app.mode == 'idle'
                app.begin_flash = original_begin
                app.pending_flash = None
                result['serial_handoff'] = True
                app.set_mode('idle')
                app.clear_log()
                app.log('界面检查通过。此窗口没有打开串口，也没有烧录板子。\n')
                result['ui'] = True
                root.update_idletasks()
                assert app.footer.winfo_ismapped() and app.footer.winfo_rooty() + app.footer.winfo_height() <= root.winfo_rooty() + root.winfo_height()
                result['footer_visible'] = True
                import esptool
                with contextlib.redirect_stdout(io.StringIO()):
                    esptool.main(['version'])
                result['esptool'] = True
                from esptool.loader import StubFlasher
                from esptool.targets.esp32s3 import ESP32S3ROM
                result['esp32s3_stub_bytes'] = len(StubFlasher(ESP32S3ROM).text)
            except Exception as exc:
                result['error'] = str(exc)
            target.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
            root.after(500, root.destroy)
        root.after(300, smoke)
    root.mainloop()


if __name__ == '__main__':
    main()
