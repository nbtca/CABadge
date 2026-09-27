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

当前源码导出到 `outputs/cabadge-v7.9.16-eaf-transition/`（v1.1.0 发布附件仍为 7.9.2-memory），会覆盖同名本地产物，先另存发布附件。详细显示诊断开关为 1 时导出 diagnostic 后缀目录，不与正式包混用。

## 检查边界

可单独运行 `python firmware-v7/test_physical_display.py`、`python firmware-v7/test_transition_compositor.py`、`python firmware-v7/test_map_stream.py`；这些宿主检查依赖脚本所示的本地 Zig 工具，不启动 UI 模拟器。首次恢复环境需按各脚本路径安装工具。

旧桌面 CMake、模拟器、像素投屏和过时测试已删除，板端唯一构建入口是 `firmware-v7/usb_screen/build.ps1`。现有工作台使用 `usb_screen/framing.py` 的 USB 编解码；`python firmware-v7/usb_screen/framing.py` 可运行无设备自检。实体画面验收仍需要实际烧录版本。

项目源码/附件放 F:/计协吧唧，工具缓存与 TEMP/TMP 放 F:/CABadgeBuild。未上传的 outputs、vendor、虚拟环境不会随 GitHub 源码 ZIP 提供。

## BlueMap USB 耗时诊断（7.9.3-mapdiag 起）

在项目根目录运行 `python tools/map_perf_monitor.py`。使用现有 pyserial；自动识别 CABadge USB，必要时加 `--port COM3`。先关闭工作台的连接、miniterm 等占用串口的程序。工具不会切页面；手动打开地图，等待加载，拖动一次，再缩放一次。Ctrl+C 停止。

日志保存在 `F:/CABadgeBuild/logs/bluemap-时间.log`，同名 `.jsonl` 保留原始记录。设备保留最近64条 Tile/Refresh 记录，读取不清空；历史覆盖会报告 lost。调试存储约14 KiB PSRAM，无逐帧或逐chunk输出，不启用 ESP_LOG/UART console。

协议复用 JXUI 帧：BR_READY capability 32；query type41 的 payload 为 `0x0a + uint32_le(after_seq)`；type42 返回带 map_perf=1 的 JSON 记录封装。原有诊断接口不变。`available=false` 表示诊断存储分配失败。

计时口径：
- first_byte 是 IDF 第一个已解析 HTTP header 回调，TTFB 是近似上界，不是线上首字节时间；不拆 DNS/TCP/TLS。
- bytes 是 HTTP body 回调实际交付字节，不含响应头；错误/取消请求也记录已收到数据。
- PNG与RGB565采样在同一次feed中完成，decode/downsample统计feed调用累计墙钟耗时。两个complete时间都表示decoder确认输出有效的时刻，不伪装成两个独立阶段。
- download_wall 包含其间穿插的decode，不能把两者简单相加。7.9.4的http_api_sum累加两路HTTP调用时间，可能互相重叠；network_phase_wall是整段下载调度的墙钟区间，含穿插的解码和渐进更新交接。players_wall含玩家HTTP及JSON解析，不能重复相加。
- refresh total从GUI接受地图请求到GUI接收结果，另列启动等待、worker_wall、GUI排队和GUI更新调用耗时（7.9.4累计本次刷新所有patch及最终状态更新）。GUI submit不代表LCD已显示。取消/过期结果为-2，未发生的里程碑为N/A。
- 7.9.3原版为串行且每次close。7.9.4最多两路独立HTTP连接；完整响应后按服务器keep-alive保留连接，损坏的空闲连接只重连一次。connects记录真正的新连接事件，reused记录本次请求是否实际复用了已有socket，不能仅凭配置开关判断。
- 7.9.7起Tile失败有独立错误码：-10 HTTP、-11 READ、-12 TIMEOUT、-13 CONNECTION、-14 PNG_PARSE、-15 CRC、-16 DECODE、-17 SIZE、-18 OTHER；JSON另带`error_category`、`error_detail`和`http_errno`。-2仍是取消，-3是第二路内存回退，-4是分配失败。完整HTTP body后才允许Keep-Alive复用。

最小离线检查：`python tools/map_perf_monitor.py --self-test`；`python firmware-v7/test_map_perf.py` 验证实际C端历史队列、覆盖与JSON边界。

### 7.9.6 BlueMap加载与内存边界

- Tile最多20张128×128 RGB565、上限640 KiB，key含world/LOD/X/Z；按访问更新LRU，当前视口命中项固定。跨refresh及退出地图保留，保留原60秒地形有效期。PSRAM低于1 MiB时回收未固定的旧Tile，退出地图时回收到至少1.5 MiB。
- 第一路复用已有badge_apps任务；第二路仅在余量充足时创建8192 B栈的map_http1，退出后释放。每路最多一个HTTP client、一份16 KiB流式解码输入、一份Pngle工作区和32 KiB输出Tile。每个活动Tile另有PSRAM双8 KiB接收槽与6144 B读取任务栈，网络读取与Pngle解码并行；PSRAM低于1.5 MiB或分配失败时回到同步读取。
- 新建第二路时按尚未分配的任务/HTTP资源预留，最严门槛Internal≥56 KiB、DMA≥48 KiB、Internal最大块≥12 KiB、PSRAM≥1.5 MiB；运行安全底线Internal32 KiB/DMA24 KiB。低内存时停止额外请求并可回退单路。已开始的旧Tile若仍在同world/LOD、距新视野中心不超过2格且PSRAM大于1 MiB，就允许下载完成并入缓存；旧serial的patch不会写入当前画布。
- GUI继续持有原有一张360×360画布；下载线程只传最多16行RGB565 patch（≤11520 B），队列1条，不分配第二张完整地图。GUI修改像素并invalidate局部区域；按住拖动时暂停接收，序列号丢弃旧视口patch。
- 协调任务的临时结果移到约3 KiB PSRAM，避免在HTTP/解码调用栈中叠加大结构；退出释放。保留小队列用于下次复用。
- 附加最小检查：`python firmware-v7/test_map_cache.py`，覆盖LRU/pin和六档缩放、负坐标的区域采样。

7.9.7仅调整地图HTTP链路：大Tile总时限从12秒改为30秒并单独报告TIMEOUT；lwIP TCP接收窗口5760→11520 B、接收邮箱6→10，HTTP client内部接收缓冲2048→4096 B，读取任务仍按8 KiB送入双槽，Pngle仍使用16 KiB输入。TLS输入缓冲保持16 KiB。一次实板LOD2/3检查见主记录R70；全局TCP缓冲增加可能占用额外内存，原低水位单路降级仍在。
