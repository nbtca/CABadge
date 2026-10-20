# CABadge · 计协电子吧唧

CABadge 是为计算机协会设计的可自定义电子徽章。项目希望将传统吧唧的身份表达与电子屏幕的可变内容结合，让佩戴者通过壁纸、图案和协会名片展示个人喜好与社团归属。

它以日常佩戴和内容展示为核心：平时展示喜欢的画面，需要时通过触摸操作切换内容，通过无线连接或电脑工具更新壁纸、管理设备。界面尽量简洁，让展示内容本身成为主角。

这里收录硬件设计、固件源码、配套电脑工具和实板测试记录，供复刻、改进和协作开发使用。

**当前正式版本：v1.2.0 / 内部固件 7.9.16-eaf-transition。** 包含统一缓存位图转场、Direct Compositor 和内存优化。已完成编译、烧录及基本功能检查；长期稳定性与完整人工验收仍在完善。

**最新测试版本：[v1.3.0-rc.1 / 7.9.23-timetiles-fix](https://github.com/nbtca/CABadge/releases/tag/v1.3.0-rc.1)。** 新增 TimeTiles、FluidBox、标准 BLE 心率和动作控制更新；已确认烧录与启动，最新手势、画面及长期稳定性仍待实屏验收。本次只发布二进制和调试附件，开发源码尚未并入 `main`。

**当前最新板卡： [V2.1 GEK100（2026-10-20）](hardware/releases/V2_1_GEK100_20261020/README.md)。** 已同时上传完整的 [KiCad 工程](hardware/releases/V2_1_GEK100_20261020/kicad/CABadge_V2_1.kicad_pro)和[嘉立创EDA专业版工程](hardware/releases/V2_1_GEK100_20261020/easyeda/CABadge_V2_1_GEK100.epro2)，包括 GEK100 按键电源、R40 连接器抽屉避让，以及当前优化后的 EDA 原理图。EDA 已更新为 2026-10-20 16:04 的人工优化原理图导出，PCB 设计记录与上一快照一致；新版原理图 DRC 和跨格式网表待复核，新增电源与机械适配仍待实板验证。固件与板卡修订分别编号。

**当前正式外壳：[V11 一体杯壳，配套 V2.1 GEK100](hardware/enclosure/releases/V11_V2_1_20261020/README.md)。** 提供三个打印件、STEP / SolidWorks 装配模型、[装配图 PDF](hardware/enclosure/releases/V11_V2_1_20261020/images/assembly-guide.pdf)及[装配说明](hardware/enclosure/releases/V11_V2_1_20261020/ASSEMBLY.md)。PCB 采用板边先入、壳内转平，屏幕最后落座；正式配套发布不改写尚待完成的实物装配验证。

[下载 v1.2.0](https://github.com/nbtca/CABadge/releases/tag/v1.2.0) · [发布与升级说明](docs/RELEASE-v1.2.0.md) · [已知问题](docs/KNOWN_ISSUES.md) · [实板测试记录](首板测试记录.md)

当前 `main` 源码版本为 **7.9.16-eaf-transition**，包含 BlueMap 下载/缓存改进、统一壁纸库、EAF Direct 动态壁纸与逐资源 timing、冻结当前帧的页面转场，以及 [动态壁纸转换说明](docs/EAF_TIMING.md)。上方 v1.2.0 下载链接对应同版固件；最新源码构建方法见 [BUILD](docs/BUILD.md)，验证范围见 [HANDOFF](firmware-v7/HANDOFF.md)。

## 可以做什么

- **展示与切换壁纸**：圆形触摸屏展示静态图片，支持浏览、选择和保存上传的壁纸。
- **展示协会名片**：内置计协风格名片页面。
- **连接与管理设备**：Wi-Fi、BLE 连接管理，通过 USB 或本地 HTTP 上传壁纸。
- **基础交互**：触摸操作、亮度设置、息屏后摇晃唤醒，以及可选 FPS 显示。
- **小应用**：Grok 表情、MC 二维地图、奶蛙矿工；使用说明见 [应用](firmware-v7/APPS.md)。
- **电脑端调试**：Windows 原生 NBTCA Badge TOOL 提供固件安装与设备联调入口。

<img width="3072" height="4096" alt="44ebd1ad073642b58210a89b23977e9f" src="https://github.com/user-attachments/assets/51c7bce0-58fa-4813-8621-cffee2827b9e" />
<img width="3072" height="4096" alt="2ba59d724997cc1bf2ca568b1a78c989" src="https://github.com/user-attachments/assets/a6ad94ad-bac8-474c-ab1c-10988e7616c3" />
<img width="3072" height="4096" alt="10e58efb694e944809ac0757480d7f42" src="https://github.com/user-attachments/assets/2f7458d9-f58d-4769-8252-f9dc3a8ce983" />


当前源码支持在电脑上将 GIF/视频转换为 EAF 动态壁纸，设备不直接播放 GIF/视频。BLE 传图和 OTA 尚未提供。完整的异常场景和长期稳定性测试仍在完善中。

## 硬件概览

|部分|当前方案|
|---|---|
|主控|ESP32-S3R8，8 MB Octal PSRAM|
|外部 Flash|W25Q128JVSIQ，16 MB|
|显示与输入|360×360 圆形 ST77916 屏幕、触摸输入|
|运动传感器|SC7A20|
|主电源|SGM62117 升降压至 3.3 V|
|充电|TP4056-MS，配合外围供电分配与充电使能电路|
|PCB|直径 50 mm、四层板、名义板厚 1.6 mm|

最新板卡设计为 **V2.1 GEK100**，源文件与检查结果见[最新版板卡目录](hardware/releases/V2_1_GEK100_20261020/README.md)。已完成首板实测的硬件基线仍为 **JXBadge_EVT_20260909_c7c59dff**；[BOM](hardware/BOM-DRAFT.csv) 与[采购表](hardware/LCSC_IMPORT_LIST.csv)已列出两版用量及新旧独有器件，详见[用料差异](hardware/bom/README.md)。交互式 BOM 与旧冻结生产包仍属于首板资料，V2.1 新增电源与机构仍待实板验收。

## 如何开始复刻

复刻需要自行采购、焊接和调试。板上包含 QFN 主控和细间距 FPC 连接器，建议具备贴片焊接与万用表排查经验，并准备放大观察工具及合适的焊接设备。

**流程：下载配套资料 → 核对采购 → PCB 打样 → 分阶段焊接与上电 → 烧录 → 功能检查。** 已有装配完成的同版硬件，可以从第 5 步开始。

### 1. 下载固件与配套硬件

固件与板卡分别选择版本：最新测试固件为 [v1.3.0-rc.1 / 7.9.23](https://github.com/nbtca/CABadge/releases/tag/v1.3.0-rc.1)，正式固件仍为 [v1.2.0 / 7.9.16](https://github.com/nbtca/CABadge/releases/tag/v1.2.0)；当前最新板卡为 [V2.1 GEK100](hardware/releases/V2_1_GEK100_20261020/README.md)。测试版的新功能和 V2.1 新增电源、机构适配仍待实物验收，固件版本与 PCB 修订独立编号。

|文件|用途|
|---|---|
|`CABadge-v1.3.0-rc.1-firmware.zip`|最新测试版三个 BIN、安装说明、验证记录及校验值|
|`CABadge-v1.2.0-firmware.zip`|正式版三个 BIN、安装说明及校验值|
|所选 Release 的 `debug.zip`|与该版固件匹配的 ELF、有效构建配置，仅排查问题时需要|
|[V2.1 KiCad 工程](hardware/releases/V2_1_GEK100_20261020/kicad/CABadge_V2_1.kicad_pro)|KiCad 10 项目入口，需连同该目录的原理图、PCB、库和模型一起下载|
|[V2.1 嘉立创EDA工程](hardware/releases/V2_1_GEK100_20261020/easyeda/CABadge_V2_1_GEK100.epro2)|专业版 V4 原生工程，包含人工优化后的原理图和 PCB|
|[V11 正式配套外壳](hardware/enclosure/releases/V11_V2_1_20261020/README.md)|三个打印 STL、装配模型、爆炸图及剖面图，与 V2.1 PCB 配套|
|[v1.0 的硬件附件](https://github.com/nbtca/CABadge/releases/tag/v1.0)|仅用于复刻旧首板 c7c59dff，不作为 V2.1 生产包|

V2.1 KiCad 资料请从 `main` 分支的 **Code → Download ZIP** 下载整套仓库并保留目录结构；EDA 用户可下载上述 `.epro2`，在专业版“导入专业版”入口导入。两种格式不自动同步，最新 EDA 原理图重新优化过（kicad未变）方便阅读，具体范围见板卡说明。

首次点亮直接使用所选 Release 的固件附件，无需先搭建编译环境，下载后核对 `SHA256SUMS.txt`。**v1.3.0-rc.1 自动生成的 `Source code (zip)` 不是 7.9.23 的对应源码；公开 `main` 固件源码仍为 7.9.16，最新板卡资料以 `main` 的上述目录为准。**

### 2. 核对器件与装配

先选定板卡版本，再从 [BOM](hardware/BOM-DRAFT.csv) 与[采购料号表](hardware/LCSC_IMPORT_LIST.csv)筛选对应版本数量大于 0 的器件；两表均标明共用、旧版独有、新版独有与用量变化，采购表原“数量”列默认表示 V2.1。新版新增 U7 GEK100_13、C32，SW1 由拨动开关改成轻触按键，R40 仅移位；具体型号与每板数量见[两版用料差异](hardware/bom/README.md)。[交互式 BOM](hardware/bom/ibom.html)仍是旧首板快照，下载后用浏览器打开，不用于核对新版完整用料。

查看当前电路时，用 KiCad 10 打开 [CABadge_V2_1.kicad_pro](hardware/releases/V2_1_GEK100_20261020/kicad/CABadge_V2_1.kicad_pro)，或导入对应 EDA 工程。只有复刻旧首板时才使用 [JXBadge_V1.kicad_pro](hardware/JXBadge_V1.kicad_pro)及其整套资料。

采购时重点核对屏幕完整型号、排线引脚定义、FPC 座的接触方向与封装，不能仅凭“360×360”或“18 针”判断兼容。电池也需核对极性、保护、尺寸与安装空间。BOM 文件保留 `DRAFT` 名称，采购前应结合原理图和 [首板测试记录](首板测试记录.md) 复核，不能当作已验证的即买即装套件清单。

### 3. PCB 打样

复刻 **V2.1** 时，从实际选用的 V2.1 PCB 工程导出四层 Gerber、板框及 PTH/NPTH 钻孔，检查后打包交给板厂。当前公开的 V2.1 目录提供设计源文件与验证记录，尚未附配套制造 ZIP；不要用 v1.0 首板包替代。

按[最新版板卡说明](hardware/releases/V2_1_GEK100_20261020/README.md)核对四层、名义 1.6 mm 板厚和直径 50 mm 圆板。项目实际试制沿用华秋普通打样：10 片、黑油白字、有铅喷锡、过孔盖油；叠层、孔工艺及局部间距仍需板厂按实际 Gerber 审核。需要钢网时，从同一版 PCB 的锡膏层另行导出。

只有复刻 **旧首板 c7c59dff** 时，才使用 v1.0 硬件附件中的 `JXBadge_EVT_20260909_c7c59dff_PCB.zip`；[原下单说明](hardware/fabrication/JXBadge_EVT_20260909_c7c59dff/下单说明.md)保留历史参考含义。交给板厂的是 PCB 制造 ZIP，不是仓库源码 ZIP、整套资料外层 ZIP 或钢网包。

### 4. 分阶段焊接与首次上电

外壳使用 [V11 正式配套包](hardware/enclosure/releases/V11_V2_1_20261020/README.md)，按[装配图与顺序](hardware/enclosure/releases/V11_V2_1_20261020/ASSEMBLY.md)准备杯壳、按帽和隐藏限位件，电路调试完成后再合屏固定。

按 [焊接与分阶段上电检查](焊接与分阶段上电检查_首板.md) 分组完成：USB 输入 → 3.3 V 电源 → 主控与 USB 下载 → 屏幕与触摸 → 电池与充电。

该检查表来自首板；V2.1 还需核对 GEK100 按键电源、USB/电池供电切换与充电联锁，不能用首板成功开机代替新增电路验收。

首次排查可以只用 USB 供电、不接电池。每增加一组先断电检查焊接与短路，再上电测量；不要带电插拔屏幕排线。确认主电源和主控正常后再接入屏幕。

首板换装的 J1 规格为 **FFC/FPC 0.5 mm、18P、抽屉式上接触**，换座后已成功显示。当前 [BOM](hardware/BOM-DRAFT.csv) 与 [采购表](hardware/LCSC_IMPORT_LIST.csv) 已按截图更正；厂家型号和尺寸图仍未提供，采购前须核对封装与接触方向。**v1.0 旧源码、原理图和交互式 BOM 中仍有原 J1 条目，采购请以 [J1 连接器更正](docs/J1-CONNECTOR.md) 为准。**

### 5. 烧录所选版本固件

解压所选固件包，按包内 `README.md` 操作；[安装与构建说明](docs/BUILD.md)对应公开源码及 v1.2.0 正式版。烧录前核对校验值和硬件版本，使用支持数据传输的 USB 线，并释放被其他程序占用的串口。

|文件|烧录地址|
|---|---|
|`bootloader.bin`|`0x0`|
|`partitions.bin`|`0x8000`|
|`firmware.bin`|`0x10000`|

安装 Python 与 esptool 5.x，在解压后的 BIN 目录执行命令，`COMx` 换为实际串口。已使用相同分区布局的 7.9.x 设备只更新应用：

```powershell
python -m esptool --chip esp32s3 --port COMx --baud 115200 write-flash 0x10000 firmware.bin
```

首次安装或从旧 v1.0 分区布局升级时，写入同一个固件包的三个 BIN：

```powershell
python -m esptool --chip esp32s3 --port COMx --baud 115200 write-flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

当前壁纸分区从 `0x710000` 开始、大小 `0x8f0000`；旧布局升级前保留壁纸原文件及需要的设备备份。v7 工作台仍默认安装 v1.2.0，将正式包解压到 `outputs/cabadge-v7.9.16-eaf-transition/`；安装测试版使用上述 esptool 命令，具体依赖见所选 Release 的说明。

烧录完成后 RESET，确认启动版本与所选包一致：测试版为 **7.9.23-timetiles-fix**，正式版为 **7.9.16-eaf-transition**。不要为了普通升级额外执行全片擦除，以免清掉已有设置和壁纸。

### 6. 检查自己的板子

依次检查开机显示、触摸与翻页、壁纸切换、Wi-Fi/BLE 基本连接，再检查上传壁纸及重启后的保留情况。遇到问题时记录固件版本、触发步骤和现象，附上照片或串口日志，方便复现与修复。

使用 7.9.23 测试版时，还需检查 TimeTiles 手势/主题/计时提醒、FluidBox 运动输入和 BLE 心率连接；操作及已知限制见 [v1.3.0-rc.1 说明](https://github.com/nbtca/CABadge/releases/tag/v1.3.0-rc.1)。已有烧录与软件检查不代替这些新功能或 V2.1 整机的实物验收。

当前背光已使用平方曲线调节实际 PWM，旧 5% 测试上限已取消；CHG_ALLOW 仍保持 LOW。成功开机不代表充电、电流温升或长期运行已完整验收。显示 TE 未连接，动态画面仍需观察是否撕裂；具体待办见 [已知问题](docs/KNOWN_ISSUES.md)。

## 继续开发

完成首次点亮后，再按 [安装与构建说明](docs/BUILD.md) 恢复开发环境。当前工程使用 LVGL 9.4.0；桌面预览基于 SDL2，板端使用 ESP-IDF / PlatformIO。构建脚本仍有 Windows 和 F 盘路径限制，尚未整理成跨平台的一键构建流程。

|目录|内容|
|---|---|
|`firmware-v7/`|当前固件、UI、工作台、协议与测试|
|`hardware/`|KiCad 工程、项目库、BOM 与生产文件|
|`tools/flasher/`|共用烧录工具|
|`docs/`|安装、构建、发布与已知问题说明|

历史实验固件、原始日志和本地备份没有全部随仓库上传，旧文档中的部分研究链接仅保留为历史出处。`v1.0`、`v1.2.0` 标签固定各次发布快照；主分支继续更新。文档分类见 [仓库索引](docs/INDEX.md)。

## 许可

本项目原创内容采用 [MIT License](LICENSE)。第三方字体、库、模型和素材仍遵循各自许可，不因本项目采用 MIT 而改变；字体许可见 [FONT-LICENSE.txt](firmware-v7/ui/FONT-LICENSE.txt)。

## 致谢

感谢 **嘉立创** 和 **华秋** 提供的免费打板支持，也感谢组织成员 **m1ngsama** 提供的 Pro 账号。这些支持为本项目的开发提供了极大的帮助。
