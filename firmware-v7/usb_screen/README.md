# CABadge 板端服务与 USB 工具

当前版本 **v1.1.0 / 7.9.2-memory**。本目录名称沿用早期 USB 屏幕阶段；现固件直接驱动实体 ST77916，旧 USB 像素投屏及壁纸读回已移除。

## 使用

- 安装：见 [正式发布与升级](../../docs/RELEASE-v1.1.0.md)，或运行 `../run.cmd` 的工作台安装入口。默认包为 outputs/cabadge-v7.9.2-memory。
- USB：烧录、状态/控制、壁纸上传；不提供实时画面镜像。
- 手机管理：在设备“连接 → 手机管理”打开。同一网络访问设备显示的地址；直连模式连接 CABadge-xxxx 开放热点并访问 http://192.168.4.1/ 。无访问码或 300 秒授权窗口。
- 壁纸：手机/电脑裁剪为 360×360 RGB565 后上传，支持保存多张、应用与删除。wallpaper 分区从 0x710000 起，长度 0x8f0000，最多 31 张用户图片；恢复默认不删除图库。旧分区升级注意事项见正式发布说明。
- Wi-Fi：扫描、连接、保存历史网络和重连；手机热点要允许设备访问所需网络。
- BLE：NimBLE 状态/控制、广播与扫描连接；不提供经典蓝牙音频或 BLE 图片传输。
- SC7A20：仅息屏时摇晃唤醒，亮屏不跳页；不是主控深度睡眠。

## 源码导航

|路径|职责|
|---|---|
|device/src/main.c|Adapter 接入、GUI 调度与 USB 服务|
|device/src/physical_display.c|ST77916、触摸、显示 ownership、共享 DMA staging|
|device/src/wifi_service.c / ble_service.c|无线任务与状态|
|device/src/wallpaper_service.c / wallpaper_store.c|HTTP/USB 壁纸库、异步缩略图与持久化|
|device/src/wallpaper.html|手机本地管理页面|
|device/src/app_service.c / map_png.c|BlueMap 网络与流式解码|
|../ui/ui_transition_cache.c / ui_transition_compositor.c|缓存、预热及 Direct 转场|
|../workbench.py / wallpaper.py|当前原生工作台及上传工具|
|viewer.py / WIRELESS_P0.md|早期像素镜像工具与协议历史，不是当前产品入口|

构建入口 `build.ps1`，实际环境见 [构建说明](../../docs/BUILD.md)。当前有真实背光 PWM 和 LCD/触摸驱动，不再采用早期“只在电脑显示”的接法。不要按旧说明拔屏或烧录 v4/v5 预览包。
