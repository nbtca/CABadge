# v1.1.0 安装与构建

内部固件版本：7.9.2-memory。下载与烧录地址、旧版分区升级方法见 [发布说明](RELEASE-v1.1.0.md)。发布 ZIP 解压到 `outputs/cabadge-v7.9.2-memory/`，使三个 BIN 与 verification.json 直接位于该目录。设备已运行本版时无需重刷。

## Windows 工作台

系统 Python 需含 Tkinter、pyserial、esptool；运行 `firmware-v7/run.cmd`，使用当前固件安装与诊断入口。壁纸管理使用设备本地网页。当前没有 USB 实板画面镜像；直接看 LCD。`tools/flasher/app.py` 的校验/烧录逻辑仍被复用，但旧独立窗口的诊断预设不是当前安装入口。

## 板端构建

现有环境使用 Windows、F 盘与 PlatformIO。未验证全新机器一键构建或字节级复现；下面是实际脚本约定。

1. 用 Python 3.10 创建 `firmware/bringup/.venv` 并安装 PlatformIO 6.2.0；只需这个虚拟环境，不需要旧 firmware 源码。构建脚本固定调用其中的 Python。
2. 用支持 tarfile 安全解包的 Python 运行 `firmware-v7/setup_deps.py`，获取 LVGL 9.4.0 与字体到 vendor。不再下载已删除模拟器使用的 SDL2；现有生成资源已入库，板端构建不必重新生成图片/字库。
3. 在仓库根目录运行：

```powershell
$env:TEMP = $env:TMP = 'F:\CABadgeBuild\temp'
$env:CABADGE_DISPLAY_PERF = '0'
$env:CABADGE_TRANSITION_CACHE_DEBUG = '1'
powershell -NoProfile -ExecutionPolicy Bypass -File firmware-v7/usb_screen/build.ps1
```

脚本固定 PlatformIO 核心目录 `F:/CABadgeBuild/platformio/idf61`、暂存 `F:/CABadgeBuild/staging/usb-screen-v7-idf61`。`platformio.ini` 固定 espressif32 7.1.3、framework-espidf 4.60100.0（ESP-IDF 6.1.0）；组件清单固定 LVGL 9.4.0、Adapter 0.7.1、ST77916 2.0.2，锁定信息在 `device/dependencies.lock`。

导出到 `outputs/cabadge-v7.9.2-memory/`，会覆盖同名本地产物，先另存发布附件。详细显示诊断开关为 1 时导出 diagnostic 后缀目录，不与正式包混用。

## 检查边界

可单独运行 `python firmware-v7/test_physical_display.py`、`python firmware-v7/test_transition_compositor.py`、`python firmware-v7/test_map_stream.py`；这些宿主检查依赖脚本所示的本地 Zig 工具，不启动 UI 模拟器。首次恢复环境需按各脚本路径安装工具。

旧桌面 CMake、模拟器、像素投屏和过时测试已删除，板端唯一构建入口是 `firmware-v7/usb_screen/build.ps1`。现有工作台使用 `usb_screen/framing.py` 的 USB 编解码；`python firmware-v7/usb_screen/framing.py` 可运行无设备自检。实体画面验收仍需要实际烧录版本。

项目源码/附件放 F:/计协吧唧，工具缓存与 TEMP/TMP 放 F:/CABadgeBuild。未上传的 outputs、vendor、虚拟环境不会随 GitHub 源码 ZIP 提供。
