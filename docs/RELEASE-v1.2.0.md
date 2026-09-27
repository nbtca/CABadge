# CABadge v1.2.0 — 7.9.16-eaf-transition

本版使用已编译、烧录并完成基本检查的7.9.16二进制，不重新编译。硬件仍为c7c59dff / ESP32-S3R8 / ST77916 360×360 QSPI。

## 更新

- EAF动态壁纸通过Direct路径播放，支持每个资源独立的固定/逐帧timing；旧资源默认30FPS。
- 导航时冻结并借用当前RGB565帧进行转场，完成后恢复播放，避免转场入口整帧复制/换序。
- 统一壁纸库、BlueMap下载连接复用/两路调度/缓存修复及USB诊断、电池图标和低电量提示。
- 保留静态壁纸、Wi-Fi/BLE、Grok和其他应用。esp_emote_gfx仅保留可选原型，正式后端不变。
- 转换工具不随本次发布提供；格式和转换要求见docs/EAF_TIMING.md，仓库保留上传工具。

## 安装

解压firmware.zip，校验包内SHA256SUMS.txt。COMx换为实际端口，安装Python/esptool。

已使用相同分区布局的7.9.x设备只更新应用：

```powershell
python -m esptool --chip esp32s3 --port COMx --baud 115200 write-flash 0x10000 firmware.bin
```

首次安装或旧v1.0升级需写入三份BIN：

```powershell
python -m esptool --chip esp32s3 --port COMx --baud 115200 write-flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

不要全片擦除。壁纸分区为0x710000起、大小0x8f0000；旧v1.0升级会扩大分区，升级前保留壁纸原文件，必要时备份整片Flash。已验证设备仅做应用更新，未重新验证所有历史版本迁移。新EAF可占连续多槽，含这些资源的库不应直接交给旧固件写入。

## 验证范围

当前BIN已完整编译、应用烧录及hash校验；短USB检查覆盖动态首页与控制中心/名片/功能面板往返、暂停恢复和静态切换，增加15次Direct命中且无新增miss。软件检查覆盖冻结帧借用/生命周期、timing和Direct路径。实屏触摸、颜色、残影和撕裂仍需用户目视确认；未做长期压力测试，无TE，不保证无扫描撕裂。未测本次静态/动态转场可比耗时，不宣称提升比例。

## 附件

- CABadge-v1.2.0-firmware.zip：三个BIN、verification.json、安装说明与校验值。
- CABadge-v1.2.0-debug.zip：ELF、有效sdkconfig与verification.json，用于排查。
- SHA256SUMS.txt：两个ZIP的校验值。

不包含个人素材、Flash/NVS备份、原始设备日志或转换工具。
