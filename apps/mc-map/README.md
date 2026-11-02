# MC 实时地图

CABadge 的 BlueMap 二维地图源码快照：主世界/下界/末地、拖动、六档缩放、
出生点复位、玩家位置，异步 HTTPS 下载、PNG 流式解码、瓦片缓存与渐进更新。
默认地图地址在 `app_service.c` 的 `MAP_ORIGIN`，运行需要能访问服务器的 Wi-Fi。

## 文件与共享边界

- `ui/apps.c/.h`：地图界面、拖动、缩放、玩家和 GUI 结果接收。
  地图当前仍内嵌在共享应用壳中，所以原文件同时包含其他应用的菜单注册与生命周期调用；
  没有复制其他应用实现，也没有为了源码发布重写这部分运行逻辑。
- `usb_screen/device/src/app_service.c`：HTTP/JSON、请求取消、工作任务、结果交接及共享 SNTP 校时。
- 同目录 `map_cache.*`、`map_lane_policy.h`、`map_png.*`、`map_perf.*`：缓存、内存准入、解码与诊断。
- `pngle/`：PNG/DEFLATE 实现及其许可证。
- `ui/badge_ui.h`、`heart_rate.h`、`ui_transition_cache.h`、其他应用 `.h` 及服务 `.h`：宿主接口声明。

## 接入 CABadge

保留共享 `apps.c` 时，组件编译 `ui/apps.c`、`app_service.c`、`map_cache.c`、
`map_png.c`、`map_perf.c` 和 `pngle/miniz.c`；不要单独再编译被 `map_png.c` 包含的 `pngle.c`。
宿主提供 LVGL 9、中文字体、转场/内存保护、物理显示状态与壁纸服务的 Wi-Fi 地址接口，
以及已有的其他应用接口实现。若只移植地图，需由移植方把共享菜单和其他应用调用
接入自己的应用壳；本包不宣称是已经解耦的独立运行程序。

`badge_apps_create(parent)` 建立 GUI 与现有 33ms 定时器；`badge_apps_launch(1)` 打开地图，
`badge_apps_hide()` 退出。宿主初始化 `app_service_init()`、GUI 线程持续调用 `app_service_poll()`，
由工作线程下载/解码，GUI 线程消费结果并更新图片，不在网络回调中操作 LVGL。
需要 ESP-IDF HTTP/TLS 证书包、SNTP、FreeRTOS、PSRAM 与 cJSON。
HTTPS 保留证书校验，连接可上网的 Wi-Fi 后才可能取得地图/网络时间。

## 检查与许可证

在仓库根目录运行（既有 Zig 环境；流式解码检查另需 Pillow）：

```powershell
python -B apps/mc-map/tests/host/test_map_cache.py
python -B apps/mc-map/tests/host/test_map_perf.py
python -B apps/mc-map/tests/host/test_map_stream.py
```

覆盖缓存/内存准入、诊断 JSON、碎片输入、采样一致性及 CRC/截断拒绝。
这些宿主检查不访问地图服务，也不证明设备网络、触摸、画面或帧率。
PNG 许可证见 [pngle/LICENSE](usb_screen/device/src/pngle/LICENSE)；
miniz 的版权/许可条款保留在文件内，CABadge 自编代码见 [LICENSE](LICENSE)。
