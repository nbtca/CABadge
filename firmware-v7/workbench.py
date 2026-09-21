"""Native v7 entry: reuse the existing verified flasher, launch SDL for LVGL."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import queue
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import ttk, messagebox

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / 'tools' / 'flasher'))
from app import firmware_files, flash_worker, ANSI
from serial.tools import list_ports
import performance_probe

RELEASE = ROOT.parent / 'outputs' / 'cabadge-v7-mem-20260921'


def check_flash_runtime():
    try:
        import esptool
        from esptool.loader import StubFlasher
        from esptool.targets.esp32s3 import ESP32S3ROM
        if not StubFlasher(ESP32S3ROM).text:
            raise RuntimeError('ESP32-S3 stub is empty')
    except (ImportError, OSError, RuntimeError) as exc:
        raise RuntimeError('烧录环境不完整，请关闭工作台后通过 firmware-v7\\run.cmd 重新打开。\n'
                           f'Python：{sys.executable}\n原因：{exc}') from exc
    return esptool.__version__


class Workbench:
    def __init__(self, root):
        self.root, self.process, self.flashing = root, None, False
        self.events = queue.Queue()
        self.probing = False
        self.probe_cancel = threading.Event()
        self.port = tk.StringVar()
        self.profile = tk.StringVar(value='30 FPS · 40 MHz 预估')
        self.status = tk.StringVar(value='v7 Preview')
        root.title('NBTCA Badge TOOL — v7')
        root.geometry('620x510')
        root.minsize(580, 500)
        style = ttk.Style(root)
        style.theme_use('clam')
        style.configure('.', background='#f4f6f8', foreground='#1b2430', font=('Microsoft YaHei UI', 10))
        style.configure('TButton', padding=(18, 11), borderwidth=0, background='#e6edf5')
        style.configure('Accent.TButton', background='#365b86', foreground='white')
        style.map('Accent.TButton', background=[('active', '#25496f'), ('disabled', '#a6b4c4')])
        style.configure('TCombobox', padding=7, fieldbackground='white')
        style.map('TCombobox', fieldbackground=[('readonly', '#ffffff')])
        style.configure('TNotebook', borderwidth=0, background='#f4f6f8')
        style.layout('TNotebook.Tab', [])
        root.configure(background='#f4f6f8')
        outer = ttk.Frame(root, padding=26)
        outer.pack(fill='both', expand=True)
        ttk.Label(outer, text='NBTCA Badge TOOL', font=('Segoe UI Semibold', 23)).pack(anchor='w')
        row = ttk.Frame(outer)
        row.pack(fill='x', pady=(22, 18))
        self.ports = ttk.Combobox(row, textvariable=self.port, state='readonly', width=15)
        self.ports.pack(side='left')
        ttk.Button(row, text='刷新', command=self.refresh).pack(side='left', padx=10)
        tabs = ttk.Frame(outer)
        navigation = ttk.Frame(outer)
        navigation.pack(fill='x', pady=(0, 10))
        nav_buttons = []
        def choose_tab(index):
            for n, frame in enumerate((preview, flash)):
                if n == index:
                    frame.grid()
                else:
                    frame.grid_remove()
            for n, button in enumerate(nav_buttons):
                button.configure(style='Accent.TButton' if n == index else 'TButton')
        for index, title in enumerate(('界面预览', '固件安装')):
            button = ttk.Button(navigation, text=title, command=lambda i=index: choose_tab(i), style='Accent.TButton' if index == 0 else 'TButton')
            button.pack(side='left', padx=(0, 8))
            nav_buttons.append(button)
        tabs.pack(fill='both', expand=True)
        preview = ttk.Frame(tabs, padding=20)
        flash = ttk.Frame(tabs, padding=20)
        tabs.columnconfigure(0, weight=1)
        tabs.rowconfigure(0, weight=1)
        preview.grid(row=0, column=0, sticky='nsew')
        flash.grid(row=0, column=0, sticky='nsew')
        flash.grid_remove()
        ttk.Combobox(preview, textvariable=self.profile, values=['30 FPS · 40 MHz 预估', '保守档 · 10 MHz', '板端 P95 校准'], state='readonly', width=28).pack(anchor='w', pady=(4, 20))
        actions = ttk.Frame(preview)
        actions.pack(anchor='w')
        self.connect = ttk.Button(actions, text='连接实板', style='Accent.TButton', command=lambda: self.launch(False))
        self.connect.pack(side='left')
        self.preview = ttk.Button(actions, text='离线预览', command=lambda: self.launch(True))
        self.preview.pack(side='left', padx=10)
        sampling = ttk.Frame(preview); sampling.pack(anchor='w', pady=(12, 0))
        self.lcd_probe = tk.BooleanVar(value=True)
        self.probe_mode = tk.StringVar(value='完整分析')
        self.probe_button = ttk.Button(sampling, text='性能采样', command=self.start_probe)
        self.probe_button.pack(side='left')
        ttk.Checkbutton(sampling, text='包含实体屏传输', variable=self.lcd_probe).pack(side='left', padx=8)
        ttk.Combobox(preview,textvariable=self.probe_mode,values=['常规三场景','完整分析','触摸响应（8秒）'],state='readonly',width=24).pack(anchor='w',pady=(6,0))
        ttk.Button(preview, text='关闭预览', command=self.stop).pack(anchor='w', pady=(12, 0))
        ttk.Label(flash, text='CABadge  7.2.3-monitor', font=('Segoe UI Semibold', 15)).pack(anchor='w')
        ttk.Label(flash, text='c7c59dff · 实体屏与 USB 服务固件').pack(anchor='w', pady=(7, 16))
        self.install = ttk.Button(flash, text='安装 v7', command=self.start_flash, style='Accent.TButton')
        self.install.pack(anchor='w')
        ttk.Button(flash, text='查看日志', command=self.show_log).pack(anchor='w', pady=(10, 0))
        ttk.Label(outer, textvariable=self.status, foreground='#5b6778').pack(anchor='w', pady=(16, 0))
        self.log_lines = []
        self.refresh()
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.after(100, self.poll)

    def refresh(self):
        values = [p.device for p in list_ports.comports()]
        self.ports['values'] = values
        if self.port.get() not in values:
            self.port.set(values[0] if values else '')

    def stop(self):
        if not self.process or self.process.poll() is not None:
            self.process = None
            return
        pid = self.process.pid
        # Close our SDL window gracefully so USB upload sessions can be cancelled.
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def close_window(hwnd, _):
            owner = wintypes.DWORD()
            ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid:
                ctypes.windll.user32.PostMessageW(hwnd, 0x0010, 0, 0)
            return True
        ctypes.windll.user32.EnumWindows(callback_type(close_window), 0)
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            self.process.wait(timeout=2)
        self.process = None

    def launch(self, offline):
        if self.flashing or self.probing:
            return
        if not offline and not self.port.get():
            self.status.set('请选择设备串口')
            return
        self.stop()
        args = [str(ROOT / 'build' / 'JXBadgeSimulator.exe'), '--qspi', '10' if self.profile.get().startswith('保守') else '40']
        if self.profile.get() == '板端 P95 校准':
            try:
                args += ['--render-ms', str(performance_probe.calibration())]
            except (OSError, ValueError, KeyError) as exc:
                self.status.set('请先完成一次性能采样'); return
        args += ['--offline'] if offline else ['--port', self.port.get()]
        try:
            self.process = subprocess.Popen(args, cwd=ROOT, creationflags=subprocess.CREATE_NO_WINDOW)
        except OSError as exc:
            messagebox.showerror('无法打开预览', str(exc))
            return
        self.status.set('离线预览' if offline else '连接状态显示在预览窗口标题栏')

    def start_probe(self):
        if self.probing:
            self.probe_cancel.set(); self.status.set('正在结束采样…'); return
        if self.flashing or not self.port.get():
            self.status.set('请选择空闲的设备串口'); return
        self.stop(); self.probe_cancel.clear(); self.probing = True
        self.probe_button.configure(text='取消采样')
        for button in (self.connect, self.preview, self.install):
            button.state(['disabled'])
        self.status.set('连接诊断固件…')
        port = self.port.get()
        mode=self.probe_mode.get();audit=mode=='完整分析';manual=mode=='触摸响应（8秒）'
        lcd = self.lcd_probe.get() or audit or manual
        def sample():
            try:
                result, path = performance_probe.run(port, self.probe_cancel, lambda message: self.events.put(('probe_progress', message)), lcd=lcd,audit=audit,manual=manual)
                self.events.put(('probe_done', (result, path)))
            except Exception as exc:
                self.events.put(('probe_error', str(exc)))
        threading.Thread(target=sample, daemon=True).start()

    def start_flash(self):
        if self.flashing or self.probing:
            return
        if not self.port.get():
            self.status.set('请选择设备串口')
            return
        try:
            check_flash_runtime()
        except RuntimeError as exc:
            self.status.set('烧录环境不完整 · 尚未访问设备')
            messagebox.showerror('烧录环境检查失败', str(exc))
            return
        try:
            firmware_files(RELEASE)
        except (OSError, ValueError) as exc:
            messagebox.showerror('固件校验失败', str(exc))
            return
        self.stop()
        self.flashing = True
        for button in (self.connect, self.preview, self.install):
            button.state(['disabled'])
        self.status.set('正在安装 v7…')
        self.log_lines.clear()
        threading.Thread(target=flash_worker, args=(self.events, RELEASE, self.port.get()), daemon=True).start()

    def poll(self):
        while not self.events.empty():
            kind, value = self.events.get_nowait()
            if kind == 'probe_progress':
                self.status.set(value)
            elif kind in ('probe_done', 'probe_error'):
                self.probing = False; self.probe_button.configure(text='性能采样')
                for button in (self.connect, self.preview, self.install):
                    button.state(['!disabled'])
                if kind == 'probe_done':
                    result, path = value
                    if result.get('lcd_transfer'):
                        self.status.set('实屏采样完成')
                        lines = [f"{performance_probe.SCENES[r['scene']]}：完成 {r['completed_fps']:.1f} FPS，绘制 P95 {r['render_us_p95']/1000:.1f} ms，刷屏 P95 {r['flush_us_p95']/1000:.1f} ms" for r in result['scenes']]
                        messagebox.showinfo('实屏性能采样', '\n'.join(lines) + '\n\n统计完整 SPI 提交，不代表面板扫描频率或无撕裂。结果：\n' + str(path))
                    else:
                        self.profile.set('板端 P95 校准')
                        self.status.set(f"采样完成 · 绘制 P95 {result['render_ms']:.1f} ms · 可连接预览")
                        lines = [f"{performance_probe.SCENES[r['scene']]}：绘制 P95 {r['render_us_p95']/1000:.1f} ms，板端无屏 {r['headless_present_fps']:.1f} FPS" for r in result['scenes']]
                        messagebox.showinfo('性能采样', '\n'.join(lines) + '\n\n实体 LCD 帧率仍待测。结果：\n' + str(path))
                else:
                    self.status.set(value)
            elif kind in ('log', 'error'):
                self.log_lines.append(ANSI.sub('', str(value)))
            elif kind == 'flashed':
                self.flashing = False
                for button in (self.connect, self.preview, self.install):
                    button.state(['!disabled'])
                self.status.set('安装完成 · 按一下 RESET，实体屏独立运行' if value else '安装失败 · 查看日志')
        self.root.after(100, self.poll)

    def show_log(self):
        window = tk.Toplevel(self.root)
        window.title('安装日志')
        text = tk.Text(window, width=95, height=28, font=('Consolas', 10), wrap='word')
        text.pack(fill='both', expand=True)
        text.insert('1.0', ''.join(self.log_lines) or '尚未安装固件。')
        text.configure(state='disabled')

    def close(self):
        if self.probing:
            self.probe_cancel.set(); self.status.set("正在结束采样，请稍后关闭"); return
        if self.flashing:
            self.status.set('正在烧录，请等待完成')
            return
        self.stop()
        self.root.destroy()


if __name__ == '__main__':
    if '--check' in sys.argv:
        version = check_flash_runtime()
        firmware_files(RELEASE)
        assert (ROOT / 'build' / 'JXBadgeSimulator.exe').is_file()
        print(f'v7 simulator, firmware manifest, esptool {version}, ESP32-S3 stub: PASS; no serial port opened')
    else:
        root = tk.Tk()
        app = Workbench(root)
        if '--smoke-test' in sys.argv:
            root.attributes('-topmost', True)
            def check_preview():
                app.launch(True)
                assert app.process and app.process.poll() is None
            def finish_check():
                assert app.process.poll() is None
                app.stop()
                assert app.process is None
                root.destroy()
                print('Native workbench / offline preview / graceful close: PASS; no COM port opened')
            root.after(300, check_preview)
            root.after(2400, finish_check)
        root.mainloop()
