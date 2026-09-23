# CABadge v1.1.0 — 正式固件发布

本版发布当前已烧录并完成基本检查的 **7.9.2-memory** 固件。项目版本为 v1.1.0，设备内部版本仍显示 7.9.2-memory，两者对应同一发布包。此前已有 v1.0，故保留历史版本并递增到 v1.1.0；本次不修改或重新编译已验证二进制。

## 更新内容

- ESP-IDF 6.1.0、LVGL 9.4.0、esp_lvgl_adapter 0.7.1；QSPI 40 MHz、DRAW_UNIT=1。
- Cached Bitmap + Direct Compositor 页面转场，单份 LCD-native RGB565 缓存、导航预热和按内存压力回收。
- BlueMap 16 KiB HTTP 流式 PNG 解码，直接采样到 RGB565 tile，取消完整 RGBA 中间图；退出时等待后台资源清理。
- 普通显示与 Direct 共用 23,040 B 内部 DMA staging；关闭正式版旧显示诊断数组。
- Flash 内置壁纸直接使用原资源，用户缩略图采用后台加载的五项 LRU；名片复用统一转场缓存。
- 保留 Wi-Fi/BLE、手机本地管理、壁纸库、蓝色名片、Grok、MC 二维地图与奶蛙矿工。

## 安装

适用于 c7c59dff 基线、ESP32-S3R8 / 8 MiB PSRAM / 16 MiB Flash / ST77916 360×360 QSPI。硬件未改版，硬件制造附件沿用历史 v1.0；J1 选型以 docs/J1-CONNECTOR.md 更正为准。

固件包解压后先校验 SHA256SUMS.txt。安装 Python、esptool、pyserial 后，在 BIN 所在目录执行，COMx 换成实际端口：

```powershell
python -m esptool --chip esp32s3 --port COMx --baud 115200 write-flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

不要全片擦除。旧 v1.0 壁纸分区是 1 MiB，本版在相同 0x710000 起点扩展到 0x8f0000，因此旧版升级需一起更新 partitions.bin。迁移代码保留，但本次未重新实测从 v1.0 升级；升级前保留壁纸原文件，必要时先用 esptool read-flash 0 0x1000000 backup.bin 备份整片 Flash。备份含用户网络配置，不要公开上传。

已经使用相同分区布局的 7.9.x 用户可只写应用：

```powershell
python -m esptool --chip esp32s3 --port COMx --baud 115200 write-flash 0x10000 firmware.bin
```

当前已运行 7.9.2-memory 的设备无需重复烧录。工作台使用者将固件包解压到 outputs/cabadge-v7.9.2-memory/，通过 firmware-v7/run.cmd 打开现有工具。

## 验证与边界

发布二进制已完整编译、COM3 应用烧录及哈希验证。一次基本检查覆盖主页、设置/连接、图库、Grok、BlueMap 返回；地图取得四瓦片、一玩家、error0，Wi-Fi 连接、BLE 广播，Direct errors0。原八张壁纸的持久化身份保持。

主机检查通过 PNG 碎片输入/采样/CRC/截断拒绝，DMA buffer 生命周期/最终完成通知/错误排空，以及 compositor stride/clip/取消检查。没有进行大规模 benchmark。

实屏触摸、撕裂/残影/颜色、BLE 对端连接和长期稳定性仍需用户验证。无 TE，不能保证所有场景无撕裂。手机管理热点为开放网络且无授权码；GIF、BLE 传图、OTA 不列为本版已交付能力。构建环境有 Windows/F 盘依赖，未验证干净机器字节级重建。

## 附件

- CABadge-v1.1.0-firmware.zip：三个 BIN、原始 verification.json、校验文件和安装说明。
- CABadge-v1.1.0-debug.zip：ELF、有效 sdkconfig、原始 verification.json。
- SHA256SUMS.txt：发布 ZIP 的 SHA-256。
- Source code：v1.1.0 标签对应当前 firmware-v7 源码，不包含开发缓存和个人设备原始日志。

第三方来源见 docs/THIRD_PARTY.md；第三方素材不因本项目 MIT 许可自动转授其他权利。
