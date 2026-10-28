# 复刻与安装

本文提供采购、打样、装配与烧录的详细步骤。开始前先查看[项目版本表](../README.md#版本与资料对应)，分别选择固件、PCB 和外壳；当前 V2.1 设计、V1 首板记录与各次固件附件各有适用范围。

## 复刻流程


复刻需要自行采购、焊接和调试。板上包含 QFN 主控和细间距 FPC 连接器，建议具备贴片焊接与万用表排查经验，并准备放大观察工具及合适的焊接设备。

**流程：下载配套资料 → 核对采购 → PCB 打样 → 分阶段焊接与上电 → 烧录 → 功能检查。** 已有装配完成的同版硬件，可以从第 5 步开始。

### 1. 下载固件与配套硬件

固件与板卡分别选择版本：最新测试固件为 [v1.3.0-rc.1 / 7.9.23](https://github.com/nbtca/CABadge/releases/tag/v1.3.0-rc.1)，正式固件仍为 [v1.2.0 / 7.9.16](https://github.com/nbtca/CABadge/releases/tag/v1.2.0)；当前板卡为 [V2.1 GEK100 / 星火计划](../hardware/pcb/current/README.md)。测试版的新功能和 V2.1 新增电源、机构适配仍待实物验收，固件版本与 PCB 修订独立编号。

|文件|用途|
|---|---|
|`CABadge-v1.3.0-rc.1-firmware.zip`|最新测试版三个 BIN、安装说明、验证记录及校验值|
|`CABadge-v1.2.0-firmware.zip`|正式版三个 BIN、安装说明及校验值|
|所选 Release 的 `debug.zip`|与该版固件匹配的 ELF、有效构建配置，仅排查问题时需要|
|[V2.1 KiCad 工程](../hardware/pcb/current/kicad/CABadge_V2_1.kicad_pro)|当前 V2.1 的 KiCad 格式，保留 2026-10-20 原文件，需连同库和模型下载|
|[V2.1 嘉立创EDA原生重画工程](../hardware/pcb/current/easyeda/CABadge_V2_1_GEK100.epro2)|2026-10-27 最新专业版 V4 工程，含五张复用原理图及原生重画 PCB|
|[V11 正式配套外壳](../hardware/enclosure/releases/V11_V2_1_20261020/README.md)|Cup 与 Button 两个打印 STL、装配模型、爆炸图及剖面图，与 V2.1 PCB 配套|
|[v1.0 的硬件附件](https://github.com/nbtca/CABadge/releases/tag/v1.0)|仅用于复刻旧首板 c7c59dff，不作为 V2.1 生产包|

V2.1 同时提供两种格式：EDA 用户下载上述 `.epro2`，在专业版“导入专业版”入口导入；KiCad 用户从 `main` 分支的 **Code → Download ZIP** 下载工程并保留 `hardware/pcb/current/kicad/` 的目录结构。EDA 已换为 2026-10-27 原生重画版，KiCad 原文件保持，两种格式不自动同步；详见[当前版本说明](../hardware/pcb/current/README.md)。

首次点亮直接使用所选 Release 的固件附件，无需先搭建编译环境，下载后核对 `SHA256SUMS.txt`。**自行编译 7.9.23 请下载 `main` 分支源码，并按 [BUILD](BUILD.md)恢复依赖。v1.3.0-rc.1 标签自动生成的 `Source code (zip)` 仍是该标签发布时的旧源码快照；当前公开源码从 `main` 获取，既有标签保留发布时的快照。**

### 2. 核对器件与装配

先选定板卡版本，再从 [BOM](../hardware/bom/BOM-DRAFT.csv) 与[采购料号表](../hardware/bom/LCSC_IMPORT_LIST.csv)筛选对应版本数量大于 0 的器件；两表均标明共用、旧版独有、新版独有与用量变化，采购表原“数量”列默认表示 V2.1。新版新增 U7 GEK100_13、C32，SW1 由拨动开关改成轻触按键，R40 仅移位；具体型号与每板数量见[两版用料差异](../hardware/bom/README.md)。[交互式 BOM](../hardware/bom/ibom.html)仍是旧首板快照，下载后用浏览器打开，不用于核对新版完整用料。

查看 V2.1 设计时，选择[当前 EDA 工程](../hardware/pcb/current/easyeda/CABadge_V2_1_GEK100.epro2)或 [KiCad 工程](../hardware/pcb/current/kicad/CABadge_V2_1.kicad_pro)。只有复刻 V1 首板时才使用[归档的 JXBadge_V1.kicad_pro](../hardware/pcb/archive/V1_c7c59dff/JXBadge_V1.kicad_pro)及其整套资料。

采购时重点核对屏幕完整型号、排线引脚定义、FPC 座的接触方向与封装，不能仅凭“360×360”或“18 针”判断兼容。电池也需核对极性、保护、尺寸与安装空间。BOM 文件保留 `DRAFT` 名称，采购前应结合原理图和 [首板测试记录](../首板测试记录.md) 复核，不能当作已验证的即买即装套件清单。

### 3. PCB 打样

复刻 **V2.1** 时，从实际选用的 V2.1 PCB 工程导出四层 Gerber、板框及 PTH/NPTH 钻孔，检查后打包交给板厂。当前公开的 V2.1 目录提供设计源文件与验证记录，尚未附配套制造 ZIP；不要用 v1.0 首板包替代。

按[最新版板卡说明](../hardware/pcb/current/README.md)核对四层、名义 1.6 mm 板厚和直径 50 mm 圆板。项目实际试制沿用华秋普通打样：10 片、黑油白字、有铅喷锡、过孔盖油；叠层、孔工艺及局部间距仍需板厂按实际 Gerber 审核。需要钢网时，从同一版 PCB 的锡膏层另行导出。

只有复刻 **旧首板 c7c59dff** 时，才使用 v1.0 硬件附件中的 `JXBadge_EVT_20260909_c7c59dff_PCB.zip`；[原下单说明](../hardware/fabrication/JXBadge_EVT_20260909_c7c59dff/下单说明.md)保留历史参考含义。交给板厂的是 PCB 制造 ZIP，不是仓库源码 ZIP、整套资料外层 ZIP 或钢网包。

### 4. 分阶段焊接与首次上电

外壳使用 [V11 正式配套包](../hardware/enclosure/releases/V11_V2_1_20261020/README.md)，按[装配图与顺序](../hardware/enclosure/releases/V11_V2_1_20261020/ASSEMBLY.md)准备杯壳与按帽，电路调试完成后再合屏固定。

按 [焊接与分阶段上电检查](../焊接与分阶段上电检查_首板.md) 分组完成：USB 输入 → 3.3 V 电源 → 主控与 USB 下载 → 屏幕与触摸 → 电池与充电。

该检查表来自首板；V2.1 还需核对 GEK100 按键电源、USB/电池供电切换与充电联锁，不能用首板成功开机代替新增电路验收。

首次排查可以只用 USB 供电、不接电池。每增加一组先断电检查焊接与短路，再上电测量；不要带电插拔屏幕排线。确认主电源和主控正常后再接入屏幕。

首板换装的 J1 规格为 **FFC/FPC 0.5 mm、18P、抽屉式上接触**，换座后已成功显示。当前 V2.1 EDA 已标记为 **AFC07-S18ECC-00 / C2840708**，保留现有封装；已有 BOM CSV 保留历史数据，采购前须核对工程字段与具体商品。首板旧采购记录见 [J1 历史更正说明](J1-CONNECTOR.md)，当前工程字段与范围见 [V2.1 说明](../hardware/pcb/current/README.md)。

### 5. 烧录所选版本固件

解压所选固件包，按包内 `README.md` 操作；[安装与构建说明](BUILD.md)对应公开源码及 v1.2.0 正式版。烧录前核对校验值和硬件版本，使用支持数据传输的 USB 线，并释放被其他程序占用的串口。

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

当前背光已使用平方曲线调节实际 PWM，旧 5% 测试上限已取消；CHG_ALLOW 仍保持 LOW。成功开机不代表充电、电流温升或长期运行已完整验收。显示 TE 未连接，动态画面仍需观察是否撕裂；具体待办见 [已知问题](KNOWN_ISSUES.md)。
