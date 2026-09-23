# CABadge c7c59dff 硬件复刻

当前固件 v1.1.0 使用原硬件基线，未改板。KiCad 10 打开 [JXBadge_V1.kicad_pro](JXBadge_V1.kicad_pro)，保持本目录结构。

## 必须一起保留的文件

- 项目、根原理图、Controls_Test / Display_Sensor / USB_Power 子页、PCB 和设计规则。
- JXBadge.kicad_sym、JXBadge.pretty、JXBadge.3dshapes 及两份 lib-table：工程使用 KIPRJMOD 相对引用，不随意移动或删减。
- [BOM](BOM-DRAFT.csv)、[采购表](LCSC_IMPORT_LIST.csv)、[交互式 BOM](bom/ibom.html)：后者下载后在浏览器打开，辅助焊接定位。
- [冻结生产包与下单说明](fabrication/JXBadge_EVT_20260909_c7c59dff/下单说明.md)：PCB ZIP 和顶层钢网 ZIP 分开使用；MANIFEST 中的文件整组保持一致。

## 采购与装配

首板换装 J1 为 FFC/FPC 0.5 mm、18P、抽屉式上接触。厂家精确型号及尺寸图仍待提供；[J1 连接器更正](../docs/J1-CONNECTOR.md) 优先于原理图/库/交互式 BOM 中的旧采购标识，不可直接将原 C2856802 当成已验证替代。

板为 50 mm 圆形、四层、名义 1.6 mm，目标叠层 JLC04161H-7628；订单工艺须由板厂确认。制造 ZIP 的正确性检查不等于机械适配、射频、充电或产品可靠性验收。

按[焊接与分阶段上电检查](../焊接与分阶段上电检查_首板.md)逐组断电检查与上电，使用板上 VBUS/3V3/GND/BAT/PWR/EN 丝印。不要带电插拔屏幕排线。

## 固件与证据

下载 [v1.1.0](https://github.com/nbtca/CABadge/releases/tag/v1.1.0)，按[安装说明](../docs/RELEASE-v1.1.0.md)烧录。当前实板结果与历史故障见[首板测试记录](../首板测试记录.md)，不把历史 CAD 检查等同于实板全部通过。

运行 `python docs/check_repository.py` 可核对制造 MANIFEST 文件字节数和 SHA-256。冻结验证目录中的原始源快照用于追溯制造包，不是另一个现行硬件版本。

[精简前硬件设计与 CAD 历史](https://github.com/nbtca/CABadge/blob/d1fe9716b383dcb6fd292d683265beacd92fde64/hardware/README.md)
