# 安装与构建

当前公开源码版本由 [version.json](../firmware-v7/version.json) 定义；正式附件、测试附件及旧标签源码例外见[项目版本表](../README.md#版本与资料对应)。工作台继续默认安装 v1.2.0，不会因这次构建工具整理改变默认固件。

## Windows 环境

使用 Python 3.10 或更新版本（含 Tkinter）和 PowerShell 7。从仓库根目录执行：

```powershell
pwsh -NoProfile -File tools/bootstrap.ps1 -Python python
F:\CABadgeBuild\venv\Scripts\python.exe firmware-v7/setup_deps.py
```

依赖固定在 [requirements-dev.txt](../tools/requirements-dev.txt)：PlatformIO、Zig、pyserial、esptool、Pillow。bootstrap 将环境安装到 `F:\CABadgeBuild\venv`，不再使用旧 `firmware/bringup/.venv`。`setup_deps.py` 获取 LVGL 9.4.0 和 OFL 字体到本工作树的 vendor；已有生成资源已入库，普通板端编译无需重新生成字库。

`CABADGE_BUILD_ROOT` 可覆盖工具根目录，默认保留 F 盘约定；TEMP、缓存、宿主测试和按工作树隔离的构建暂存均由它派生。源码与导出包留在项目 F 盘目录。依赖版本已固定，不承诺不同系统的逐字节固件复现；生成资源使用的上游字体仍须按任务核对。

## 板端构建

```powershell
$env:CABADGE_DISPLAY_PERF = '0'
$env:CABADGE_TRANSITION_CACHE_DEBUG = '0'
$env:CABADGE_EAF_EMOTE = '0'
$env:CABADGE_EAF_INTERPOLATE = '0'
pwsh -NoProfile -File firmware-v7/usb_screen/build.ps1
```

脚本先解析唯一版本配置，生成 UI 与 CMake 版本，再按一份源清单复制和计算输入指纹。每次构建使用全新暂存目录，避免已删除的源文件残留参与编译；先前构建暂存保留供追查。`source_commit`、`source_dirty`、变体和构建参数写入输出 `verification.json`。输出为本工作树 `outputs/builds/<工作树标识>/` 下的独立时间戳目录，包含三个 BIN、ELF、有效 sdkconfig、壁纸测试资源和清单，不覆盖旧固件包。

PlatformIO 固定 espressif32 7.1.3、framework-espidf 4.60100.0（ESP-IDF 6.1.0）；LVGL 9.4.0、Adapter 0.7.1、ST77916 2.0.2 等组件由清单和 dependencies.lock 约束。详细诊断、emote 与插帧构建使用独立配置目录，不能作为默认版本混用。

`-ConfigurationOnly` 仅输出解析结果，不创建暂存或编译；定制姓名必须指定独立 `-BuildVariant`，示例见[开发约定](DEVELOPMENT.md)。

## 宿主和仓库检查

```powershell
F:\CABadgeBuild\venv\Scripts\python.exe tools/repository/check_repository.py
F:\CABadgeBuild\venv\Scripts\python.exe tools/repository/test_repository.py
F:\CABadgeBuild\venv\Scripts\python.exe tools/release/test_prepare.py
F:\CABadgeBuild\venv\Scripts\python.exe firmware-v7/tests/host/test_build_config.py
F:\CABadgeBuild\venv\Scripts\python.exe firmware-v7/tests/host/test_workbench.py
```

其他固件宿主检查位于 `firmware-v7/tests/host/`，逐个运行需要的 `test_*.py`。它们使用中央环境中的 Zig；`CABADGE_ZIG` 可显式覆盖 Zig 可执行文件。保留原生产 C/测试桩检查，不启动 UI 模拟器，不连接实板。USB 探针会访问设备，仅在明确安排实板测试时运行。

GitHub Actions 自动运行仓库完整性、回归、发布修订、构建配置、工作台和 framing 离线检查。完整板端编译和全部依赖 C 的宿主检查仍按任务在本地运行；CI 不替代显示、手势、撕裂、无线或长稳验收。

## 工作台与发布

`firmware-v7/run.cmd` 使用中央环境打开原生工作台。按[正式发布说明](RELEASE-v1.2.0.md)准备原正式包，工作台默认目录仍为 `outputs/cabadge-v7.9.16-eaf-transition/`；三个 BIN 与 verification.json 应直接位于该目录。壁纸管理使用设备本地网页。

新交付包通过[发布工具](../tools/release/README.md)核对源码身份和指纹后生成。脏源码开发构建会明确标记，不能通过正式发布门禁。检查、编译和生成交付包均不自动烧录设备。

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

最小离线检查：`python tools/map_perf_monitor.py --self-test`；`python firmware-v7/tests/host/test_map_perf.py` 验证实际C端历史队列、覆盖与JSON边界。

### 7.9.6 BlueMap加载与内存边界

- Tile最多20张128×128 RGB565、上限640 KiB，key含world/LOD/X/Z；按访问更新LRU，当前视口命中项固定。跨refresh及退出地图保留，保留原60秒地形有效期。PSRAM低于1 MiB时回收未固定的旧Tile，退出地图时回收到至少1.5 MiB。
- 第一路复用已有badge_apps任务；第二路仅在余量充足时创建8192 B栈的map_http1，退出后释放。每路最多一个HTTP client、一份16 KiB流式解码输入、一份Pngle工作区和32 KiB输出Tile。每个活动Tile另有PSRAM双8 KiB接收槽与6144 B读取任务栈，网络读取与Pngle解码并行；PSRAM低于1.5 MiB或分配失败时回到同步读取。
- 新建第二路时按尚未分配的任务/HTTP资源预留，最严门槛Internal≥56 KiB、DMA≥48 KiB、Internal最大块≥12 KiB、PSRAM≥1.5 MiB；运行安全底线Internal32 KiB/DMA24 KiB。低内存时停止额外请求并可回退单路。已开始的旧Tile若仍在同world/LOD、距新视野中心不超过2格且PSRAM大于1 MiB，就允许下载完成并入缓存；旧serial的patch不会写入当前画布。
- GUI继续持有原有一张360×360画布；下载线程只传最多16行RGB565 patch（≤11520 B），队列1条，不分配第二张完整地图。GUI修改像素并invalidate局部区域；按住拖动时暂停接收，序列号丢弃旧视口patch。
- 协调任务的临时结果移到约3 KiB PSRAM，避免在HTTP/解码调用栈中叠加大结构；退出释放。保留小队列用于下次复用。
- 附加最小检查：`python firmware-v7/tests/host/test_map_cache.py`，覆盖LRU/pin和六档缩放、负坐标的区域采样。

7.9.7仅调整地图HTTP链路：大Tile总时限从12秒改为30秒并单独报告TIMEOUT；lwIP TCP接收窗口5760→11520 B、接收邮箱6→10，HTTP client内部接收缓冲2048→4096 B，读取任务仍按8 KiB送入双槽，Pngle仍使用16 KiB输入。TLS输入缓冲保持16 KiB。一次实板LOD2/3检查见主记录R70；全局TCP缓冲增加可能占用额外内存，原低水位单路降级仍在。
