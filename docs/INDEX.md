# 复刻资料索引

当前 `main` 固件源码为 **7.9.23-timetiles-fix**；最新测试附件为 [v1.3.0-rc.1](https://github.com/nbtca/CABadge/releases/tag/v1.3.0-rc.1)，正式固件仍为 v1.2.0 / 7.9.16。当前板卡为 [V2.1 GEK100 / 星火计划](../hardware/pcb/current/README.md)，同时提供 KiCad 和 EDA；EDA 已替换为 2026-10-27 原生重画版，配套 V11 外壳，旧首板 c7c59dff 继续保留实测基线含义。

|任务|入口|
|---|---|
|从哪里开始|[项目说明](../README.md)|
|下载和烧录|[v1.2.0 正式版](RELEASE-v1.2.0.md)、[v1.3.0-rc.1 测试版](https://github.com/nbtca/CABadge/releases/tag/v1.3.0-rc.1)|
|自行编译|[BUILD](BUILD.md)|
|当前板卡与历史版本|[V2.1 双格式工程](../hardware/pcb/current/README.md)、[V1 KiCad 归档](../hardware/pcb/archive/V1_c7c59dff/README.md)|
|采购、PCB 与装配|[硬件](../hardware/README.md)、[J1 更正](J1-CONNECTOR.md)、[焊接检查](../焊接与分阶段上电检查_首板.md)|
|使用无线与壁纸|[板端服务](../firmware-v7/usb_screen/README.md)|
|使用应用|[APPS](../firmware-v7/APPS.md)、[GROK](../firmware-v7/GROK.md)、[FluidBox](../firmware-v7/FLUIDBOX.md)、[TimeTiles](../firmware-v7/TIMETILES.md)、[BLE 心率](../firmware-v7/HEART_RATE.md)|
|界面维护|[DESIGN](../DESIGN.md)、[架构](../UI_ARCHITECTURE_V7.md)、[交接](../firmware-v7/HANDOFF.md)|
|验证、限制及许可|[首板记录](../首板测试记录.md)、[已知问题](KNOWN_ISSUES.md)、[第三方来源](THIRD_PARTY.md)|

旧模拟器、像素投屏、阶段报告和废弃测试已从当前树删除。首板测试历史与已发布标签保留；需要旧文件时从下方快照或 v1.0/v1.1.0 标签获取，不将旧工具混入当前构建。

[精简前完整仓库](https://github.com/nbtca/CABadge/tree/d1fe9716b383dcb6fd292d683265beacd92fde64)

生产目录是冻结校验集合，包含 MANIFEST 引用的制造、验证及导出源快照，整组保留。硬件库、模型、素材源与许可证均为完整复刻或维护依赖，不按文件名相似判断重复。

仓库自检：`python docs/check_repository.py`；检查结构、语法、链接、ZIP/EDA 原生归档、受跟踪 MANIFEST 与 SHA256SUMS 清单，不连接设备。回归检查使用 `python docs/test_repository.py` 和 `python firmware-v7/test_workbench.py`，只使用临时测试文件，不打开设备或 GUI。
