# CABadge 当前实体固件

项目发布 **v1.1.0**，内部版本 **7.9.2-memory**；发布说明、安装与已知边界以 [Release](../docs/RELEASE-v1.1.0.md) 为准。`run.cmd` 打开原生工作台，默认包为 `outputs/cabadge-v7.9.2-memory`，不是旧 USB 画面预览。

## 使用入口

- 展示页：壁纸与蓝色名片；下拉控制中心，上滑功能面板。
- 功能面板：壁纸管理、设置和应用入口；[应用说明](APPS.md)、[Grok 操作](GROK.md)。
- 连接：Wi-Fi、BLE、手机管理；同网或开放直连热点上传/应用/删除壁纸，无授权码和倒计时。
- 设置：显示、设备信息和息屏；亮度驱动实际背光，减少动态直接切终态。设备信息包含外部 16 MB Flash 容量信息。
- USB 保留烧录、状态/控制和壁纸传输；旧像素投屏与壁纸读回已移除。

## 当前显示与内存架构

ESP32-S3R8、8 MiB PSRAM、ST77916 360×360 RGB565、QSPI 40 MHz、CST816D；ESP-IDF 6.1.0、LVGL 9.4.0、esp_lvgl_adapter 0.7.1。PARTIAL、双 360×360 PSRAM 绘图缓冲、DRAW_UNIT=1、DMA queue=2。

静止显示真实 LVGL 控件；静态/半静态转场使用单份 LCD-native RGB565 缓存与 Direct Compositor。缓存未 READY 时走安全路径，动态 App 本体实时绘制。缓存预算 NORMAL 最高 3 MiB，HIGH 软目标 1 MiB，CRITICAL 512 KiB；保护在用缓冲，内存压力下暂停预热。Flash L2 未启用。

LVGL 与 Direct 互斥共用两块 11,520 B 内部 DMA staging；最后 DMA 完成后才通知 LVGL。无 TE，不保证所有动态画面无扫描撕裂。LCD 提交次数不是面板扫描 Hz。

BlueMap 使用 16 KiB HTTP + Pngle 流式采样，不创建整张 RGBA；用户缩略图五项 LRU 加至多一张待提交图。内置壁纸使用 Flash 原图；名片没有独立常驻快照。

## 开发与验证

板端入口：`usb_screen/build.ps1`；[完整构建说明](../docs/BUILD.md)。源码路径：`ui/` 为页面/cache/compositor，`usb_screen/device/src/` 为驱动与服务。`usb_screen` 是沿用的目录名。

最小检查：`test_map_stream.py`、`test_physical_display.py`、`test_transition_compositor.py`。实板检查记录见 [R63/R64](../首板测试记录.md)，软件检查不能替代目视验收。

旧桌面模拟器、像素投屏与废弃测试已移出当前树。保留三个当前内存/DMA/compositor 最小检查和工作台 USB framing 自检；历史工具可从 Git 旧提交恢复。板端构建仍使用 `usb_screen/build.ps1`。

[当前交接与历史演进](HANDOFF.md) · [文档索引](../docs/INDEX.md) · [已知限制](../docs/KNOWN_ISSUES.md)
