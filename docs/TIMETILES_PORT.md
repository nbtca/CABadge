# TimeTiles 当前移植进度（2026-10-15）

用户已提供 `reverse/mosaico-b359b358` 恢复材料并授权继续移植，下面 2026-10-11“尚无源码”的调查已成为历史状态。当前是根据恢复函数与直接提取像素资产重建的 ESP32-S3 适配工程，没有取得原作者完整工程。

`firmware-v7` 新增应用第六项TimeTiles：四模式、六闹钟/星期/贪睡、方向切换、六主题、私有NVS检查点和手动/共享SNTP校时。当前开发实板为 `7.9.23-timetiles-fix`，包含大时钟/简化菜单、数字双击六主题轮换、标题/数字区三击代码雨及摇晃飞散场景。仅烧录应用区，固件哈希及启动版本确认。旧版实屏布局、颜色及彩蛋失败反馈保留，本版继续由用户手测，未自动切页或运行实板功能测试。

详细操作、原函数证据、触摸适配差异、时间校准逻辑、断电限制与待测项统一见 [移植说明](../firmware-v7/TIMETILES.md)。最新烧录和修复证据统一在根主测试记录R104，前版历史为R100～R103；历史固件verification.json保留发布时含义。原恢复文件没有改写。

# 2026-10-11 历史：源码调查与 CABadge 移植评估

调查日期：2026-10-11。目标为已打板 `c7c59dff`、`firmware-v7`；本轮仅调查，未改固件、未烧录、未作实板验收。

## 结论

功能适合移到吧唧，但目前未找到可获取的 TimeTiles 本体源码。已取得官网公开固件并核对包内文件及 SHA256；它是 ESP32-S31 的编译产物，不能用于我们的 ESP32-S3。找到的官方框架、BSP 和工具仓库不能当作 TimeTiles 源码。

下一步优先取得原工程，再判断保留其 C 逻辑和绘图代码的范围。若取不到，需要按公开功能描述重新实现，并明确标为独立实现，不能称为原源码移植。

## 项目与公开入口

- [TimeTiles 项目页](https://mosaico-ideas.espressif.com/firmware/b359b358-bf99-4437-8cfd-4d8722748a71)：作者 ESP Friends，页面版本 1.0.3，更新时间 2026-10-10。页面描述本次增加联网对时。
- [公开项目数据](https://mosaico-ideas.espressif.com/api/v1/firmware-projects/b359b358-bf99-4437-8cfd-4d8722748a71)：`external_links`、`attachments`、`versions` 均为空，许可证字段为 Apache-2.0；这不能证明源码已公开，也未获得原工程的 LICENSE 文件。
- [公开 Iris 固件包](https://mosaico-ideas.espressif.com/api/v1/files/aus_3203e57349af187e5fa784869ad0f1d73c3f74a628a16b802801aaa5bc980021.irisfw)。本地保存于 `outputs/timetiles-research-20261011/TimeTiles-public-download.irisfw`。

公开功能：时钟、番茄钟、倒计时、最多 6 个闹钟；竖持后按 0/90/180/270 度方向稳定约 320 ms 切换模式，平放保持当前模式；触摸与画面一起旋转；六套主题、网格/柔光及整点、摇动、长按等动画彩蛋。具体时长、重复规则与触发条件保留在本地 `project.json`。

## 固件证据与版本差异

Iris 包是 ZIP，仅包含以下三个文件，没有 C/C++ 源文件或工程压缩包：

| 文件 | 大小 | 检查结果 |
| --- | ---: | --- |
| manifest.json | 578 B | `esp-iris-system-update/v1`；目标 chip_id=32、Flash=16 MiB |
| ota_0.bin | 2,499,488 B | 大小及 SHA256 与 manifest 一致 |
| partition-table.bin | 4,096 B | 大小及 SHA256 与 manifest 一致 |

包 SHA256：`94a39dbf4a496c784fce5ed7046613bcf2f2d4d5530bd09eddd454e0696820ab`。

应用 SHA256：`db1e45bf8801060a03a6ec25420cc354c31dd6a6732437fa4064badbd64771d0`。

**页面与包内部版本有差异，尚未确认对应关系：**

- 页面：1.0.3，2026-10-10 更新。
- manifest release 和 ESP application description version：1.12.2。
- application description：project_name=`timetiles`，编译日期字段 `Sep 24 2026`，时间 `16:07:33`，IDF=`v6.1-dev-6935-g08e0d30a74-dirty`。
- 编译产物字符串：`BUILD=timetiles-gsp-moments-v122 target=esp32s31 idf=6.1 gsp=1.2.0 480x480 RGB565 path=gsp-direct-draw`。

字符串还能定位 `timetiles_gsp_init`、`timetiles_store`、`timetiles_net`、`timetiles_board`、`timetiles_imu` 及开发机路径 `esp-mosaico-demo/timetiles/projects/timetiles`。这些是定位原工程的线索，不是已取得的源文件。

## 已找到的相关公开仓库

| 仓库 | 能提供什么 | 本体源码状态 |
| --- | --- | --- |
| [esp-mosaico-vibe](https://github.com/esp-mosaico/esp-mosaico-vibe) | 工作区入口、开发说明和固定依赖 | 当前完整文件树无 TimeTiles 路径 |
| [esp-mosaico-bsp](https://github.com/esp-mosaico/esp-mosaico-bsp) | CO5300、CST9217、BMI270、音频与电源板级驱动及示例 | 当前完整文件树无 TimeTiles 路径 |
| [esp-mosaico-utils](https://github.com/esp-mosaico/esp-mosaico-utils) | Iris、恢复/更新工具及 Hello World 模板 | 当前完整文件树无 TimeTiles 路径 |
| [esp-mosaico-claw](https://github.com/esp-mosaico/esp-mosaico-claw) | 另一个 Mosaico 应用工程 | 当前完整文件树无 TimeTiles 路径 |
| [esp-gsp](https://github.com/espressif/esp-gsp) | UI 框架头文件、适配器、示例及各目标的预编译库 | 当前完整文件树无 TimeTiles 路径；运行时以预编译库提供 |

GitHub 仓库/代码搜索另检查了 `TimeTiles`、`esp-mosaico-demo`、`timetiles_gsp` 及初始化/存储/姿态符号，没有找到该像素时钟的公开源码。搜索结果中的 `jfilter/timetiles` 是表格地图平台，与本项目无关。此结论限于本轮可访问的公开入口，不证明不存在私有或尚未索引的仓库。

五个仓库的文件树均未截断，树 SHA、文件数和搜索范围保存在 `outputs/timetiles-research-20261011/research.json`。框架版本不要混用：下载包字符串报告 GSP 1.2.0，而当前 Vibe 模板固定 1.5.1；当前 BSP/Vibe 构建说明也不能直接代表这份旧二进制的依赖。

[GSP 1.2.0 官方组件说明](https://components.espressif.com/components/espressif/esp-gsp/versions/1.2.0/readme?language=en) 列出 ESP32-S3、ESP-IDF >=6.0、RGB565 和 QSPI 支持。因此框架层没有明显的目标芯片阻碍，但这不等于 TimeTiles 已通过 S3 编译或实板性能验收。

## 与吧唧的差异和接入位置

| 项目 | TimeTiles/Mosaico | 当前 CABadge | 移植处理 |
| --- | --- | --- | --- |
| 主控 | ESP32-S31 | ESP32-S3R8、8 MiB PSRAM、16 MiB Flash | 重新编译应用；保持现有分区和工具链 |
| 屏幕 | 480×480 方屏，CO5300 | 360×360 圆屏，ST77916，QSPI 40 MHz | 调整布局和绘图范围；方屏整体缩到 360 后四角仍会被圆屏裁掉 |
| 触摸 | CST9217 | CST816D | 复用 `physical_display.c` 的驱动；如旋转，只变换应用自己的画面/触摸坐标 |
| 姿态 | BMI270 | SC7A20 三轴加速度 | 竖持四方向、摇动可由重力/加速度实现；不能沿用 BMI270 驱动 |
| UI | 包字符串报告 GSP DirectDraw | LVGL 9.4.0 + 现有 Direct 显示所有权管理 | 拿到源码后比较保留绘图与 LVGL 接入成本；不先替换全系统 UI |
| 网络/时间 | 页面宣称保存 Wi-Fi、自动联网对时 | 已有 Wi-Fi 历史及重连；SNTP 目前在地图请求路径中启动 | 复用网络配置，为时钟补独立的时间有效状态/对时入口 |
| 提醒 | 扬声器与振动马达 | 首板设计未配置对应硬件 | 现板可做视觉提醒/唤醒屏幕，不能提供同等的声音/振动提醒 |

本机核对的接入位置：

- `firmware-v7/ui/apps.c` / `apps.h`：应用创建、打开/关闭、33 ms tick、姿态分发与退出路径；需保留其他应用和原导航行为。
- `firmware-v7/usb_screen/device/src/motion_service.c`：已有滤波后的 XYZ 与摇动事件，但 `badge_apps_motion` 当前只传 XY。可靠区分竖持/平放需把 Z 或明确的朝向状态一并传给新应用，核对全部调用点及现有姿态检查。
- `firmware-v7/usb_screen/device/src/wifi_service.c`：已有 8 个网络历史、NVS 保存和自动重连；不另建一套配网逻辑。
- `firmware-v7/usb_screen/device/src/app_service.c:482`：当前地图请求触发 SNTP，不能作为时钟打开后必然已校准的保证。
- `firmware-v7/usb_screen/device/src/physical_display.c` / `.h` 及 `ui/ui_transition_compositor.c`：复用显示传输与缓冲生命周期；任何 Direct 绘图必须先取得独占显示权，退出后排空 DMA 并交回 LVGL，避免与壁纸/菜单同时提交。

圆屏内容优先按 360×360 重新布局，把主要按钮与文字放入圆形安全区。圆内最大轴对齐正方形边长约 254 px，可用于检查关键控件是否会被裁切；不把整套 480 布局直接硬缩进该正方形。

## 原源码需要的最小材料

取得对应版本的 `projects/timetiles` 完整工程，包括应用 C/C++、UI 场景 JSON、字体/图片等资源、CMake、依赖清单/锁文件、sdkconfig 默认配置和许可证；同时确认源码提交、网页 1.0.3 与下载包 1.12.2 的关系。仅有 `.irisfw`、生成的 UI bundle 或应用 BIN 无法完成可维护的源码移植。

## 实施与验收顺序

1. **核对原工程及版本，先接入一个时钟页面。** 验证：S3 实际编译、原分区不变、应用能退出、圆屏关键文字与触摸一致；按项目要求直接实板验证。
2. **接入四模式、方向切换和持久状态。** 验证：边界姿态不抖动、平放保持模式；离开应用后计时继续，暂停/继续正确；对时前提示未校准，对时跳变不导致重复闹钟，时区及一次/工作日/周末规则正确。
3. **补齐主题与动画彩蛋并复测。** 验证：与动态壁纸、菜单、息屏/唤醒、Wi-Fi/蓝牙共存；记录真实帧率、内存及屏幕目视反馈。TE 未接，不能预先承诺无撕裂。

在拿到本体源码或用户选择独立实现之前，保持现有固件不变。本轮没有新增实板结果，故未改 `首板测试记录.md` 的测试状态。
