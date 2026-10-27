# CABadge 当前板卡 · V2.1 GEK100 / 星火计划

V2.1 同时提供 KiCad 和嘉立创EDA 两种格式。**本次将 V2.1 的转换版 EDA 替换为 2026-10-27 完全在 EDA 中重画 PCB 的版本，KiCad 原文件保持不变。** 五张原理图直接复用已有 EDA 原理图；旧的转换版 EDA 已删除。

| 格式 | 打开入口 | 当前内容 |
| --- | --- | --- |
| KiCad 10 | [CABadge_V2_1.kicad_pro](kicad/CABadge_V2_1.kicad_pro) | 保留 2026-10-20 的 120 个源文件及依赖：PCB、五张原理图、规则、项目库和本地模型 |
| 嘉立创EDA专业版 V4 | [CABadge_V2_1_GEK100.epro2](easyeda/CABadge_V2_1_GEK100.epro2) | 2026-10-27 从“CABadge-基于 ESP32-S3 的电子吧唧-星火计划”保存导出的原生重画工程 |
| EDA 归档摘要 | [easyeda-summary.json](verification/easyeda-summary.json) | 六文档保存、原生包 CRC、PCB 记录统计、已有 DRC 时间与验证范围 |

## 打开方式

KiCad：下载仓库并保留 `kicad/` 内全部文件，打开 `CABadge_V2_1.kicad_pro`。项目库和本地模型使用 `${KIPRJMOD}` 相对路径；标准库依赖仍需 KiCad 10 提供。

嘉立创EDA：下载 `.epro2`，通过专业版“导入专业版”入口导入。工程由桌面版 4.1.78 导出，保留内嵌的器件、符号、封装和图像；本次目录整理未重新导入或修改工程内部设计。

这两种格式属于同一 V2.1 板卡版本，各自保留其源文件与验证依据，不自动同步。KiCad PCB 仍为 `9d094d57cf92abfb1791aaf05bcdb3205478dee128614ef87b8f4fef2c3da4ea`；EDA 的 PCB 图元、器件属性和丝印以今天的原生工程为准。

## EDA 原生重画内容

- 复用 CABadge_V2_1、Controls_Test、Display_Sensor、Power_Button、USB_Power 五页原理图。
- PCB1 为直径 50 mm、四层，107 个器件、1,858 段走线、237 个过孔；按确认的布局恢复原版连线与正面丝印，底部为星火计划 Logo。
- 导出前五页原理图及 PCB 均已保存，最后 PCB 保存时间为 2026-10-27 15:23（北京时间）。本次整理仅移动、改名和更新说明，工程文件字节不变。
- J1 为 AFC07-S18ECC-00 / C2840708，按用户决定保留现有封装与两侧固定焊盘；U3 保持 SC7A20TR / C5126709。
- BOM / 商城元器件库核对已按用户要求停止，尚未全部完成，U7 商城料号未明确。[已有采购资料](../../bom/README.md)保留其原始版本范围，本次未改料或更新用量。

## 校验与验证范围

原生压缩包 CRC 通过，包含五张原理图和一个 PCB；EDA 文件 SHA-256 为：

```text
9c335971ff7f0c77a8030949b27930cb43197a29638a4cce736567127c67a8a5
```

EDA 最近保存的 DRC 记录为 2026-10-27 14:38:37（北京时间），0 项违规；目录整理未重跑电气检查。KiCad 保留原有 [DRC](verification/kicad-drc.json)和 [ERC](verification/kicad-erc.json)记录，原源文件与 2026-10-20 版本按字节相同。

[MANIFEST.json](MANIFEST.json)与 [SHA256SUMS.txt](SHA256SUMS.txt)覆盖本目录文件。归档完整性和已有 CAD 检查不代表新板制造、GEK100 时序、供电切换、充电联锁、机械装配或实物功能验收。

## EDA 工程内预览

以下图片直接来自今天导出的原生工程；PCB 为图层叠加预览。

![PCB 原生预览](easyeda/pcb-preview.webp)

![主原理图预览](easyeda/schematic-preview.webp)

[硬件总入口](../../README.md) · [V1 首板 KiCad 归档](../archive/V1_c7c59dff/README.md) · [V11 外壳](../../enclosure/releases/V11_V2_1_20261020/README.md)
