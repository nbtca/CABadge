# CABadge V11 外壳 · 配套 V2.1 GEK100

当前采用 **Cup + Button 两件打印方案**。2026-10-29 按实际装配更新模型、装配图和清单。Cup 与现有 R2 Button 的几何及打印文件保持原样。

配套板卡：[V2.1 GEK100 双格式工程](../../../pcb/current/README.md)。几何参考使用原 KiCad PCB，SHA-256 为 `9d094d57cf92abfb1791aaf05bcdb3205478dee128614ef87b8f4fef2c3da4ea`；EDA 与 KiCad 不自动同步。

## 下载与打印

| 文件 | 用途 |
| --- | --- |
| [V11_Cup.stl](print/V11_Cup.stl) | 一体杯壳，打印 1 件 |
| [V11_Button_R2.stl](print/V11_Button_R2.stl) | R2 侧面按帽，打印 1 件 |
| [STEP 模型包](https://github.com/nbtca/CABadge/releases/download/enclosure-v11-cup-button-20261029/CABadge_V11_V2_1-STEP.zip) | 七部件参考装配，以及 Cup、Button_R2 两个独立 STEP，共 3 个文件 |
| [SolidWorks 模型包](https://github.com/nbtca/CABadge/releases/download/enclosure-v11-cup-button-20261029/CABadge_V11_V2_1-SolidWorks.zip) | SLDASM 与全部 7 个 SLDPRT，共 8 个文件；解压后保持同目录 |
| [两页装配图 PDF](images/assembly-guide.pdf) | 爆炸图、整机半剖及按帽与开关位置示意 |
| [装配说明](ASSEMBLY.md) | 零件清单、装入顺序与合屏前检查 |
| [SHA256SUMS.txt](SHA256SUMS.txt) | 包内文件校验值 |

**打印 Cup 和 Button_R2，各 1 件。** STL 单位为 mm，切片保持 100%。屏幕、电池、PCB、绝缘片与底部粘接层为装配参考，不是打印件。

大型 CAD 包首选从[两件打印方案 Release](https://github.com/nbtca/CABadge/releases/tag/enclosure-v11-cup-button-20261029)下载；[仓库内模型包](cad/README.md)与其对应。

## 当前按钮 R2

当前按帽沿用已发布 R2：按面 9.6 × 2.6 mm，杯壳原孔 10.4 × 3.4 mm，按面单边名义间隙 0.4 mm。CAD 参考位置的曲面名义外凸为 1.6 mm，属于设计尺寸。

按帽与开关的触发、释放和位置稳定性按实际装配检查。

## 装配图

![Cup 与 Button 两件打印方案爆炸图](images/assembly-exploded.png)

![装配半剖及按帽与开关位置](images/assembly-details.png)

图中只有 01～07 七个部件，打印件为 06 Cup 与 07 Button_R2。图片基于同一组 STEP 几何与参考坐标，爆炸位移仅用于辨认层次。PCB 采用板边先入、再在壳内转平的装入方式，图中轴向展开不表示整板平放直降。

可编辑标注源：[爆炸图 SVG](images/assembly-exploded.svg)、[剖面图 SVG](images/assembly-details.svg)。STEP 装配包含 PCB、屏幕和电池参考；SolidWorks 装配为固定位置参考，不是驱动运动装配。

## 配套结构

- 杯壳名义外径 62 mm，含屏总厚 13.2 mm；Cup 与 R2 Button 几何未修改。
- PCB 由底部绝缘粘接层固定，安装总厚 0.3 mm，含两面胶层。
- 电池参考为 450 mAh、42×30×4 mm，电池线直接焊接，J3 不装；电池与板上器件之间有绝缘片。
- 屏幕台阶名义胶层空间为 0.2 mm；屏幕最后固定，电池不能承受固定预压。
- 按帽从杯壳内侧装入，不能粘到板上开关。

## 发布与验证状态

**装配方式来自用户反馈；该反馈仅确认使用的打印件，不等于所有装配专项和整机功能已验收。**

当前核对记录见 [Cup + Button 装配记录](verification/assembly-cup-button.json)和[发布摘要](verification/publication-summary.json)。Cup / R2 Button 的 STL、STEP 与原生零件保持既有指纹，SolidWorks 重新打开后为 7 个完整解析的固定部件。

R2 按帽几何与网格记录见[按钮检查](verification/button-r2.json)和[网格检查](verification/button-r2-mesh.json)。

`source-*.json` 保留 2026-10-20 历史检查与失败状态。当前按钮的摩擦、释放、位置稳定性，胶粘强度、FPC 弯折及整机长稳仍按实际结果逐项记录；原理图、PCB 与固件保持原版本。实物反馈继续维护在[首板测试记录](../../../../首板测试记录.md)。

本项目原创外壳设计采用仓库 [MIT License](../../../../LICENSE)；第三方器件模型与资料保留各自权利归属。
