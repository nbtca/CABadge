# CABadge 内存优化实测（2026-09-23）

基线 7.3.7-mem；当前板端/工作台安装候选 **7.3.10-mem**。未修改实体 UI 布局或动画；用户随后明确取消电脑实板预览，已删除工作台预览入口与固件壁纸读回。未运行模拟器。7.3.7/v1.0 原发布包保留。

## 阅读与取舍

已阅读用户提供的 [ESP-IDF RAM 指南](https://docs.espressif.com/projects/esp-idf/zh_CN/stable/esp32/api-guides/performance/ram-usage.html)、[ESP-Techpedia 内存优化](https://docs.espressif.com/projects/esp-techpedia/zh_CN/latest/esp-friends/advanced-development/performance/reduce-ram-usage.html)、[CSDN ESP32 内存优化](https://blog.csdn.net/xuan530482366/article/details/113354355)，并用 [ESP32-S3 / IDF 5.5 对应指南](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/performance/ram-usage.html)及本地头文件核实。

- 采用：分析静态大数组、复用/迁移普通数据缓冲、减少临时分配、利用已有任务与堆采样。总空闲和最大连续块分别记录。
- 7.3.7 已有 malloc 1024 B 阈值、双 PSRAM 绘图缓冲、图片缓存、上传缓冲及 USB 协议缓冲；本轮不将这些旧工作算作新增收益。
- 本机 IDF 5.5.0 的 `components/bt/host/nimble/Kconfig.in` 与 `components/bt/porting/mem/bt_osi_mem.c` 确认支持主机动态内存显式 SPIRAM；独立内部/DMA分配入口仍保留。没有移动蓝牙控制器及任务栈。
- Techpedia 的 C2 节省数字、C2 控制器代码迁移选项不能套用 S3。文中系统任务迁移补丁针对 5.5.2，且禁止对应任务调用 Flash 操作；本项目使用 5.5.0，涉及 NVS 和壁纸写入，不应用该补丁。
- 不压缩任务栈、无线缓存或数据缓存，不关闭 IRAM 中的性能路径，不改变 -O2。当前历史栈水位不是所有异常/上传路径的峰值证明，减少吞吐或放宽稳定性来换内存不符合本轮目标。

## 实施

1. **7.3.8-mem**：`management.c` 的 8192 B 状态缓存显式放入 PSRAM；`cJSON_PrintPreallocated` 直接写固定缓存，失败清空，避免动态扩容/复制。`wallpaper_service.c::web_status` 的响应缓冲显式 SPIRAM，不回退侵占内部 RAM。`main.c` 删除独占 4105 B 壁纸读回数组，第一阶段复用 USB scratch。
2. **7.3.9-mem**：仅增 `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y`；构建配置对比确认仅主机内存选项及其兼容别名变化，保留中央/外设角色和两个连接额度。
3. **7.3.10-mem**：按用户追加要求删除实板/离线预览启动入口及进程管理、固件 `BR_WALL_READ` 处理与 `wallpaper_read_resource`。握手不再宣布壁纸读回能力；旧协议编号保留，避免重排后破坏性能采样。历史模拟器源码/产物仍归档在原处，工作台不再依赖它。烧录、性能采样、USB状态与上传、HTTP/BLE管理继续保留。

应用 Flash 偏移 0x10000；三版烧录哈希均验证通过，bootloader/partitions 与 7.3.7 相同，未擦除用户存储。显示接口仍 QSPI40MHz、RGB565、两块 PSRAM 绘图缓冲、两块内部32行DMA暂存；DMA完成/缓冲复用、触摸和 UI 实现未改。

## 内存数据

单位 B；为每轮13个场景中周期采样的最低空闲/最小连续块，**不是所有瞬时峰值或重启以来水位**。

|版本|内部空闲最低采样|内部最大连续块的最低采样|PSRAM 空闲最低采样|
|---|---:|---:|---:|
|7.3.7-mem|36,951|24,576|6,552,832|
|7.3.8-mem|49,215|28,672|6,544,636|
|7.3.9-mem|71,767|31,744|6,523,064|
|7.3.10-mem|72,511|31,744|6,523,064|

最终相对基线，内部最低采样空闲增加 **35,560 B（34.73 KiB）**；PSRAM 空闲减少29,768 B，仍约6.22 MiB。ELF静态 `.dram0.bss` 45144→32856 B，减少12288 B；`.dram0.data` 21072 B、`.iram0.text` 122719 B不变。删除已复用的读回功能在7.3.9→7.3.10不会再额外释放4 KB，不能重复计算。

每个场景运行6秒，以相邻快照差值统计约5秒；Wi-Fi开启但未连上热点、BLE开启并广播，包含独立扫描场景。7.3.7是较长运行历史，候选经历重启；扫描发现数量/连接重试状态不同，因此动态差值不是严格分配归因或多轮中位数。静态节省与配置变化可直接核对。

|内容完成提交 FPS|7.3.7|7.3.10|
|---|---:|---:|
|壁纸浏览|28.26|28.21|
|上下抽屉|25.09|25.09|
|蓝色名片切换|21.44|21.71|
|壁纸＋USB状态查询|28.07|28.40|
|壁纸＋Wi-Fi扫描|27.69|27.95|
|壁纸＋BLE扫描|28.50|28.11|

静态壁纸、菜单、名片及息屏场景均0内容刷新。帧率基本持平；这次优化目标是内部 RAM 余量，不能宣称画面速度大幅提升。统计 SPI 完成提交，不等于面板扫描 Hz，也不证明无撕裂。

## 验证及异常

- 三版 ESP32 构建、哈希核验、实板烧录与版本读回完成。最终13场景完整采样；Wi-Fi/BLE扫描请求返回0。没有运行模拟器。
- 生产服务逻辑主机测试通过：授权、Origin、类型、CRC、队列、取消、超时、原子存储；新增 PSRAM分配失败、HTTP响应分配失败、JSON解析及容量不足检查。4项相关CTest、两种探针自检、工作台 `--check` 和 `--smoke-test` 通过；桌面冒烟不打开COM或模拟器。
- 最终实板 USB 上传开始/1024 B分块/取消完成，259200 B缓冲回收，壁纸 generation/size/crc/selected 保持不变；此测试未提交新图片到Flash。最终握手确认旧读回能力位取消。
- 7.3.8曾完整读回259200 B旧壁纸并核对CRC，超长请求被拒；这是删除前的历史证据，不代表最终仍保留该功能。
- 最终 Windows BLE 实连、授权写入、20 B状态读取、7次20 B通知、板端断开后恢复广播通过。电脑蓝牙为测试临时打开，完成后恢复关闭。早期7.3.8的Windows客户端断开后3秒状态仍显示未恢复广播，Windows保留连接因素未定位；最终增加板端主动断开验证成功，不覆盖所有手机主动断开路径。
- **7.3.9首轮在USB状态5Hz场景出现一次采样及清理超时**。重新握手恢复；不能据此断言发生重启、固件崩溃或确定NimBLE PSRAM是原因。首次不完整记录保留；7.3.9完整复测与最终7.3.10全场景通过。长期USB稳定性仍待测。
- 尚未验证：手机热点连接/实际Web首页与HTTP上传、双BLE链路同时连接、长期压力、实屏无撕裂及真实手指跟手。没有收到热点开启确认；当前没有有效STA IP。`http=true`仅代表服务初始化，历史F09不能因此关闭。

## 证据与修改位置

- 基线/逐项/最终：`outputs/performance/` 下 runtime-20260923-100928.json, runtime-20260923-101254.json, runtime-20260923-101922.json, runtime-20260923-102156.json 及相应jsonl；7.3.9失败首轮为 `runtime-20260923-101659.json/.jsonl`。
- 最终产物/构建烧录日志/配置/源码快照/实板JSON：`outputs/cabadge-v7-memory-20260923/`；`hardware-verification.json`独立记录后续验证，不改构建时verification.json的PENDING历史含义。
- 修改：`usb_screen/device/src/management.c`、`main.c`、`wallpaper_service.c/.h`、`sdkconfig.defaults`；`usb_screen/service_test.c`；版本号/build导出位置/探针版本白名单；`workbench.py`。
- 主记录本轮R38；原7.3.7发布与7.3.6撕裂失败历史保留。
