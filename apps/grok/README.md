# Grok 表情球

CABadge 原生 C 表情球源码，360×360 圆屏、25 组左右眼轮廓、32 种状态数据。
轻点切换表情，拖动跟随手指，长按显示控件，支持配色、倾斜注视与短暂摇晃反应。
复杂网页特效没有全部移植，32 个状态不代表 32 套原版特效。

## 文件与接入

`ui/grok.c/.h` 为动画、触摸和 RGB565 绘制，`ui/grok_data.h` 为生成后的几何数据，
`ui/generate_grok.py` 从 `ui/grok_source/data.json` 重新生成数据。
`ui/badge_ui.h`、`ui/heart_rate.h` 仅保存宿主接口声明。

编译 `ui/grok.c`，包含 `ui`，使用宿主 LVGL 9 和 `font18`、点击保护等接口。
在 GUI 任务调用 `grok_open(parent, back)`，可见且亮屏时约每 33ms 调用
`grok_tick(lv_tick_get())`，退出调用 `grok_close()`。
使用现有传感器桥接 `grok_motion(x, y, shake, available)`；传感器失效时要传 `available=false`，
不能一直复用旧倾斜值。系统息屏时暂停绘制。图像在 GUI 任务中拥有和更新，
退出先销毁对象/清图片缓存，再释放内存。ESP32 实现使用 PSRAM。

## 数据检查与来源

```powershell
python -B apps/grok/ui/generate_grok.py
```

生成器校验 25 组双眼轮廓、32 条状态及索引边界；不是实屏或动画性能验收。
原始几何来自 [tycoding/grok-ball](https://github.com/tycoding/grok-ball)，MIT。
原许可证见 [grok_source/LICENSE](ui/grok_source/LICENSE)，来源指纹见 `source.json`。
CABadge 代码许可证见 [LICENSE](LICENSE)。
