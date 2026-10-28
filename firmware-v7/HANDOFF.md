## 2026-10-29 仓库维护整理（公开源码仍为 7.9.23-timetiles-fix）

版本定义集中到 `version.json`；板端构建按工作树和变体隔离暂存与输出，并记录源码提交、脏状态和输入指纹。环境由根目录 `tools/bootstrap.ps1` 安装，宿主检查迁至 `tests/host/`。开发、构建和发布入口见 [docs/INDEX](../docs/INDEX.md)。

这次整理保留公开固件功能、正式 v1.2.0 和既有测试附件；原工作区的 7.9.25 / m1ngsama 未发布开发工作保持独立。编译和离线检查不构成新的实板验收；没有连接、烧录设备，也没有改写首板测试记录。

## 2026-10-21 当前公开源码：7.9.23-timetiles-fix

按用户要求将本地最新开发源码同步到 main，包含 TimeTiles、FluidBox、标准 BLE 心率、SC7A20 动作、所需资源、测试与操作文档。正式安装包和 Workbench 默认仍为 v1.2.0 / 7.9.16；v1.3.0-rc.1 二进制附件及既有标签不变。该标签的自动源码 ZIP 保留原快照，最新版源码从 main 下载。

本轮从待提交文件导出独立源码副本，并保留旧构建暂存后重新编译。PowerShell 7 构建与导出通过，内部版本 7.9.23-timetiles-fix，BIN 3983872 B；54 份构建输入指纹、有效 sdkconfig 和分区表与既有 7.9.23 发布记录一致。不承诺字节级复现旧 BIN，也没有替换 Release 附件。

20 个固件/工作台宿主检查脚本通过；仓库回归 7 项、USB framing 和地图监测工具离线自检通过。干净副本暴露了 TimeTiles effects 日志目录缺失及 raster 依赖未跟踪参考副本的问题，现已创建日志父目录并以原始提取 SHA-256 固定资源校验，保留全部像素/越界检查。首次失败与复测证据保存于本地 outputs/CABadge_Source_Sync_20261021/，未覆盖历史发布或测试产物。

本轮没有连接设备、烧录、自动切页或复测画面。既有 R104 的手势/主题/摇晃、异常心率重连、撕裂及长稳待测项继续保留；下方历史条目的“未提交/推送”仅表示当时状态。原理图、PCB、外壳及未纳入本次源码同步的本地工作保持不变。

## 2026-10-15 当前开发实板：7.9.23-timetiles-fix（R104）

用户实屏反馈双击只有霓紫/白色且会回白，三击和摇晃无效。已在旧源复现：双击只翻布尔、不换主题；数字第二击先启动动画吞第三击，标题热区/微移过严；分钟换字清空点击，方向判断未保护点击链；摇晃事件未派发到TimeTiles。旧失败和原始输入保留，修订生产源通过同一复现。

当前数字双击轮换并保存六主题，第二次释放后等待420ms区分第三击；数字/扩大标题区域三击进入全高代码雨，不变更主题。分钟变化、场景取消和方向检测保留/保护点击状态，不再在退出时回白。共享SC7A20摇晃事件仅驱动可见时钟的TIME TILES FLY适配飞散场景，弹窗/提醒/睡眠/转场/已有交互不触发；全局传感器/唤醒阈值不改。旧私有NVS布局/版本兼容，没有新任务、LCD或DMA分支。额外不同尺寸粒子、整点/日期和完整Demo仍有原版差距。

生产effects/integration/motion检查通过：六主题颜色和保存、一致的双三击/四旋转/微移/分钟/方向保护，实际摇晃服务派发和状态保护；是宿主代码证据。构建73.53秒、RAM49868B、Flash3983207B、BIN3983632B，SHA256 `27ce117c2d49d86768d0a5ac24f4147d3636aa6792ea40eb6ff17976c1abc7e7`，4产物/54暂存指纹匹配。首次宿主assert编译失败和验证脚本遗漏lodepng路径的保护停止均保留，后者发生在连接/擦写前。

COM3仅写0x10000，32.5秒/Hash of data verified，USB启动版本7.9.23/store_error=0。壁纸选择/库项/CRC/类型及亮度56/减少动态保持，时区480；两计时均完成。主题读前5/读后1，两读间用户操作及最后保存时刻未记录，不据此判实体主题持久化通过或故障。串口释放，只做烧录与只读确认；沿用用户手测安排，没有自动切页/重置/闹钟/触摸测试。新版实屏颜色、双三击/摇晃/方向、校时/提醒、性能/撕裂/长稳仍待用户。

主记录R104；证据outputs/timetiles-gestures-20261015/，发布outputs/cabadge-v7.9.23-timetiles-fix/。hardware-verification.json独立记录安装，构建verification.json保持PENDING；正式v1.1.0/工作台默认和旧产物保留，未提交/推送。操作入口和版本差异见TIMETILES.md。

## 2026-10-15 历史开发实板：7.9.22-timetiles-ui（R103）

用户照片指出页面太乱、时间太小。时钟排布210→330px，11×13步距/9px字块；小标题、无边框返回/更多，日期点按校准，四模式和外观入口合并更多，倒计时字号放大，特效独占画布/代码雨全高。1,440时钟柔光圆屏边界及菜单/旋转/场景恢复代码检查通过。构建74.42秒，BIN3983040B、SHA256 `c55e69aa3031b24b9e9bede4443c774f20dc37968061ad06ed0f1785fae06eb7`；应用区烧录32.6秒/hash与启动版本确认、壁纸及亮度保持。随后用户颜色/三击/摇晃失败反馈保留并由R104继续处理，不计视觉/功能通过。原证据outputs/timetiles-layout-20261015/。

## 2026-10-15 历史开发实板：7.9.21-timetiles-touch（R101/R102）

R102补充作者参考视频：用户提供约39.9秒/720×1280/30fps录像，画面为方形Mosaico原板，非本吧唧。确认原版拖动局部推开回位、双击大量不同尺寸彩色粒子散开重组、摇晃TIME TILES FLY、顶部三击近全屏代码雨；本版双击/代码雨是简化重建，摇晃场景未接入。长按在该片未清楚展示，视频有剪辑且末尾网页1.0.2，与恢复包标记不同。原图/时间段/差异见outputs/timetiles-video-20261015/observations.md；本轮未改源码/烧录/访问USB，没有新增本板功能或性能通过。

用户明确要求“烧录”，已复核现有发布BIN SHA256 `98b7c43aa87e7dd5fd4c7b64647c2d4acd7961cc645c4562a8273814b677bf2a`，通过COM3仅写0x10000应用区，3982944B/32.8秒、Hash of data verified。只读启动状态确认7.9.21-timetiles-touch、TimeTiles store_error=0；前后选中壁纸、库项/名称/类型/payload CRC/timing CRC、亮度56及reduced_motion=false相同。主题4、时区480保留，番茄/倒计时烧录前均已完成，启动后仍为完成；本轮没有验证运行计时重启恢复。

串口已释放；沿用用户自行手测安排，未自动切页、重置计时、建闹钟或运行触摸/联网功能测试。触摸效果/方向、真实SNTP、提醒/贪睡/长稳/撕裂仍待用户。主记录R101，原始证据outputs/timetiles-flash-20261015/；发布目录另增hardware-verification.json，构建时verification.json不改。正式发布和工作台默认包未改，未提交/推送。

## 2026-10-14 历史：7.9.20-timetiles；7.9.21编译候选（R100）

用户提供 `reverse/mosaico-b359b358` 的恢复 C/汇编及可读辅助函数，授权继续移植；当前应用第六项 TimeTiles。原样计时辅助函数和直接提取字体/图标/六主题/119掩码，重建时钟、番茄钟、倒计时、六闹钟/星期/贪睡、方向切换及私有 NVS 检查点。GUI拥有一张259200B PSRAM图，没有新增任务或改显示DMA/分区。重启将运行计时恢复暂停，完全断电后须联网或手动校时。

首版7.9.20构建103.52秒、RAM49840B、Flash3978251B、BIN3978672B，SHA256 `8cdeb6d323c0b84d3ceb1425aa8f0017be06b7235455c15fe72525f65cfaf5f6`。COM3只写0x10000、32.8秒Hash of data verified；USB读回版本。两次自动探针因用户已有专注/倒计时在保护检查处停止，未执行重置、临时闹钟或重启测试；用户后来取消自动测试，串口已释放，不继续干扰当前手测。

用户指出首版只接按钮短按而漏画布特效；7.9.21-timetiles-touch已补普通数字推开/弹性回位、双击散开并切换彩色、680ms长按聚拢、标题三击字符雨，四方向输入回算一致。按原触发规则适配重建三种彩蛋，抖音加载失败，未观看/比对演示；整点、摇动、日期彩蛋和完整Demo未移植。校时使用UTC系统时间/默认UTC+8；手动保存重排未来闹钟，单调计时不变；修复手动日期有效后不启动后续SNTP的问题，联网后幂等启动共享ntp.aliyun.com客户端。

四项TimeTiles生产C检查及motion/心率/EAF集成回归PASS，触摸版最终构建22.17秒、RAM49856B、Flash3982523B、BIN3982944B，SHA256 `98b7c43aa87e7dd5fd4c7b64647c2d4acd7961cc645c4562a8273814b677bf2a`；4发布产物/54构建源指纹匹配，分区与旧版相同。新包 `outputs/cabadge-v7.9.21-timetiles-touch/` **未烧录**，设备仍保留首版手测状态；不把新源码/编译当作板上更新或实屏通过。

操作、时间校准和差异见TIMETILES.md；原始日志与失败在outputs/timetiles-port-20261014/，根主记录R100。实屏触摸/方向、真实联网时间、到期/贪睡/断电恢复、帧率/撕裂/长稳均待用户手测；无TE、扬声器或振动马达。正式v1.1.0及Workbench默认包未改，未提交/推送。

## 2026-10-11 历史开发实板：7.9.19-fluidbox（R99）

按用户要求移植 eMUQI/esp-mosaico-fluidbox，固定提交75d0c076b3f5b3529253f9238bc936875c47a159，MIT LICENSE/NOTICE及原始33文件blob校核保留。原算法普通C，可在S3运行；未导入S31/CO5300/BMI270/TE驱动。1,200粒子、Particles/Liquid/Water/Lava/Ukiyo-e全部保留；360px圆形容器、native RGB565经原LVGL/40MHz QSPI显示，SC7A20实测轴向输入倾斜/摇动，无omega/alpha旋转惯性。应用第五项“FluidBox”，底部重来/切换、左上返回。

大数组PSRAM BSS，打开时两张画面518400B；core1求解/core0渲染均priority2，GUI持有LVGL对象，关闭先join再释放，息屏暂停。静态内部RAM49328B、Flash3789171B；独立产物outputs/cabadge-v7.9.19-fluidbox。COM3仅写0x10000、hash verified，应用SHA256 d466aef158ac61bc3f86727d296deadbb4c12e8a488de6634eb5828073c74668；分区、NVS及11项壁纸CRC保持，亮度56/reduced_motion=false。构建与临时文件在F盘，正式发布及Workbench默认未改。

五模式两轮各约10秒，复测显示最后块DMA完成率11.71/8.34/8.26/6.42/8.66 FPS，初测9.46/6.72/7.12/5.19/8.46。重置/休眠恢复/六次开关/原ID10 EAF恢复通过，motion与壁纸errors0；关闭后15任务、运行17任务。扣除页面cache占用后PSRAM六次相差仅8B，未发现持续图片泄漏；首轮脚本把cache回收导致空闲量增加判成失败，保留原失败JSON/log，不改固件掩盖。性能受求解/渲染/外部内存与GUI合成限制，不能按S31的320MHz/250MHz PSRAM原始FPS推断S3收益。

自动测试已停止，设备Water模式亮屏、USB端口已释放；不要再自动切页干扰目视。颜色、真实倾斜/摇动跟随、触摸和无撕裂/长稳/温升仍待用户；TE未接，完成FPS不是扫描Hz。精确记录以首板测试记录R99为准，移植架构/复现见FLUIDBOX.md，原证据outputs/fluidbox-port-20261011；现有HRS异常重连/30秒无Notify实测待办保留。本包默认不编译R98混帧实验；源码开关和旧包保留。未提交/推送。

## 2026-10-06 历史开发实板：罗小黑插帧实验（R98，版本号仍7.9.18-heart-rate）

已按用户要求实现并COM3仅烧0x10000、hash verified。相邻RGB565帧逐通道50/50混合，非光流/AI；两张额外PSRAM源帧518400B，缓存下一原帧避免重复解码。原QSPI40MHz、16行/23块、末块DMA排空及版本号不变；正式默认包和旧心率应用未覆盖。应用SHA256 c7445619500a66f42b0f594d963838fca7e55ba67d52cd255fac648daf0a12c2，独立产物`outputs/cabadge-eaf-interpolation-20261006/firmware/`。

同版关/开/关、各预热2秒后约15/30/15秒：完成帧率28.000/23.440/28.034FPS，含等待完成间隔35.714/42.662/35.671ms；开启时354原帧+354中间帧均已计入23.440。缓存原帧完整cost20.189ms，中间帧预解码14.832+混合28.570+staging8.133+submit11.245+末块0.300及循环开销，共63.630ms；均摊处理41.909ms。保持原30FPS动画速度需要60FPS输出，本段原画面推进约11.72FPS；用户确认“更慢”。仅证明本混帧版本退步，不是所有插值算法或屏幕扫描Hz的结论。

五项生产像素/时序/借帧/输入/合成软件检查PASS。实板菜单/息屏/静态切回恢复及资源CRC/亮度46/减少动态false保留通过，errors0；首轮固定1秒恢复断言失败保留，状态等待复测在3.266秒Direct恢复。task15，transition/lvgl最小栈2384/3480B。重影/色彩/撕裂与长期稳定未验收，旧HRS真实重连/无Notify过期待测仍保留。详细方法、均值表、失败与证据以根主记录R98为准；不另建冲突总结。

实验编译环境`CABADGE_EAF_INTERPOLATE=1`；USB type41/sub11 `[11,0]`/`[11,1]`非持久开关，重启默认OFF，减少动态模式不接受开启。目前设备已恢复罗小黑ID10、保持插帧ON供观察，COM3已释放。日常关闭无需重新烧录或动壁纸库。新实测metadata为实验目录`hardware-verification.json`，构建时verification.json及所有历史产物原义不改。未提交/推送。

## 2026-10-06 历史壁纸样张：独眼扫视（R96，固件未改）

COM3实时读回仍7.9.18-heart-rate。本轮仅制作并导入用户提供的三色独眼动态壁纸：360×360、12秒循环、155个编码帧，EAF1443004B及逐帧timing；慢扫视/快速转向/停顿/瞳孔缩放，不是传感器或触摸跟随。新选中ID21，旧十项壁纸CRC、亮度46及reduced_motion=false保持。约13秒USB短检查帧154回到0、完成156、提交errors0；端口已释放，颜色/节奏/撕裂及长期目视验收待用户。素材、预览、源图备份和证据在`outputs/cabadge-eye-20261006/`，生成源码`tools/make_eye_wallpaper.py`；未重刷、未修改固件源码。浏览器预览支持减少动态效果，设备EAF不自动跟随该设置，可手动改选静态图。

R97只读瓶颈分析：沿用R96记录，平均decode14.256ms、staging8.074ms、submit11.046ms、末块drain0.306ms，合计33.682ms；主要限制是全帧解码/搬运/传屏帧级串行。submit已含前块DMA等待，不额外叠加理论SPI时间；长驻留使总体interval不能用作运动段FPS。即时只读探测COM3不存在，未新增实板采样；未修改/烧录/切换，优化收益尚未测量。详见主记录R97。

## 2026-10-04 最新开发实板：7.9.18-heart-rate

基于当前7.9.17-motion，复用ESP-IDF 6.1 NimBLE host/central外连和原GUI队列，新增标准HRS 0x180D/Measurement 0x2A37 Notify。Bluetooth列表识别/优先展示心率设备，选择后发现服务/自身CCCD并订阅；实时显示真实设备名、Connected及♥ BPM。30秒无有效通知stale/--，断线旧值立即失效，选中地址2～30秒退避重连，手动断开/关蓝牙取消；一个host callout兼顾5秒RSSI读数，无新RTOS任务/任务栈/蓝牙框架。不实现小米私有认证、步数/电量/睡眠。

生产协议/状态/GAP/GATT mock边界单测与motion/battery/compositor/EAF transition回归PASS；最终构建36.73秒，静态RAM60248B（较7.9.17 +508B）、Flash3761963B（+16016B），无warning/error匹配。纯BPM/RSSI/stale更新仅刷新心率标签，只有无线状态变化才调用全局UI刷新。应用SHA256=64a18f65773ddc59db6c3a852d48aa2b2fa25d4669173270bf272390fe1a6809；30份记录源hash校核一致，分区与旧包相同，COM3仅写0x10000、flash hash verified，NVS/壁纸库保持。正式v1.1.0和工作台默认包不变。

真实扫描到Xiaomi Smart Band 9 Pro 0863（D0:AE:05:01:08:63），已完成连接→CCCD→Notify→持续BPM更新；最终版12秒采样有效通知21次、invalid_packets0，BPM观察79～81，最后80/RSSI−65dBm，15个不同更新时间。首轮候选获用户确认蓝牙页面能看到设备/Connected/BPM并持续更新，最终版保持相同布局。实板任务仍15个，NimBLE栈配置4096B不变；最后采样lvgl最小剩余6664B、nimble_host1624B，Internal free78375B/minimum39828B。最终版首页扫描/连接/Notify期间EAF完成帧311→904、errors0；蓝牙页按原交接暂停不当作阻塞。两段50秒监测未捕获真实断开→重连过渡，自动重连及连接内30秒无通知stale仍待实物复测，不将单测当射频验收；Wi-Fi有开启但未关联，无并发大流量压力验收。

产物outputs/cabadge-v7.9.18-heart-rate（basic-check.json记录最终实板短测试及未验项目）；交接前只读采样仍Connected，BPM83、有效通知402、invalid_packets0，COM3已释放。主记录R95；接口/线程/限制及重复测试说明HEART_RATE.md。其他任务/程序化壁纸可include ui/heart_rate.h后调用ble_service_heart_rate_snapshot，必须检查stale；快照过期BPM=0，HTTP/USB JSON为bpm:null。预编码EAF本轮不新增心率叠层。UI和无线状态不跨线程直接操作LVGL。

## 2026-10-01 历史开发实板：7.9.17-motion（双敲修订候选）

基于7.9.16，仅接入SC7A20动作与应用：Grok倾斜注视/摇动暂态、稳定朝下1.2s息屏及自动息屏后的朝上0.3s唤醒、硬件双敲唤醒、应用第四项重力小球。显示DMA/壁纸/PCB/外壳未改；正式v1.1.0与工作台默认不变。游戏校准为临时软件水平零点；不是芯片永久倾斜校准，也不是ESP深睡。

首轮用户右低/下低/朝上实测确认screen=(rawX,-rawY,rawZ)；真实翻面亮灭获用户反馈通过。但Z-only/100Hz双敲侧壳及底壳未亮、硬件计数不增，保留失败。按用户要求只读重开V6原生SW装配：PCB未翻转、Z+1.8mm，中央距0.8mm底板1mm，三边缘支承；传感器靠近中心且在电池投影下。CAD不替代振动或打印实测。

修订芯片400Hz、应用约100Hz读取XYZ、XYZ硬件双击250mg、80ms脉冲/80–480ms间隔；保留最近IA源便于排除触摸唤醒。参数在ui/motion.h，轴映射在motion_service.c。test_motion.py实际生产函数及电池/转场回归PASS；编译/COM3应用烧录hash和USB版本/寄存器配置读回、应用列表/小球/Grok/息屏恢复/壁纸设置保留通过。最新应用hash184524ec2baf5f1345a20ba35f9b97597d30005cbecb555b098c7f530eca2fae。

修订装壳侧敲用户确认亮屏，double_taps由0增至19、最后源0x72(Y双击)、shake/face_wakes/errors为0；再次息屏后底敲用户确认亮屏，双击计数19→22、源0x74(Z双击)、errors0，但shake同时0→1，不能从聚合计数独立归因底敲首次亮屏分支。两次静置10样本计数保持；这是短复测，不是长期误唤醒验收。Grok/小球目视仍待反馈。最终设备主页亮屏、COM3释放。主记录R83、APPS.md/GROK.md及outputs/cabadge-v7.9.17-motion/basic-check-shell-tap.json。

## 2026-09-27 历史：7.9.16-eaf-transition

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

1. [当前产品规范](../docs/architecture/DESIGN.md)、[实际架构](../docs/architecture/UI_ARCHITECTURE_V7.md)。
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
