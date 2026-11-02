# CABadge 应用源码

每个应用单独一个目录，分别保存应用代码、专用素材、来源说明和已有宿主检查。

| 应用 | 源码 | 内容 |
| --- | --- | --- |
| 应用 1：宝可梦对战 | [pokemon](pokemon/README.md) | 四精灵、单机对战、双机 BLE、战斗规则与素材 |
| 应用 2：TimeTiles | [timetiles](timetiles/README.md) | 时钟、番茄钟、倒计时、闹钟和字块特效 |
| 应用 3：Grok | [grok](grok/README.md) | 原生表情球、触摸与倾斜交互 |
| 应用 4：MC 实时地图 | [mc-map](mc-map/README.md) | BlueMap 地图、玩家、瓦片下载与缓存 |

这些是从本地 `7.9.25-poke-duel` 应用基线整理的**源码快照**。每个目录的
`MANIFEST.json` 记录文件大小、SHA-256 与原始位置；应用生产代码保持原样。
`ui/badge_ui.h` 等仅作为宿主接口声明随包保存；测试脚本只调整目录和工具路径。

应用依赖 CABadge 的 LVGL、字体、Wi-Fi/BLE、传感器和系统生命周期；不是可单独烧录的固件。
本目录发布不改变 [firmware-v7](../firmware-v7/README.md) 的公开源码版本、默认构建或 Release。
特别是宝可梦需要自己的新版 BLE 适配及接口，不能只复制一个 `.c` 文件到旧固件就运行。
MC 地图的界面仍位于共享应用壳 `apps.c`，保留原结构，具体依赖见各目录说明。

后续按应用更新相应目录；仅在应用依赖变化时同步所需接口和清单。
已停用的重力小球、奶蛙矿工和 FluidBox 不放入本应用目录。

CABadge 自编代码遵循仓库 MIT 许可证；第三方素材与恢复材料的权利说明以各目录为准。
