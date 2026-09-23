# 当前版本 v1.1.0 / 7.9.2-memory

安装见 [当前发布说明](RELEASE-v1.1.0.md)。构建入口仍为 firmware-v7/usb_screen/build.ps1；当前 PlatformIO 固定 espressif32 7.1.3 / framework-espidf 4.60100.0 (ESP-IDF 6.1)，LVGL 9.4.0、Adapter 0.7.1。源依赖由 setup_deps.py 与 IDF Component Manager 获取，已提交 dependencies.lock。

构建工具 Python 路径仍为 firmware/bringup/.venv/Scripts/python.exe（仅需创建环境并安装 PlatformIO），暂存为 F:/CABadgeBuild/staging/usb-screen-v7-idf61，核心目录 F:/CABadgeBuild/platformio/idf61，TEMP/TMP 在 F:/CABadgeBuild/temp。

发布配置：CABADGE_DISPLAY_PERF=0、CABADGE_TRANSITION_CACHE_DEBUG=1。运行 build.ps1 导出 outputs/cabadge-v7.9.2-memory；构建会覆盖该目录，先备份发布附件。未验证全新机器重建。以下为历史 v1.0 环境说明，不作为当前版本配置。

---

# v1.0 安装与构建

## 使用发布固件

下载 `CABadge-v1.0-firmware-7.3.7-mem.zip`，解压到仓库根目录的 `outputs/cabadge-v7-mem-20260921/`，使 `verification.json` 和三个 BIN 直接位于该目录。此目录被 Git 忽略。

本次附件是既有实板固件，不需要为安装重新编译。只适用于 c7c59dff 硬件基线。烧录地址：

|文件|地址|
|---|---|
|bootloader.bin|0x0|
|partitions.bin|0x8000|
|firmware.bin|0x10000|

优先使用 `firmware-v7/run.cmd` 的“固件安装”入口；该入口会先核对硬件标识、文件哈希及分区边界。Windows PATH 中的 Python 需具有 Tkinter、esptool 5.4.0、pyserial 3.5 与 Pillow。串口先由其他程序释放。

没有桌面预览程序也可直接使用 esptool，先核对包内 SHA256SUMS.txt，再执行（将 COMx 替换为实际端口）：

```powershell
python -m esptool --chip esp32s3 --port COMx --baud 115200 write-flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

该命令在三个 BIN 所在目录运行。不要追加全片擦除：发布包不含 NVS 与用户壁纸备份。烧录后 RESET，确认板端版本 7.3.7-mem。

## 当前开发环境

这是既有 Windows 工程的发布快照，不宣称已支持任意电脑一键构建。本轮没有重新编译发布固件，也没有验证干净环境逐字节复现。

- 桌面：Python 3.14、Zig 0.14.1、CMake 4.4.3、Ninja 1.13.2、Pillow；LVGL 9.4.0、SDL2 2.32.10。
- 板端：Python 3.10 虚拟环境、PlatformIO 6.2.0，`platformio.ini` 固定 espressif32 6.12.0 / ESP-IDF。
- 编译暂存与工具缓存：`F:/CABadgeBuild/`。`TEMP`、`TMP` 均应放 `F:/CABadgeBuild/temp`。
- 源码及用户产物保存在 F 盘。旧工程的 `.venv`、`vendor` 目录联接不入库。

## 恢复依赖与构建顺序

1. 在 `firmware-v7/.venv` 建立 Python 3.14 环境，安装上述桌面依赖。运行 `firmware-v7/setup_deps.py` 下载 LVGL、SDL2 和字体。它是原工程初始化脚本的副本；字体下载 URL 沿用上游 main，若重新生成资源，不保证得到与此次发布完全相同的字节。
2. 现有板端构建脚本仍使用仓库根下 `firmware/bringup/.venv/Scripts/python.exe`。此处只需建立 Python 3.10 虚拟环境并安装 PlatformIO 6.2.0，不需要旧固件源码。PlatformIO 核心目录由脚本指定为 `F:/CABadgeBuild/platformio`。
3. 本地已有生成好的 `ui/assets.c`，可以直接运行 `firmware-v7/usb_screen/build.ps1` 构建板端；这会导出到 `outputs/cabadge-v7-mem-20260921` 并覆盖同名文件，开发前应另存发布附件。
4. 如需桌面程序，现有 CMake 从 `%USERPROFILE%/.platformio/packages/framework-espidf/components/json/cJSON` 找 cJSON；原开发机此路径通过兼容目录联接指向 F 盘。新机器需配置等价依赖路径后运行 `firmware-v7/build.ps1`。该脚本会重新生成 `ui/assets.c`；检查源码差异后再提交。
5. 桌面现有回归入口：`.venv/Scripts/ctest.exe --test-dir build --output-on-failure`。桌面测试不能代替实体画面验收。

上述是现状说明与恢复顺序，尚未进行新机器端到端验证。后续可单独改进依赖初始化和路径可配置性，不在 v1.0 发布整理中重构构建系统。

## 文档与工具边界

`tools/flasher` 的独立旧界面还包含历史诊断预设，相关历史包未随 v1.0 发布；本版请走 v7 工作台入口或上述明确烧录地址。`usb_screen` 是历史目录名，当前已移除旧持续像素投屏，保留状态、控制和壁纸服务。

历史性能报告中的 outputs 链接指向本地研究证据，未全部上传。源码、二进制、主测试记录及本次校验信息分别保存，避免把历史测试归到当前版本。
