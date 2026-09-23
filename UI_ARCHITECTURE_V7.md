# CABadge 当前 UI 与转场架构

版本：v1.1.0 / 7.9.2-memory。本文以当前源码为依据，描述实现和所有权，不是最初 PC 预览方案，也不宣称完整性能/交互验收。

## 运行链路

```text
CST816D / 服务状态
        ↓
GUI task：真实 LVGL 页面、输入、业务状态、缓存预热
        ├─ 静止/普通刷新 → esp_lvgl_adapter → physical_display → ST77916
        └─ 合适的页面转场 → 已就绪 LCD-native RGB565 cache
                            → Direct Compositor worker
                            → physical_display → ST77916
转场结束：排空 DMA → 交还 LVGL → 恢复真实目标页
```

平台为 ESP32-S3R8、8 MiB PSRAM、360×360 RGB565、QSPI 40 MHz；ESP-IDF 6.1.0、LVGL 9.4.0、Adapter 0.7.1。PARTIAL 模式、两个 360×360 PSRAM draw buffer、DRAW_UNIT=1、transaction queue=2。配置依据在 [sdkconfig.defaults](firmware-v7/usb_screen/device/sdkconfig.defaults) 与 [组件清单](firmware-v7/usb_screen/device/src/idf_component.yml)，发布实际配置同时保存在 Release 调试包。

## 页面职责与导航

展示页包括壁纸与蓝色名片；下拉控制中心、上滑功能面板。功能入口连接壁纸管理、设置和应用。设置/连接相关页面包含显示、设备信息、Wi-Fi、BLE、手机管理。应用包括 Grok、BlueMap 和奶蛙矿工。

[badge_ui.c](firmware-v7/ui/badge_ui.c) 中 `badge_ui_create()` 创建对象，`badge_ui_page()` 处理页切换，`navigation_prewarm()` 维护实际导航预热优先级。缓存 page ID 与业务页索引是不同概念，不应直接用同一数值猜测对象。

设置↔连接使用 `(50,28)`、250×272 局部 viewport；`connection_wrap()` / `connection_enter()` / `connection_leave()` 负责内容组织。其他页面按实际 surface 大小注册，不应把这个局部尺寸套给所有转场。

## 模块边界

|模块|责任|
|---|---|
|badge_ui.c / wifi_panel.c|真实页面、控件、手势与视觉状态更新|
|apps.c / grok.c / miner.c|应用页面与各自实时逻辑|
|ui_transition_cache.c|surface 注册、revision、预热、预算、淘汰、在用引用|
|ui_transition_compositor.c|offset 对应图层、裁剪/stride、分块合成、worker 生命周期|
|physical_display.c|LCD 提交、显示所有权、触摸和共享 DMA staging|
|main.c|Adapter 接入、GUI/service 调度、USB 协议|
|app_service.c / map_png.c|BlueMap 网络与解码 worker、结果交接|
|wallpaper_service.c|上传、持久化、后台缩略图与 GUI 提交|

模块路径及入口见 [板端服务说明](firmware-v7/usb_screen/README.md)。业务页面不直接调用 SPI API。

## 缓存生命周期

`ui_transition_cache_register()` 注册真实不透明 subtree；视觉变化调用 invalidate，导航调用 plan/request。`ui_transition_cache_poll(idle)` 在 GUI 线程空闲时每轮最多构建一张：布局 → LVGL snapshot RGB565 → 一次 byte swap → READY。

构建是 GUI 内的受控空闲工作，不是从独立线程调用 LVGL。存在触摸、动画、活动转场、暂停或高内存压力时停止预热。透明根/ext-draw 等不适用对象保留 fallback。

缓存仅长期保存一份 LCD-native RGB565；revision 匹配才有效。动画使用期间 pin，完成后才释放使用权。hide/show/reparent 不应直接等同于视觉变更。页面真正销毁或预算压力可以释放缓存。

当前 NORMAL 预算最高 3 MiB，HIGH 软目标 1 MiB，CRITICAL 512 KiB；优先保留当前/hot/在用资源，故软目标不是绝对占用上限。按 priority/LRU 选择淘汰，低空闲内存停止非必要预热。退出高内存 App 并确认 worker cleanup 后恢复导航热集。Flash L2 未启用。

相关实现：`ui_transition_cache_plan()`、`ui_memory_pressure_set()`、`ui_transition_cache_poll()`、`ui_transition_cache_direct()`。缓存 miss 不同步截图等待用户，回到现有真实控件安全路径；不能将 LCD-native 数据当普通 lv_image 像素使用。

## Direct 与 DMA 所有权

`ui_transition_compositor_begin/present/stop()` 接收最多四层不可变缓存描述，保留 stride、位置及裁剪信息。worker 按块读取/合成，不每帧分配完整 framebuffer。它仍有 staging copy，不能称为零拷贝。

`physical_display_transition_acquire()` 在实际传输完成后交接；`physical_display_transition_release()` 在 Direct 最后一批传输结束后交还。普通 LVGL 与 Direct 不能同时向 LCD 提交。

两条路径共享两块 11,520 B 内部 DMA buffer。普通 LVGL 路径逐 16 行复制/提交，只在最终块完成后通知 Adapter/LVGL；缓冲复用和所有权释放依赖真实 completion/drain，不用固定 delay 猜测。

Direct stop 等待 worker 完成后才能解除源缓存 pin。取消、cache miss、源不可用和内存不足走安全退化；减少动态直接到终态。转场结束恢复真实页及输入，状态变化不能被旧截图永久覆盖。

## 动态资源与服务

Grok、BlueMap、游戏本体不冻结为长期截图。进入/退出的转场仅在合法 surface 可用时使用缓存，否则走安全路径。

BlueMap：16 KiB HTTP 输入 → Pngle 增量解码 → 128×128 RGB565 tile（最多九张）→ 360×360 输出。保留上/下半图结构识别与完整性校验，不创建完整 RGBA。worker 完成后由 GUI 接收结果，退出等待实际 cleanup；旧/新 framebuffer 在交接期可并存。

内置壁纸使用 Flash 原图；用户缩略图采用五项 LRU、最多一张待提交图，三张可见图受保护。正常页面通过真实服务状态刷新，视觉变化使关联缓存失效。

## 调试与验证边界

正常使用直接观察实体屏；旧 SDL2/USB 像素镜像不是当前验收路径。详细性能统计由 CABADGE_DISPLAY_PERF 控制，正式包为关闭；轻量 cache debug 开启。无 TE，DMA 完成不等于面板扫描完成。

当前实板证据见首板记录 R63，构建/安装见 [BUILD](docs/BUILD.md)。尚未逐一迁移的桌面测试、长期稳定性和人工目视范围见 [仓库检查](docs/REPOSITORY_CHECK.md)。本架构说明不引入新的 API、参数或功能。

[重写前 PC 预览架构（历史固定快照）](https://github.com/nbtca/CABadge/blob/107f518cade6b85d44fc918d985363973a5d8b22/UI_ARCHITECTURE_V7.md)
