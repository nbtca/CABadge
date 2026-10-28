# CABadge 硬件资料

**当前板卡：V2.1 GEK100 / 星火计划。** [打开当前版本说明](pcb/current/README.md)，选择 [KiCad 工程](pcb/current/kicad/CABadge_V2_1.kicad_pro)或[嘉立创EDA工程](pcb/current/easyeda/CABadge_V2_1_GEK100.epro2)。

| 版本 | 格式与来源 | 入口 |
| --- | --- | --- |
| 当前 V2.1 | KiCad 保留 2026-10-20 原文件；EDA 替换为 2026-10-27 原生重画工程 | [pcb/current](pcb/current/README.md) |
| V1 首板 c7c59dff | 仅 KiCad，连同原理图、项目库和模型归档 | [pcb/archive/V1_c7c59dff](pcb/archive/V1_c7c59dff/README.md) |

今天的重画工程属于 V2.1 的 EDA 格式更新，五张原理图复用、PCB 原生重画。旧的 KiCad 转换版 EDA 已从当前仓库文件中移除，过往版本由 Git 历史保留。

## 目录与使用范围

| 目录 | 内容 | 版本说明 |
| --- | --- | --- |
| [pcb/current](pcb/current/README.md) | 当前 V2.1 的 KiCad、EDA、预览与校验信息 | 后续 V2.1 工程更新使用固定路径 |
| [V1 归档](pcb/archive/V1_c7c59dff/README.md) | 首板 KiCad 源文件、符号库、封装库和本地模型 | 历史源文件；与冻结制造快照分别保存 |
| [bom](bom/README.md) | 原 BOM、采购表、料号列表、版本对比及交互式 BOM | CSV 为已有 V1 / V2.1 KiCad 用量记录；交互式 BOM 为 V1 快照，元器件核对已停止 |
| [冻结制造包](fabrication/JXBadge_EVT_20260909_c7c59dff/下单说明.md) | 首板 Gerber、钻孔、钢网及导出核验源快照 | 本次保持路径与全部文件字节不变 |
| [V11 外壳](enclosure/releases/V11_V2_1_20261020/README.md) | V2.1 配套打印件、装配模型与说明；当前按钮为 R2 | 2026-10-28 修正按钮发布包，杯壳及限位件不变；几何检查对应说明中的 KiCad PCB；[V6](enclosure/releases/V6_20260930/README.md)为历史外壳 |

## 采购、装配与验证

当前 EDA 中 J1 为 AFC07-S18ECC-00 / C2840708，按用户决定保留原封装；U3 保持 SC7A20TR / C5126709。BOM 和商城器件库核对已停止，旧 CSV 未在本次整理中重新核对或改料。

电池沿用用户提供的 450 mAh、42×30×4 mm，正负线直接焊接、J3 不装。具体版本和历史装配说明见 [BOM 说明](bom/README.md)、[J1 更正](../docs/J1-CONNECTOR.md)和[V1 历史记录](pcb/archive/V1_c7c59dff/README.md#电池与屏幕连接器)。

[焊接与分阶段上电检查](../焊接与分阶段上电检查_首板.md)保留其设计核对依据；[首板测试记录](../首板测试记录.md)保留实物反馈与历史失败。工程归档、文件校验及已有 CAD 检查不代替 V2.1 新板的制造、机械和实物功能验收。
