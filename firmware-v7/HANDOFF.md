## 2026-09-27 最新：7.9.16-eaf-transition

动态首页导航冻结单个LCD-native EAF frame，借给统一cache/Direct，无导航截图/整帧copy/swap；LVGL已有RGB565_SWAPPED用于暂停/fallback，缩略图仅采样换序。STOP不再开下一帧，当前帧完成并drain后交权。返回首页转场结束后恢复EAF；首次载入分3个idle轮次预热邻居。Prototype/timing/显示参数不变。

编译/烧录hash及短USB动画检查通过：控制/名片/功能面板共+15 Direct，未新增miss；目标页EAF停止、返回恢复，静态回归通过。未采可比转场毫秒数据，不宣称提升比例。手指/目视反馈待用户。已停自动操作、恢复原ID10、串口释放。详情主记录R82及outputs/cabadge-v7.9.16-eaf-transition/basic-check.json。

## 2026-09-27 最新：7.9.15-eaf-timing

EAF资源新增Flash头JXWT v1 CONSTANT/PER_FRAME时间表；make_eaf.py保留GIF每帧duration，生成.eaf.timing，upload_eaf.py一起上传。正式Direct backend按当前帧时长累计绝对deadline，原显示架构不变；旧壁纸默认33333us。编译/烧录hash、固定30与80/180/420ms GIF、菜单暂停恢复、息屏/静态切换、真实重启持久化及旧EAF播放检查通过。实屏目视未验收。已删本轮测试素材，恢复测试前STATIC ID9，端口释放。罗小黑转换版是20FPS，已生成50ms sidecar但未改设备旧ID18；详情docs/EAF_TIMING.md与主记录R81。

## 2026-09-27 补充：emote原型已评估，默认仍7.9.14 Direct

CABADGE_EAF_EMOTE=1可选构建esp_emote_gfx3.0.5原型；播放无整帧buffer，短测24.46FPS vs Direct29.86FPS，PSRAM后端20KiB vs266KiB，但菜单需恢复253KiB冻结图且普通resume从头播放。未迁移默认。测试后已恢复原7.9.14 Direct BIN、ID18，串口释放。原型构建/USB菜单及静态切换通过，未做实屏目视；详情主记录R80与docs/EAF_EMOTE_PROTOTYPE.md。

## 最新补充：2026-09-27 / 7.9.14-eaf-direct

EAF 首页播放旁路 lv_eaf/LVGL renderer：官方格式解析及 RLE → PSRAM 单帧 LCD-native RGB565 → 原 compositor 任务/双 staging → ST77916。33,333us 调度，保留 3MiB 文件支持。按下手势/菜单/息屏/叠层时 join/drain 后交回 LVGL，冻结帧原地转换字节序；不另建全屏副本。QSPI/Adapter/缓存及其他应用参数未改。

编译和 COM3 应用烧录 hash 通过。USB 基本暂停/恢复、静态切换通过；最后 180 帧平均完成间隔 33.378ms，约29.96提交/秒，最差39.319ms，非面板物理扫描率。实屏颜色/上滑/残影等待用户确认。当前保留 ID18 罗小黑，串口释放。原始结果 outputs/cabadge-v7.9.14-eaf-direct/basic-check.json；主测试记录 R79。

## 最新补充：2026-09-27 / 7.9.13-eaf3m

EAF上限已扩至3MiB，连续空槽分配，流式Flash上传，无整文件PSRAM缓冲；静态库位置/分区不变。目标仍20FPS。完整终末地动画2066776 B/101帧已导入ID10，原7项CRC保留，静态切换及重启恢复经USB确认，画面目视待用户。编译和应用烧录哈希校验通过；串口已释放。详见根目录首板测试记录R77、assets/wallpapers/README.md和outputs/cabadge-v7.9.13-eaf3m/basic-check.json。旧固件不能安全写入含多槽EAF的库。

> 2026-09-27 EAF：源码/实板7.9.12-eaf，固定esp_lv_eaf_player 0.3.0；同一wallpaper库新增EAF类型，Flash mmap原数据、PSRAM单帧解码，目标20FPS，无新FS/分区。测试动态壁纸已导入ID6并选中；菜单/息屏暂停、返回恢复、静态切换销毁及重启恢复均由USB基本检查通过，视觉/触摸待用户反馈。RLE8/360×360/不透明/256KiB的首版素材约束及导入方式见assets/wallpapers/README.md；详细实测见R76。

> 2026-09-27 统一壁纸库：源码/实板7.9.11-wallpaper。晴日/流光不再编进应用，显式tools/install_wallpapers.py导入同一wallpaper分区，可像上传图片一样删除；空库为空白背景，不自动补回。原有壁纸ID保持兼容。编译、应用烧录哈希、首次导入及新增图片删除后重启持久性已验证；后续用户库发生额外变化且补导入USB超时，已暂停操作，详见主记录R75。

> 2026-09-27 电池显示：当前源码/实板7.9.10-battery，输出outputs/cabadge-v7.9.10-battery。控制中心改手机式电池轮廓与电压近似分档填充，低电红色、无效读数问号；持续≤3500mV约10秒提示一次“电量低，请充电”，≥3700mV持续30秒重新使能，提示避开转场/息屏。无精确SOC或充电状态推断，不增加任务，不改BlueMap或显示性能参数。电池策略检查、编译、应用烧录哈希及USB版本读回通过；实际放电提醒与图标目视验收待用户反馈。证据见主记录R74。

> 2026-09-24 BlueMap stale-while-revalidate：当前源码/实板7.9.9-mapstale，输出outputs/cabadge-v7.9.9-mapstale。TTL到期的Tile继续作为命中立即显示，本次refresh只等真正缺失的Tile；有界后台队列在前台完成后逐张更新，同key在飞行/排队时去重，成功才原位替换，失败保留旧图。新视野缺图会让后台第二路让位，第一路不等待后台。一轮实板地图检查：LOD2过期1～3张时refresh均0 MISS、约1.0～1.1秒完成，后台记录随后成功；另一次后台仍运行时refresh约1.4秒且只用第一路。详见主记录R73；v1.1.0发布附件不变。

> 2026-09-24 BlueMap 7.9.8-maprx实板采样：LOD2成功Tile约64～80 KiB/s、Pngle累计约1.87～4.18秒；LOD3四张冷Tile全成功，约57～70 KiB/s、Pngle约1.63～2.76秒。约132秒采样中BlueMap页Internal/DMA/PSRAM空闲最低50783/42419/4949828 B、始终2 lane。首次快速切换后的LOD2有两张在15379 B报DECODE invalid filter，后续同坐标成功；原因未定、未改固件。原始记录与边界见主记录R72。

> 2026-09-24 BlueMap接收与采样优化：当前源码/实板7.9.8-maprx，输出outputs/cabadge-v7.9.8-maprx。TCP窗口14360、邮箱12、Wi-Fi RX BA窗口8、HTTP client接收缓冲8192；静态Wi-Fi RX缓冲10不变。非交错8-bit RGB/RGBA PNG仅对500x500源图中命中128x128输出的像素做颜色转换与回调，其余PNG流式解压、滤波和CRC保留。真实LOD2 PNG离线采样/碎片/CRC检查通过，编译及应用分区烧录哈希通过，USB读回版本正确；当时尚未做本版冷区速度或低内存实板测试，详见主记录R71。Cache、2 lane、Keep-Alive、UI与v1.1.0发布附件不变。

> 2026-09-24 BlueMap大Tile修复：7.9.7-maplod，输出outputs/cabadge-v7.9.7-maplod。旧LOD2 `-1` 的可复现主因是12秒整Tile截止时间内HTTP body未读完；新版分类记录HTTP/read/timeout/connection/PNG parse/CRC/decode/size/other，时限30秒，适度提高TCP窗口、邮箱与HTTP client接收缓冲。一次LOD2/LOD3实板检查8张成功，详见主记录R70；v1.1.0发布附件不变。

> 2026-09-23 BlueMap冷区历史版本：7.9.6-mapcold的20张有界LRU、旧视野后台入缓存及双槽解码流水线见R69。本轮没有改动这些机制。

> 2026-09-23 BlueMap加载历史版本：7.9.4-mapfast的9张LRU、两路HTTP及实板结果见R67。

> 2026-09-23 BlueMap诊断：当前源码内部版本7.9.3-mapdiag，构建输出outputs/cabadge-v7.9.3-mapdiag；新增tools/map_perf_monitor.py，使用现有USB二进制query，无UART/ESP_LOG。发布v1.1.0附件及工作台默认7.9.2-memory保持原样。实板状态见首板记录R66。

> 2026-09-23 复刻树精简：65个旧报告/实验工具/废弃测试已移除。packet/Decoder独立到usb_screen/framing.py，工作台导入正常；板端重建三BIN与正式包完全相同，不重刷。软件验证见首板记录R65，历史工具从d1fe971或发布标签恢复。

# CABadge 当前交接

更新：2026-09-23。项目发布 **v1.1.0**，内部固件 **7.9.2-memory**，主开发目录 `firmware-v7`。不要与旧 `firmware` / Article 8.x 混用。

## 接手先读

1. [当前产品规范](../DESIGN.md)、[实际架构](../UI_ARCHITECTURE_V7.md)。
2. [安装与构建](../docs/BUILD.md)、[发布说明](../docs/RELEASE-v1.1.0.md)。
3. [首板测试记录](../首板测试记录.md) R63/R64、[已知限制](../docs/KNOWN_ISSUES.md)。

## 发布与设备状态

GitHub v1.1.0 对应内部 7.9.2-memory，发布二进制直接复用 R63，未为项目版本改号重新编译。附件本地目录 `outputs/release-v1.1.0`；工作台与构建默认目录 `outputs/cabadge-v7.9.2-memory`。源码、调试配置和固件包职责不同；不要覆盖原始 verification.json 的发布时含义。

最后一次自动实板检查在 COM3，应用写入 0x10000、hash 校验及版本读回通过，结束时回到主页并释放串口。这是当时状态，继续操作前应重新确认设备与端口，不能假定设备仍在线。

## 已实现

- ESP-IDF 6.1.0 / LVGL 9.4.0 / Adapter 0.7.1；QSPI 40 MHz、DRAW_UNIT=1、双整屏容量 PSRAM PARTIAL buffer、无 TE。
- Cached Bitmap + Direct Compositor；单份 LCD-native cache、revision、导航预热、hot/LRU 及内存压力 trim。
- 正常 LVGL 与 Direct 共用 23,040 B 内部 DMA staging，ownership 互斥、完成后复用。
- BlueMap 16 KiB HTTP/Pngle 流式采样，取消完整 RGBA；取消/退出按 worker 完成状态清理。
- 内置壁纸不再双份 PSRAM 常驻；名片没有独立快照；用户缩略图五项 LRU 与后台加载。
- 手机管理开放热点、无授权码；多图壁纸库、Wi-Fi 历史连接、BLE、Grok、地图及矿工保留。

不把“已实现”写成所有异常场景已验收。具体实现入口见架构文档；Flash L2 未启用。

## 已取得的证据

R63 完整编译和应用烧录通过；一次基本导航覆盖主页、设置/连接、图库、Grok、BlueMap 返回。地图四瓦片、一玩家、error0；Wi-Fi 已连接、BLE 广播、Direct errors0；八张壁纸持久化身份保持。

本轮历史最低空闲：Internal 39,828 B、DMA 32,040 B、PSRAM 4,219,412 B。它们是该次开机累计最低值，含之前 UI/Grok/预热，不是精确 PNG 峰值。不能外推长期无泄漏。

主机检查通过 PNG 碎片输入/采样/CRC/截断拒绝、DMA staging 生命周期/最终通知/错误排空、compositor stride/clip/取消。文档维护额外检查语法、资源和链接，不增加实体测试覆盖。

## 仍待确认

- 最新内存版真实手指滚动、目视撕裂/残影/颜色、动画结束一致性。
- BLE 对端连接、长期无线并发、断电/异常恢复。
- 旧 v1.0 分区升级路径本轮未重测。
- 干净机器构建未验证；旧桌面 CMake/probe/test 已从复刻树清理，历史可从 Git 恢复。

## 后续工作约束

用户当前要求：合理优化 → 编译 → 烧录 → 最小稳定性/功能检查 → 停止，由用户亲自体验。未经新任务要求不启动大规模 A/B、几十次往返或长采样，不用模拟器代替实体显示验收。

源码/附件使用 F:/计协吧唧；TEMP/TMP、工具链及暂存使用 F:/CABadgeBuild。保留用户修改，不调整 PC Wi-Fi，不在用户明确正在烧录时抢占串口。按任务范围修改；不要顺带调 QSPI、cache/XIP、draw unit、动画等参数。

收到新实板反馈，先更新根首板测试记录并区分用户目视、USB 日志和软件检查，再更新这里的当前状态。不要在本文件继续堆叠多个“当前版本”。

## 历史追溯

首板测试记录保留失败、回退和复测，不重写历史结论。此前7.x版本交接、临时端口/IP、安装路径与阶段待测项只属于当时状态。

[重写前完整交接（历史固定快照）](https://github.com/nbtca/CABadge/blob/107f518cade6b85d44fc918d985363973a5d8b22/firmware-v7/HANDOFF.md) · [文档索引](../docs/INDEX.md)
