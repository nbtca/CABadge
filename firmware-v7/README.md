> **v1.0 发布基线（2026-09-21）：固件 7.3.7-mem 已由用户确认上板、基本可正常使用，但仍存在较多 Bug，后续持续修复。不是全项或长期稳定性验收。当前安装源为 outputs/cabadge-v7-mem-20260921；以下旧版本说明保留历史含义。当前说明见仓库根 README 与 docs/RELEASE-v1.0.md。**

# CABadge v7 当前实体固件：7.3.4-async

实体屏菜单 → 设置 → 显示 → **FPS**：显示或关闭底部 `UI / LCD` 计数。默认关闭，开关不写 Flash，重启后关闭；息屏时隐藏。UI 是内容渲染完成率，LCD 是内容刷新周期传输完成率，排除计数器自身刷新；静态页面为 0，不代表屏幕停止扫描。TE 未接，不能把这两个数字解释成面板扫描 Hz。PC 离线预览只有开关布局，不提供实体屏测量值。

[双缓冲异步DMA实板对照](ASYNC_DMA_REPORT.md)；[固件和操作说明](../outputs/cabadge-v7-async-20260921/使用说明.md)。`run.cmd` 的安装入口已指向此版；旧版本产物保留。

本版为诊断构建，增加 FreeRTOS 任务运行时间统计；关闭 FPS 且没有电脑采样时，分组耗时与帧计数停用。LCD 驱动仍为 RGB565 / QSPI 40 MHz / 双 PSRAM PARTIAL 绘图缓冲、独立显示任务与双 DMA 传输块。旧 USB 像素投屏已移除，USB 烧录、状态控制和壁纸上传保留。

---
以下为历史记录，不代表当前安装版本。

# CABadge v7 当前实体固件：7.1.3-lcd

[安装与性能复测](../outputs/cabadge-v7-render-fast-20260921/使用说明.md)。`run.cmd` → 固件安装 → 确认7.1.3-lcd → 安装v7 → RESET。

本版启用-O2、不透明RGB565壁纸与RAM缓存、圆角缩略图缓存、静态蓝色名片缓存；保留Carousel/抽屉动效，40MHz与整帧DMA驱动保持。10项软件回归通过，实机提升待采样；用户已确认上一版7.1.2撕裂消失。

以下为历史版本说明，不代表当前安装包。

# CABadge v7 实体屏版

当前 **7.1.2-lcd**：[固件与安装说明](../outputs/cabadge-v7-lcd-refresh-20260921/使用说明.md)。针对撕裂改为PSRAM完整帧合成后连续DMA提交，保留40MHz，新增实体绘制/传输计时。TE未连接，实际改善待复测。

关闭旧工作台，双击 `run.cmd` → 固件安装 → 确认7.1.2-lcd → 安装v7 → RESET。采样见[PERFORMANCE.md](PERFORMANCE.md)。本轮未烧录。

以下为7.1.0-preview历史说明；旧安装版本、缓冲和无屏限制不代表当前实体版。7.1.2实体端已改全帧PSRAM缓冲，PC仍用16行预览缓冲，校准为近似预算。

# CABadge v7 Preview

版本 `7.1.0-preview`，冻结硬件 `c7c59dff`。独立源码目录 `firmware-v7`，原 `firmware` 继续供 v6 使用。此版交付核心圆屏交互预览和配套 USB 服务固件，尚未烧录实板；本轮补齐既定动效清单与无屏性能采样；完整 DESIGN.md 的实屏验收仍待进行。

## 本轮新增

无线详情的 Liquid 开关（160 ms、最多约 9% 形变）、亮度装饰尾随（最多 4 px，停止输入后 80 ms 收敛）、抽屉圆角过渡、应用按钮 Notify（真实成功后保留 1.2 秒）；手机 Web 的原地确认与可反向的 180 ms 分区底板。Carousel、蓝色名片及抽屉跟手保留。所有新动效支持减少动态。

**请关闭旧工作台后重新打开。** 固件安装已指向 [7.1.0-preview](../outputs/cabadge-v7-motion-perf-20260920)。新增“性能采样”按钮，完整步骤和指标边界见 [PERFORMANCE.md](PERFORMANCE.md)。新固件本轮未自动烧录。

## 打开与使用

历史 R18（7.0.0-preview）PC 修复：解决“减少动态”开启时 Wi-Fi、蓝牙、手机管理详情页被连接页遮挡的问题。当前 PC 更新兼容已安装的 7.0.0-preview，关闭旧预览后重新连接即可，无需重烧。共用源码已修，历史 ESP32 BIN 未重建；修复证据在 `outputs/cabadge-v7-pc-panel-fix-20260920`。

双击 [run.cmd](run.cmd) 打开 **NBTCA Badge TOOL — v7**。启动器使用 PATH 中已装好 esptool 5.4.0、pyserial、Tkinter 的 Python 3.14 环境；不再使用仅供编译的 bringup 虚拟环境。临时文件使用 F 盘，禁用 Python 字节码缓存写入。

- **离线预览**：无需板子；数据为本地示例/未知状态，不会建立 Wi-Fi 或 BLE 连接。
- **连接实板**：需要先安装此版配套固件。旧诊断/v5/v6 不支持这个服务握手，不能直接连接冒充成功。
- **固件安装 → 安装 v7**：复用原烧录器的 PCB、SHA-256、分区校验，写入 bootloader、partitions、firmware。先关闭本工具的预览以释放串口；其他应用占用串口需自行关闭。安装后按一下 RESET，再点连接实板。本轮没有执行安装。
- 预览在独立 SDL 窗口中运行，电脑运行共用 LVGL；标题栏显示离线/实板连接状态。

圆屏操作：左右拖动切换壁纸与蓝色名片；向下拉出控制中心，向上拉出功能面；在壁纸页拖动 Carousel，按“应用”提交选择。控制中心短按无线开关，长按进入详情。亮屏摇晃不跳页，息屏摇晃仅唤醒。

电脑键盘可输入 Wi-Fi 密码。F5 重连，F6 模拟一次摇晃事件，F7 息屏/唤醒。JPG/PNG/BMP 可拖到窗口：离线模式只加入预览，联调模式经原 USB 上传服务保存并应用（随后读回 CRC 核对的实际资源）。手机上传仍复用已有 HTTP 页面。

## 接近实机的性能约束

两端共用 LVGL **9.4.0**、同一 UI、字库、图像和 RGB565 输出；软件渲染、360×360、单个 16 行绘图缓冲。SDL2 **2.32.10** 使用软件 renderer，不以桌面 GPU 的无限帧率演示。

|档位|限制|性质|
|---|---|---|
|默认|30 FPS 上限；四线 QSPI 40 MHz 的区域传输预算|40 MHz 是待实屏验证的候选值，不是已测刷新率|
|实测校准|最大场景 MCU 绘制 P95 + 当前刷新区域传输预算|需要先安装 7.1.0-preview 并成功完成采样；仍非 LCD 实测|
|保守|同样 30 FPS 上限；四线 QSPI 10 MHz 预算|对应既有屏幕诊断配置；整屏理论传输约 51.84 ms|

每次刷新累积实际提交的矩形字节：`max(1000/fps, bytes×8/(QSPI_MHz×4000)+render_ms)` 毫秒后才允许下次渲染。整屏 259200 字节；40 MHz 纯传输 12.96 ms，10 MHz 为 51.84 ms。圆外像素没有擅自按 π/4 扣除。动画按实际时间推进；输入轮询与异步 USB 不被帧率限流阻塞。

**这不是 ESP32 指令级仿真。** 默认 `render_ms=0` 表示尚未使用 MCU 采样；完成性能采样后选择校准档，可自动加载实测绘制 P95。复杂缩放、中文、PSRAM 访问、无线负载和面板扫描/TE 仍可能让实机更慢。构建确认 ESP32-S3 240 MHz、8 MB Octal PSRAM/40 MHz、16 MB Flash；这些参数不能单独推算准确的 LVGL 帧率。实屏接通后测相同场景的渲染和 flush，再使用以下参数校准，不能把 PC 测量写成实屏成绩。

```powershell
.\build\JXBadgeSimulator.exe --offline --fps 30 --qspi 40 --render-ms 0
.\build\JXBadgeSimulator.exe --port COM3 --fps 30 --qspi 10
```

`--render-ms` 是每次刷新的保守固定 CPU 耗时附加项，未做逐场景硬件性能曲线拟合。

## 验证与文件

- [配套发布目录](../outputs/cabadge-v7-preview-20260920)；三个烧录 BIN 与 ELF、固件清单独立存放。
- [实际 LVGL 动效录像](../outputs/cabadge-v7-preview-20260920/v7-motion.mp4)、[页面预览](../outputs/cabadge-v7-preview-20260920/preview/index.html)。录像按真实帧时间导出，来源是本机 LVGL，不是网站录屏或实体 LCD。
- [软件检查证据](../outputs/cabadge-v7-preview-20260920/software-validation.json)。Windows 命名管道替身检验异步 USB 客户端的分片握手、请求、失败回执和不兼容拒绝，未打开 COM3；不能代替实板 USB。
- [BASELINE.json](BASELINE.json) 保留隔离前 52 个源文件哈希。`vendor`、`.venv` 为只读使用的原工程目录联接，不能向其中写配置/安装依赖。运行工具依赖上述已安装的 Python 环境和 `tools/flasher/app.py`；此预览入口尚非可搬到任意电脑的独立安装包。安装前及 `--check` 都会检查 esptool 和 ESP32-S3 stub。
- 字库经 `generate_assets.py` 生成，固定文案有覆盖检查；任意中文 SSID/用户文本并未保证全字库覆盖。紧凑密码键盘保留既有例外，优先电脑/手机输入。

## 构建与复查

```powershell
.\build.ps1
.\.venv\Scripts\ctest.exe --test-dir build --output-on-failure
.\usb_screen\build.ps1
python -B workbench.py --check
python -B test_flash_runtime.py
python -B performance_probe.py --self-test
python -B test_local_control.py
python -B workbench.py --smoke-test
```

设备构建独占 `F:\CABadgeBuild\staging\usb-screen-v7`，不会使用 v6 的暂存目录。工具链在 `F:\CABadgeBuild\platformio`，Zig 缓存与编译临时文件也位于 `F:\CABadgeBuild`；旧 C 盘路径仅保留目录联接以兼容既有构建。先构建 PC 以更新生成资源，再构建设备；本轮两端共用源码的哈希已核对。

## 后续验收边界

下一步先审核核心观感与动效，再装 v7 验证真实状态、Wi-Fi 列表/密码、BLE、息屏摇晃、USB/HTTP 壁纸同步及断连重连。真 LCD、触摸、背光仍待修复连接器后验证；本固件继续保持 BL GPIO1 和 CHG_ALLOW GPIO38 LOW。

无线详情长名称/完整中文回退、二维码实扫、真实触摸及运行时 RAM/PSRAM 峰值尚未完成，不能把本 Preview 作为整个 DESIGN.md 已验收。物理测试统一追加根目录 `首板测试记录.md`。
