"""Native wallpaper crop editor; the device owns storage and application."""
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from PIL import Image, ImageTk, ImageOps, ImageDraw
import webbrowser


def crop_image(source, zoom, cx, cy):
    side = min(source.size) / zoom
    cx = max(side / 2, min(source.width - side / 2, cx))
    cy = max(side / 2, min(source.height - side / 2, cy))
    return source.transform((360, 360), Image.Transform.EXTENT,
                            (cx-side/2, cy-side/2, cx+side/2, cy+side/2), Image.Resampling.BICUBIC), cx, cy


def rgb565(image):
    image = image.convert('RGB')
    if image.size != (360, 360):
        raise ValueError('Wallpaper must be 360x360')
    output = bytearray(360 * 360 * 2)
    data = image.tobytes()
    for i in range(360*360):
        r, g, b = data[i*3:i*3+3]
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        output[i*2:i*2+2] = value.to_bytes(2, 'little')
    return bytes(output)


class WallpaperPanel(ttk.Frame):
    def __init__(self, parent, send, upload, cancel):
        super().__init__(parent, padding=24)
        self.send, self.upload, self.cancel = send, upload, cancel
        self.source = self.preview = None
        self.cx = self.cy = 0
        self.drag = None
        self.busy = False
        self.info = {}
        ttk.Label(self, text='壁纸', style='Title.TLabel').pack(anchor='w', pady=(0, 18))
        body = ttk.Frame(self)
        body.pack(fill='both', expand=True)
        left = ttk.Frame(body)
        left.pack(side='left', anchor='n')
        self.canvas = tk.Canvas(left, width=320, height=320, bg='#e8edf3', highlightthickness=0, cursor='hand2', takefocus=True)
        self.canvas.pack()
        self.canvas.create_oval(12, 12, 308, 308, fill='#d9e2ed', outline='')
        self.placeholder = self.canvas.create_text(160, 160, text='选择照片', fill='#65768b', font=('Microsoft YaHei UI', 14))
        self.image_id = self.canvas.create_image(0, 0, anchor='nw')
        buttons = ttk.Frame(left)
        buttons.pack(fill='x', pady=(14, 8))
        self.choose = ttk.Button(buttons, text='选择照片', command=self.choose_image)
        self.choose.pack(side='left')
        self.center_button = ttk.Button(buttons, text='居中', command=self.center)
        self.center_button.pack(side='right')
        self.zoom = tk.DoubleVar(value=1)
        self.slider = ttk.Scale(left, from_=1, to=4, variable=self.zoom, command=lambda _: self.draw())
        self.slider.pack(fill='x', pady=8)
        self.apply_button = ttk.Button(left, text='上传并应用', style='Accent.TButton', command=self.apply)
        self.apply_button.pack(fill='x', pady=(10, 8))
        self.progress = ttk.Progressbar(left, maximum=259200)
        self.progress.pack(fill='x')
        self.note = tk.StringVar()
        ttk.Label(left, textvariable=self.note, wraplength=360, style='Muted.TLabel').pack(anchor='w', pady=(8, 0))
        right = ttk.Frame(body, padding=(22, 0, 0, 0), width=220)
        right.pack(side='left', fill='both', expand=True)
        ttk.Label(right, text='手机管理', style='Section.TLabel').pack(anchor='w')
        self.network_note = tk.StringVar(value='连接设备后可用')
        ttk.Label(right, textvariable=self.network_note, wraplength=210, style='Muted.TLabel').pack(anchor='w', pady=12)
        self.url = tk.StringVar()
        ttk.Entry(right, textvariable=self.url, state='readonly', width=23).pack(fill='x', pady=(0, 8))
        links = ttk.Frame(right)
        links.pack(fill='x')
        ttk.Button(links, text='复制地址', command=self.copy_url).pack(side='left', fill='x', expand=True, padx=(0, 4))
        ttk.Button(links, text='浏览器打开', command=self.open_url).pack(side='left', fill='x', expand=True)
        self.management_button = ttk.Button(right, text='允许手机管理', command=lambda: self.send(17))
        self.management_button.pack(fill='x', pady=(12, 3))
        self.hotspot_button = ttk.Button(right, text='开启直连热点', command=lambda: self.send(16))
        self.hotspot_button.pack(fill='x', pady=(18, 3))
        ttk.Button(right, text='刷新', command=lambda: self.send(15)).pack(fill='x', pady=3)
        transfer = ttk.Frame(right)
        transfer.pack(fill='x', pady=(12, 3))
        self.reset_button = ttk.Button(transfer, text='恢复默认', command=self.reset)
        self.reset_button.pack(side='left', fill='x', expand=True, padx=(0, 4))
        self.cancel_button = ttk.Button(transfer, text='取消上传', command=self.cancel, state='disabled')
        self.cancel_button.pack(side='left', fill='x', expand=True)
        self.canvas.bind('<ButtonPress-1>', self.press)
        self.canvas.bind('<B1-Motion>', self.move)
        self.canvas.bind('<ButtonRelease-1>', lambda _: setattr(self, 'drag', None))
        self.canvas.bind('<KeyPress>', self.key)
        self.set_busy(False)

    def choose_image(self):
        path = filedialog.askopenfilename(parent=self, title='选择照片', filetypes=[('照片', '*.jpg *.jpeg *.png *.bmp')])
        if not path:
            return
        try:
            with Image.open(path) as image:
                if image.format not in ('PNG', 'JPEG', 'BMP') or image.width * image.height > 40000000:
                    raise ValueError('请选择 4000 万像素以内的 JPG、PNG 或 BMP')
                self.source = ImageOps.exif_transpose(image).convert('RGB')
            self.center()
            self.note.set('')
            self.set_busy(False)
        except (OSError, ValueError, Image.DecompressionBombError) as exc:
            self.note.set(f'无法读取照片：{exc}')

    def center(self):
        if self.source is not None and not self.busy:
            self.cx, self.cy = self.source.width/2, self.source.height/2
            self.zoom.set(1)
            self.draw()

    def draw(self):
        if self.source is None or self.busy:
            return
        self.preview, self.cx, self.cy = crop_image(self.source, self.zoom.get(), self.cx, self.cy)
        mask = Image.new('L', (320, 320))
        ImageDraw.Draw(mask).ellipse((0, 0, 319, 319), fill=255)
        circular = Image.new('RGB', (320, 320), '#e8edf3')
        circular.paste(self.preview.resize((320, 320), Image.Resampling.LANCZOS), (0, 0), mask)
        self.photo = ImageTk.PhotoImage(circular)
        self.canvas.itemconfigure(self.image_id, image=self.photo)
        self.canvas.itemconfigure(self.placeholder, state='hidden')

    def press(self, event):
        if self.source is not None and not self.busy:
            self.canvas.focus_set()
            self.drag = event.x, event.y, self.cx, self.cy

    def move(self, event):
        if self.drag and not self.busy:
            x, y, cx, cy = self.drag
            scale = min(self.source.size) / (320*self.zoom.get())
            self.cx, self.cy = cx-(event.x-x)*scale, cy-(event.y-y)*scale
            self.draw()

    def key(self, event):
        shift = {'Left': (-5, 0), 'Right': (5, 0), 'Up': (0, -5), 'Down': (0, 5)}.get(event.keysym)
        if shift and not self.busy:
            self.cx += shift[0]
            self.cy += shift[1]
            self.draw()

    def apply(self):
        if self.preview is not None:
            self.upload(rgb565(self.preview))

    def reset(self):
        if messagebox.askyesno('恢复默认', '恢复默认壁纸？', parent=self):
            self.upload(b'')

    def set_busy(self, busy):
        self.busy = busy
        for widget in (self.choose, self.reset_button, self.hotspot_button):
            widget.configure(state='disabled' if busy else 'normal')
        for widget in (self.slider, self.center_button, self.apply_button):
            widget.configure(state='disabled' if busy or self.source is None else 'normal')
        self.cancel_button.configure(state='normal' if busy else 'disabled')

    def set_info(self, info):
        self.info = info
        self.management_button.configure(text='关闭手机管理' if info.get('authorized') else '允许手机管理')
        address = '192.168.4.1' if info.get('hotspot') else info.get('ip', '')
        self.url.set(f"http://{address}/#key={info['key']}" if address and info.get('http') and info.get('key') else '')
        self.hotspot_button.configure(text='关闭直连热点' if info.get('hotspot') else '开启直连热点')
        code = info.get('key', '')
        self.network_note.set(f"{info['ssid']}\n密码 {info['password']}" if info.get('hotspot') else (f"{address or 'BLE 管理'} · 剩余 {info.get('remaining', 0)} 秒\n授权码\n{code[:16]}\n{code[16:]}" if info.get('authorized') else '点击允许手机管理后使用'))

    def copy_url(self):
        if self.url.get():
            self.clipboard_clear()
            self.clipboard_append(self.url.get())
            self.note.set('地址已复制')

    def open_url(self):
        if self.url.get():
            webbrowser.open(self.url.get())
