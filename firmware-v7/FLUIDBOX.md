# FluidBox 的 CABadge 移植

基于 eMUQI/esp-mosaico-fluidbox 的 MIT 源码，固定提交 `75d0c076b3f5b3529253f9238bc936875c47a159`。原始 ZIP、全部 33 个文件的 Git blob 校核结果及修改前本地文件备份在 `outputs/fluidbox-port-20261011/reference/`；许可证和 NOTICE 随 `ui/fluidbox/` 保留。

## 可行性

求解器是普通 C 浮点计算，渲染器是软件 RGB565 光栅化，没有调用 S31 专用图形加速器，能够编译为 ESP32-S3 程序。S31 的硬件驱动不能直接使用，性能也不能照搬。源工程默认 S31 320MHz、Octal PSRAM 250MHz；当前吧唧为 S3 240MHz、8MB Octal PSRAM 80MHz。

| 项目 | 上游 Mosaico | 当前 CABadge | 移植处理 |
|---|---|---|---|
| 屏幕 | CO5300，480×480，QSPI 60MHz | ST77916，360×360 圆屏，QSPI 40MHz | 复用原 LVGL / LCD adapter；不更改总线频率 |
| 容器 | 圆角方形三维盒 | 圆形屏幕 | 原 rounded-box 约束设为半宽圆角，初始粒子沿圆形截面排布 |
| 动作 | BMI270 加速度和陀螺仪 | SC7A20 加速度，±2g | 沿用实板验证的 screen=(rawX,-rawY,rawZ)；倾斜和摇动输入；omega/alpha 为零 |
| 同步 | TE 调度带传输 | TE 未接 | 复用现有缓冲/DMA 所有权；无扫描同步保证 |
| 控制 | AI 切换模式、BOOT 重置 | CST816D 触摸 | “切换”“重来”和左上返回 |
| 内存 | 独立固件的大量内部静态数组 | 同时保留无线、管理和其他应用 | 求解器/渲染器数组移到 PSRAM，画面缓冲只在打开应用时分配 |

保留 1,200 个粒子和 Particles / Liquid / Water / Lava / Ukiyo-e 五种模式。横纵尺寸、深度、粒子间距、平滑半径、投影、压力系数和位移上限按 360/480 调整；水面采样网格仍为 6 像素。保留原时间尺度及熔岩的额外减速。圆形容器与加速度量程不同，运动效果不保证逐帧等同原板。

原作者 `TESTING.md` 的一次粗测中，中位帧率分别为 59.1 / 24.1 / 14.1 / 12.5 / 33.5 FPS。其条件为 S31、1,200 粒子、60MHz QSPI；不是本吧唧的验收结果。CABadge 原生应用计时器是 33ms，轻量模式的画面交接上限约 30Hz；另有 LVGL 合成开销。

## 使用

展示页上滑 → 功能 → 应用 → 第五项 FluidBox。倾斜设备使流体移动，摇动产生扰动。底部“切换”循环五种模式，模式切换不重置粒子；“重来”重新布置粒子，左上返回应用列表。250ms 无有效传感器输入时使用屏幕向下的固定重力。

休眠暂停求解和画面更新；退出等待两个后台任务结束，再释放两张画面。重新进入默认 Particles，不持久保存模式。原动作息屏/唤醒逻辑继续生效。此应用运行时会持续占用 CPU；退出后停止。

## 集成与内存

- `ui/fluidbox.c`：GUI 生命周期、SC7A20 输入、后台任务、native RGB565 画面交接。
- `ui/fluidbox/sim.c` / `render.c`：上游求解器与五种渲染模式；大数组 `EXT_RAM_BSS_ATTR`。
- `ui/fluidbox/config.h` / `input.h` / `display.h`：圆屏参数、加速度转换、带拷贝接口。
- `ui/apps.c`：第五应用入口；原四项 ID 保持。
- 原 USB/HTTP 状态增加 `fluidbox`，包含运行、模式、粒子数、GUI 交接帧数、求解步数、最近求解/渲染耗时、任务栈余量与输入新鲜度。

两个低优先级任务分别在 core1 求解、core0 渲染；GUI core1 priority4 可抢占求解任务，无线服务仍可抢占渲染任务。渲染任务只写 back，GUI 接收完成后交换 front/back；不在后台调用 LVGL。液晶传输使用现有 LVGL 绘图缓冲和共享内部 staging，不直接异步读取这两张应用图片。关闭时必须先 join 再释放。

打开分配两张 360×360×2 的 PSRAM 图片，共 518,400B；任务栈 6,144+4,096B。静态外部 BSS 会整次开机保留，退出不释放；启用 `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` 也会移动部分 IDF Wi-Fi 等组件的 BSS。最终 ELF `.ext_ram.bss` 合计 363,356B，不全部属于 FluidBox。构建报告内部静态 RAM 49,328B、Flash 3,789,171B；应用 BIN 3,789,600B。

## 验证与复测

`python firmware-v7/tests/host/test_fluidbox.py` 检查实际生产求解器/渲染器：1,200 个粒子、六个重力方向、圆形/深度边界、数值有限、加速度转换、摇动/自由落体退化、无陀螺仪、五模式、360px 带窗口及帧缓冲 canary。这是代码检查，不生成模拟器画面，也不作为实屏验收。

实板脚本 `firmware-v7/usb_screen/fluidbox_probe.py --port COM3 --seconds 10 --output <证据文件>` 使用现有管理协议；type41/sub1 页14 打开，sub12 模式0..4 / reset5。分别记录 GUI 图片交接率及 runtime 最后块 DMA 完成率、求解频率、各模式耗时和内存，检查休眠/恢复、六次开关应用、返回原 EAF 播放及库 CRC/设置保持。真实 tilt、触摸、颜色、撕裂和长时间稳定性须实物反馈，计数不代替目视。

物理测试状态与结果统一见根目录 `首板测试记录.md` 的 R99；构建发布时 `verification.json` 保持待测含义，新增实测证据单独保存。独立固件目录 `outputs/cabadge-v7.9.19-fluidbox/`，正式发布和 Workbench 默认包保持原记录。

上游参考：https://github.com/eMUQI/esp-mosaico-fluidbox；原性能表：https://github.com/eMUQI/esp-mosaico-fluidbox/blob/75d0c076b3f5b3529253f9238bc936875c47a159/TESTING.md 。
