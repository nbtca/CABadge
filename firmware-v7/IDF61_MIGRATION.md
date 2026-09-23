> 历史设计/阶段记录：保留当时版本、方案和测量值，不作为当前安装或验收说明。当前正式版为 v1.1.0 / 7.9.2-memory，参见[文档索引](../docs/INDEX.md)。

# ESP-IDF 6.1 迁移记录

2026-09-23，基于 firmware-v7 的 7.3.10-mem。目标是直接迁移到 6.1，保留 UI、BLE、Wi-Fi、壁纸和 USB 服务。当前仍在显示兼容性验证中，不能认定完成升级。

## 构建与存储

- ESP-IDF 6.1.0 / PlatformIO espressif32 7.1.3 / framework-espidf 4.60100.0 / GCC 15.2.0+20251204。
- 独立工具链 F:\CABadgeBuild\platformio\idf61；暂存 F:\CABadgeBuild\staging\usb-screen-v7-idf61；TEMP/TMP F:\CABadgeBuild\temp。
- 旧 5.5.0 工具链、outputs/cabadge-v7-memory-20260923 发布包保留；工作台默认尚未切换。
- 移除已取消的内置 json 组件依赖，添加官方 espressif/cjson 1.7.19 及 dependencies.lock。应用 cJSON 调用保持原样。
- UI 仍 LVGL 9.4.0，240 MHz CPU、80 MHz Octal PSRAM、RGB565、40 MHz QSPI、原双绘图缓冲及异步 DMA 所有权不变。
- 6.1 默认 Picolibc；非 ISR FreeRTOS 默认位于 Flash。sdkconfig.defaults 未人为改变，生成配置差异另存 JSON。

## 实板试验

|版本|改动与结果|
|---|---|
|7.3.10-mem / 5.5.0|连接 Egger、BLE 开启的 13 场景基线 runtime-20260923-105932.json|
|7.4.0-idf61|编译、烧录、USB 读回、Wi-Fi 自动重连通过；用户确认只有背光没有图像。回退 7.3.10 后用户确认恢复。runtime-20260923-110627.json 的计数不能作为显示通过证据|
|7.4.1-idf61|仅尝试数据线空闲电平设高，仍黑屏；该改动已撤销。BLE 真实授权读写及 6 次通知通过|
|7.4.2-idf61|添加 1 MHz 面板寄存器回读：ID=0xFFFFFF，power=0xFF，format=0xFF，API error=0，未取得有效面板响应|
|7.4.3-idf61|使用公开 SPICOMMON_BUSFLAG_GPIO_PINS 强制 GPIO Matrix；保持 QSPI 40 MHz。待验证|

## 回退与边界

回退烧写旧包 bootloader.bin @0、partitions.bin @0x8000、firmware.bin @0x10000，分区表逐字节相同；不要擦除整片 Flash。升级前 NVS/PHY 和壁纸私有备份在 F:\CABadgeBuild\temp\idf61-board-backup，含用户配置，不打包发布。

SPI API 成功及 MCU 提交 FPS 不等于可见屏幕刷新。TE 未接，目视无撕裂仍须用户确认。尚未完成 6.1 HTTP、上传、触控、冷启动及长期稳定性验收。

## 官方依据

- [PlatformIO ESP32 发布记录](https://github.com/platformio/platform-espressif32/releases)
- [ESP-IDF 6.0 迁移入口](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/migration-guides/release-6.x/6.0/index.html)
- [6.1 迁移入口](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/migration-guides/release-6.x/6.1/index.html)

API 以新环境内 esp_driver_spi、esp_hal_gpspi、esp_driver_gpio、esp_hal_gpio 实际头文件和实现为准。没有修改 SDK 私有实现。
