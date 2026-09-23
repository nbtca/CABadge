# 7.3.10-mem 内部 RAM 占用核算（2026-09-23）

来源：本版 ELF/map、sdkconfig.verified.h、本地 IDF 5.5.0 源码及 final-readback.json。单位 KiB=1024 B。没有运行逐次 heap allocation trace，不能给出覆盖所有动态分配的模块总排名。

## 当前实测总量

最终快照 Wi-Fi 关闭、BLE 广播开启：内部堆已分配 249480 B（243.63 KiB），空闲 74419 B（72.67 KiB），最大连续块 31744 B（31 KiB）。本次启动以来最低空闲 49899 B（48.73 KiB）。它们不是整颗芯片 SRAM 的总账。

ELF 静态 DRAM：data 21072 B + bss 32856 B = 53928 B（52.66 KiB）。IRAM text 122719 B（119.84 KiB），另有 vectors 1028 B、段尾对齐157 B。代码段不能当作可直接搬到 PSRAM 的普通数据缓冲。

## 已核实的较大单项（不是所有模块的完整排名）

|项目|字节|KiB|依据 / 边界|
|---|---:|---:|---|
|LCD DMA 两块暂存|46080|45.00|physical_display.c:78，2×360×32×2；不含驱动描述符|
|main / GUI 任务栈|12288|12.00|实际构建配置；整块预留|
|HTTP 服务任务栈|8192|8.00|wallpaper_service.c:256；任务已存在|
|三组性能采样统计数组|6216|6.07|main.c:61，ELF每组2072 B；关闭采样仍常驻；计入静态DRAM|
|NimBLE host 任务栈|4096|4.00|实际构建配置；主机动态内存迁移不等于栈迁移|
|Wi-Fi g_cnxMgr 全局对象|3880|3.79|map: libnet80211.a(wl_cnx.o)；只是一个对象，不是Wi-Fi总量|
|esp_timer 任务栈|3584|3.50|实际构建配置|
|LCD worker 任务栈|3072|3.00|display_worker.c:23|
|lwIP tiT 任务栈|3072|3.00|实际构建配置|
|双核中断栈合计|3072|3.00|ELF port_IntStack；计入静态DRAM|
|系统事件任务栈|2304|2.25|实际构建配置|
|FreeRTOS Timer 任务栈|2048|2.00|实际构建配置|
|IDLE0、IDLE1任务栈|各1536|各1.50|实际构建配置|
|ipc0、ipc1任务栈|各1280|各1.25|实际构建配置|

任务栈数字不含TCB/队列等管理开销。以上动态项已包含在内部堆总量内，静态对象已包含在静态DRAM总量内，不可重复相加。

## 栈余量与待核实项

本次快照的历史最小剩余栈：main 5168 B、httpd 7008 B、nimble_host 2048 B、badge_lcd 2048 B、esp_timer 3056 B、tiT 2456 B。剩余栈不是已经返还的可用堆；HTTP真实上传/异常路径尚未覆盖，不能据此直接压缩任务栈。

USB driver 配置 TX16384 + RX4096 = 20480 B；本地 usb_serial_jtag.c 调用 xRingbufferCreate，ringbuf.c 使用普通 malloc。当前大块分配优先PSRAM且可回退，未记录实际指针位置，不能将20 KiB列作已确认内部占用。驱动对象本身显式分配内部内存。

Wi-Fi/蓝牙控制器动态缓冲、LVGL小对象、网络连接和驱动管理开销尚无完整逐模块分配归属。不得用剩余总堆倒推某一个模块，也不得将配置上限当实测常驻量。

双LVGL绘图缓冲 518400 B（506.25 KiB）明确在PSRAM；USB协议包/scratch、管理状态缓存和NimBLE host普通动态分配也已使用PSRAM，后者不包括控制器和主机任务栈。最终PSRAM空闲6523064 B（6.22 MiB）。

## 下一候选

性能采样统计数组的6216 B可评估按需分配到PSRAM；本轮排名没有实施此改动。LCD DMA暂存是显式内部DMA缓冲，不能当普通数组搬走。USB 20 KiB先确认分配位置再判断内部RAM收益。更细无线/GUI归属需要额外堆追踪，不伪造数字。
