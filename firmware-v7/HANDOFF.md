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
- 干净机器构建未验证；旧桌面 CMake/probe/test 部分过时，见仓库检查。

## 后续工作约束

用户当前要求：合理优化 → 编译 → 烧录 → 最小稳定性/功能检查 → 停止，由用户亲自体验。未经新任务要求不启动大规模 A/B、几十次往返或长采样，不用模拟器代替实体显示验收。

源码/附件使用 F:/计协吧唧；TEMP/TMP、工具链及暂存使用 F:/CABadgeBuild。保留用户修改，不调整 PC Wi-Fi，不在用户明确正在烧录时抢占串口。按任务范围修改；不要顺带调 QSPI、cache/XIP、draw unit、动画等参数。

收到新实板反馈，先更新根首板测试记录并区分用户目视、USB 日志和软件检查，再更新这里的当前状态。不要在本文件继续堆叠多个“当前版本”。

## 历史追溯

首板测试记录保留失败、回退和复测，不重写历史结论。此前7.x版本交接、临时端口/IP、安装路径与阶段待测项只属于当时状态。

[重写前完整交接（历史固定快照）](https://github.com/nbtca/CABadge/blob/107f518cade6b85d44fc918d985363973a5d8b22/firmware-v7/HANDOFF.md) · [文档索引](../docs/INDEX.md)
