# 标准 BLE 实时心率

开发固件：`7.9.18-heart-rate`，基于当前 `7.9.17-motion`，不改 PCB、显示 DMA、EAF 格式或正式版工作台默认包。

小米手环 9 Pro 开启“心率广播/心率共享”后，在吧唧 Bluetooth 页面点“扫描附近设备”，选择带“心率”标识的设备。关闭 PC 心率桥接等其他 Central，避免占用手环连接。列表保留其他 BLE 设备及原有手机管理连接；最多 12 项，心率设备优先保留、排在前面。

## 数据流和线程

`GUI 选择 → 原 commands 队列 → NimBLE host → 连接 → 0x180D → 0x2A37 → 其自身 0x2902 CCCD 写 01 00 → Notify → 线程安全快照 / 原 updates 队列 → GUI service_poll → 标签`

复用 ESP-IDF 6.1 的 NimBLE host/controller、一个 inbound 和一个 outbound 连接；不引入 Arduino/NimBLE-Arduino/Bluedroid，也不新增心率任务或任务栈。所有 GAP/GATT 回调和重连 callout 都在 host 上运行，不调用 LVGL；只有原 GUI 5ms 服务轮询修改 UI。心率更新只更新标签，不重建 BLE 列表，隐藏页面不做 LVGL 列表渲染。

扫描采用异步 `ble_gap_disc`，8 秒自动结束，100ms 间隔 / 12.5ms 窗口，主动收取 scan response。合并同一地址/类型的 ADV、scan response，识别标准 UUID16/32/128 及 16-bit service data，不把名字包含 Band 当成 HRS 的证明。设备名称使用真实广播名称，不把任意传感器硬编码为小米手环。

服务发现只查询 HRS；读取服务全部 characteristic 是为了确定 0x2A37 的 descriptor 结束边界，避免把后一个特征的 CCCD 当作心率 CCCD。校验 Notify 属性及 CCCD 写响应。通知只接收当前 outgoing 连接/0x2A37 handle；会话 generation 排除断线、取消后的旧 GATT 回调。

解析 byte0 flags bit0，0 为 byte1 uint8，1 为 byte1/byte2 little-endian uint16；长度不足的通知不刷新时间。后续 Energy/RR 字段不消费。未实现 Xiaomi auth/protobuf、步数、电量、睡眠等私有功能。

## 断线、过期和资源

断线立即撤销有效测量，UI 显示 `Reconnecting` / `♥ -- BPM`；按 2、4、8、16、30 秒上限自动重试**用户选中的地址**。手动断开或关蓝牙取消自动重连。重新连接仍须发现服务并成功订阅，不复用旧 handle。连接期间每 5 秒读取 RSSI，复用同一个 NimBLE callout。

30 秒无有效新 Notify 就显示 `stale` / `♥ -- BPM`。刚订阅尚无样本也不显示旧值；畸形通知不能刷新时间。`last_update_ms` 为设备单调运行时间（32-bit ms，差值按无符号回绕计算），不是墙钟。若手环重启后换了未绑定的随机地址，重新扫描并选择；不凭同名猜测另一只手环，不增加配对/私有身份认证。

测量状态 80 B；已有 BLE 队列单项扩大 80 B，仍只有一个最新值；设备列表/命令队列容量不变。额外持有选中设备、GATT handles、80 B 跨任务快照和一个 callout。连接所需 GATT/链路内存由既有 NimBLE 管理，继续使用已有 external/PSRAM 分配配置。构建和实板资源数据见根目录 `首板测试记录.md`，不能把静态 RAM 增量当作连接全过程峰值。

## 动态壁纸读取

GUI、壁纸或其他任务都可无分配读取一份 coherent snapshot：

```c
#include "ui/heart_rate.h"
badge_heart_rate_t hr;
ble_service_heart_rate_snapshot(&hr);
if (!hr.stale) {
    // hr.bpm 用于节奏/颜色等；UI 变更仍由 GUI owner 执行。
} else {
    // 停止 BPM 驱动，显示 -- 或回到默认动画节奏。
}
```

快照在 stale/断线时将 `bpm` 置 0，但必须检查 `stale`，0 不是有效性标记。状态还包括设备名/地址、RSSI、status/error、最后时间、有效通知数和畸形包数。现有管理 HTTP/USB JSON 增加根对象 `heart_rate`（过期 `bpm:null`）及 `ble.devices[].heart_rate`；未改变原二进制 Status/Control 布局。

现有 EAF 是预编码帧，本次不改解码器或强行把预编码动画变成心率可交互动画。后续程序化壁纸可读这个快照；若加心率 UI 叠层，仍走原 GUI / display ownership 交接。

## 可重复检查

```powershell
& F:/CABadgeBuild/venv/Scripts/python.exe firmware-v7/tests/host/test_heart_rate.py
& firmware-v7/usb_screen/build.ps1
& F:/CABadgeBuild/venv/Scripts/python.exe firmware-v7/usb_screen/heart_rate_probe.py --action scan
& F:/CABadgeBuild/venv/Scripts/python.exe firmware-v7/usb_screen/heart_rate_probe.py --action connect --name "Xiaomi" --seconds 12 --output F:/CABadgeBuild/logs/heart-rate-board.json
```

PC 单测编译实际生产解析/状态/GAP/GATT 函数，仅模拟 radio/RTOS 边界；不代表射频/屏幕验收。实板工具沿用现有 USB 协议，只有**唯一名称匹配且广播 HRS、可连接**的设备才发起测试连接；失败不会输出通过。不会改壁纸库、制造假测量或自动连接第一个陌生设备。

参考：用户已验证的 [nekro-heart-rate PC 实现](https://github.com/Egger0/nekro-heart-rate/blob/main/heart_rate_bridge.py)、[Bluetooth HRS](https://www.bluetooth.com/specifications/specs/heart-rate-service-1-0/)、[NimBLE GATT Client](https://mynewt.apache.org/latest/network/ble_hs/ble_gattc.html)；实际 API 以本机固定版本 SDK 的 headers/源码及编译为准。
