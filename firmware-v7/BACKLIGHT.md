# 背光范围修正：7.6.3-light

用户反馈 10%～100% 变化幅度太小。原背光函数把 100% 映射为 51/1024，仍是首板点屏测试约 5% 上限；滑条拖动只更新文字及隐藏遮罩，松手才改变 PWM。

## 硬件依据

本地供应商 `1、规格书 W180TE010I-18Z(CTP-A1) SPEC规格书.pdf` 第 11 页明确说明：LEDA 为背光正极，可接 3.3V 串联 5.1Ω 限流电阻。第 8 页给出单 LED 典型电流 20mA、该条件下正向电压 2.9/3.1/3.3V（最小/典型/最大）；这些是规格条件，不是此板实测电流。

实际 PCB 文件中 Q1（AO3401A）源极接 +3V3、漏极接 BL_SOURCE；R13=5.1Ω 接 BL_SOURCE 与 LEDA。Q2（MMBT3904）下拉 Q1 栅极，BL_PWM 高有效。这与供应商给出的限流接法一致，因此取消测试阶段的 5% 软件限制。不把供应商接法等同于已完成电流、光学亮度或温升实测。

## 修改

- `usb_screen/device/src/backlight_curve.h`：10 位 LEDC，`duty=round(1023×(亮度/100)²)`，超范围钳制、息屏为 0，保留原 1kHz PWM。
- `ui/badge_ui.c/.h`：滑条 VALUE_CHANGED 更新临时预览，主循环读取预览值；RELEASED 才保存；PRESS_LOST、页面切换、息屏取消预览。移除界面的“预览亮度”旧称，改为“亮度”。
- `main.c`：沿用原背光更新位置，读取实时预览值，不更改 LCD 传输。
- `physical_display.c/.h`、`runtime_monitor.c`：沿用 LEDC 驱动；诊断增加 `backlight_pwm` 实际 LEDC 读回。
- 手机管理的亮度范围使用同一驱动映射；状态 scope 改为 backlight。

| 界面 | PWM duty /1024 | 约占空比 |
|---|---:|---:|
|10%|10|0.98%|
|20%|41|4.00%|
|25%|64|6.25%|
|50%|256|25.00%|
|75%|575|56.15%|
|100%|1023|99.90%|

百分比是用户调节档位，不代表亮度计测得的相对光学亮度。升级前保存值为 100%，本次先调到 20% 后烧录，以免重启直接升到最大输出；测试结束也保留 20%。

## 验证与回退

`python -B firmware-v7/test_physical_display.py` 使用生产函数，覆盖亮度端点、钳制、10～100 严格递增、息屏为零，以及已有传屏、DMA 生命周期、错误处理和触摸边界。

`python -B firmware-v7/backlight_probe.py` 读取 COM3 上的 7.6.3-light，对照 10/25/50/75/100/20 六档 LEDC duty，检查息屏、恢复及图库保持。原始结果见 `outputs/cabadge-v7-light-20260923/hardware-verification.json`。这是寄存器/软件读回，不是示波器、光学或 LED 电流测量；实体滑条手感仍需用户复测。

回退时仅把 `outputs/cabadge-v7-grok-full-20260923/firmware.bin` 写到 0x10000；不擦除 NVS/壁纸。旧固件在保存的 20% 下会较暗，因为恢复了 5% 上限。
