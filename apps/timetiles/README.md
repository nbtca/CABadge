# TimeTiles

CABadge 应用源码快照：时钟、番茄钟、倒计时、六个闹钟、方向切换、六主题，
以及拖动、长按、双击散开、三击字符雨和摇晃字块飞散等交互。
应用基于本地 `7.9.25-poke-duel` 内保留的 `7.9.23-timetiles-fix` 实现。

## 文件与来源

- `ui/timetiles.c/.h`：LVGL 界面、触摸、NVS 保存、时间校准和生命周期。
- `ui/timetiles/core.c/.h`：计时、提醒、闹钟和方向状态。
- `ui/timetiles/raster.c/.h`、`effects.c/.h`：RGB565 光栅、字块物理与特效。
- `ui/timetiles/readable_timer_helpers.c/.h`：恢复材料中的两个可读计时辅助函数。
- `ui/timetiles/timetiles_assets.h`：从同一原应用提取的字形、图标、配色和掩码。
- `ui/badge_ui.h`、`ui/heart_rate.h`、`ui/apps.h`：宿主接口声明。

这是根据行为恢复材料编写的 ESP32-S3 适配版，并非原作者完整工程。
原应用 SHA-256 为 `db1e45bf8801060a03a6ec25420cc354c31dd6a6732437fa4064badbd64771d0`。
提取资源和恢复函数没有因此被重新授予 MIT；相关边界见 [NOTICE](NOTICE.md)。

## 接入 CABadge

组件编译 `ui/timetiles.c` 及 `ui/timetiles/` 中的四个 `.c` 文件；包含 `ui`。
需要 LVGL 9、ESP-IDF 的 PSRAM、单调时钟、系统时间与 NVS，以及宿主中文字体。
GUI 初始化时调用 `timetiles_init()`；显示调用 `timetiles_open(parent, back)`；
持续约 33ms 调用 `timetiles_poll(当前可见且未息屏)`，隐藏或息屏时仍调用 `false` 以保持计时/提醒；
退出调用 `timetiles_close()`。传感器桥接 `timetiles_motion()` 和 `timetiles_shake()`。

宿主提供 `badge_apps_clock_sync()` 的共享非阻塞联网校时入口，以及头文件声明的
唤醒、转场/休眠判断、点击保护和字体；`ui/apps.h` 是接口，不包含网络实现。
可以复用 MC 地图包 `app_service.c` 中的 SNTP 入口或宿主等效实现，不能再创建重复的校时客户端。
原 NVS 命名空间 `badge_timetiles`、键 `state_v1` 保持原二进制格式。
断电没有持续走时保证，重启后活动计时恢复暂停，须重新联网或手动校准日期。

## 检查

在仓库根目录运行（使用既有 Zig 环境，或设置 `CABADGE_ZIG`）：

```powershell
python -B apps/timetiles/tests/host/test_timetiles_core.py
python -B apps/timetiles/tests/host/test_timetiles_raster.py
python -B apps/timetiles/tests/host/test_timetiles_effects.py
python -B apps/timetiles/tests/host/test_timetiles_integration.py
```

检查使用本目录生产代码，覆盖计时/闹钟、像素边界、NVS 和输入/特效。
本次是源码整理，不新增实板功能结论；原有手势、长期运行、日期/联网及提醒验收边界保留。
