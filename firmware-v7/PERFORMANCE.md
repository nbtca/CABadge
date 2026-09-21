# 7.1.3 实屏复测

当前工作台安装7.1.3-lcd，采样流程、模式与指标沿用下文。已兼容7.1.2和7.1.3实体结果；新固件启用绘制缓存与-O2，没有改场景或把耗时转换成虚构FPS。与 `outputs/performance/20260921-144846-577025.json` 对比，保持无线/操作条件一致，同时观察撕裂是否返回。缓存构建在初始化/更换壁纸时，未计入持续动画场景。

# v7.1.2 板端性能采样

PCB c7c59dff、LVGL9.4.0、ESP32-S3 240MHz、PSRAM40MHz、QSPI40MHz。软件检查通过，实板采样待执行。

## 操作

关闭旧串口/工作台，双击 `firmware-v7/run.cmd`，安装7.1.2-lcd后按RESET。回到“界面预览”，选端口，保持“包含实体屏传输”勾选，点“性能采样”。工具关闭自己的PC预览，约14秒依次运行Carousel、抽屉、蓝色名片，各预热0.5秒、采样约4秒。不要触摸、上传或发手机控制；无线保持现状，不主动压测。

结果在 `outputs/performance/日期时间.json`；实体模式不覆盖 `firmware-v7/build/probe-latest.json` 或改变PC校准档。

取消勾选则暂时关闭背光并跳过QSPI，只测绘制，成功后更新无屏校准。旧7.1.0-preview/7.1.1-lcd只支持无屏模式。7.1.2实体端全帧渲染与PC的16行局部缓冲不同，校准只能作为预算近似。

## 指标

|字段|含义|
|---|---|
|lcd_transfer|true实体传输、false无屏|
|render_us_p50/p95/total|LVGL RENDER_START→RENDER_READY墙钟时间减同周期物理flush时间；包含调度干扰，不含事件之前的布局/业务处理|
|flush_us_p95|总线等待、窗口指令、字节序拷贝、DMA排队到完成的墙钟时间|
|copy_us_p95|字节序拷贝累计时间；包含于flush且可与另一DMA重叠，不可再次相加|
|frame_us_p95|RENDER_START→RENDER_READY总耗时；绘制和flush各自P95不能简单相加替代|
|frame_gap_ms_max|连续完成帧的最大间隔，不是纯传输时间|
|completed_fps|实体模式frames/elapsed_ms×1000，计完整SPI提交，含场景脚本停顿；不是面板扫描Hz|
|headless_present_fps|无屏完成刷新次数/时间，不是LCD FPS|
|flush_bytes_p95/total|矩形RGB565字节量；7.1.2整帧259200B，不按圆面积扣除|
|internal_free_min/psram_free_min|各完成帧采样点最小空闲内存，非瞬时峰值|
|wifi/ble标志|场景结束时状态，不是全程稳定性证明|
|estimated_qspi40_p95_frame_ms|仅无屏：max(33.333,render_p95_ms+bytes_p95/20000)，理想预算非实测|

主控使用单PSRAM全帧缓冲与两个内部32行DMA缓冲；最后DMA完成才通知LVGL flush完成。TE未连接，计时不能证明无撕裂。USB只回传结果，不要用旧像素镜像模式评估帧率。

## 协议与边界

BR_READY能力位4无屏、8实体；包39：0取消、1无屏、2实体；包40逐场景JSON。至少10帧、最多256样本，三个场景与结束标志、字段校验通过才接受；实体结果不能作为无屏校准。

开始时临时关闭减少动态并唤醒，结束/取消恢复原主页面、减少动态、息屏状态。采样期间暂停实体触摸读取；心跳断连超过5秒退出，上传中断本次采样。传输错误返回失败。不改变无线开关、不写设置/Flash测试扇区；充电允许LOW，实体背光仍约5%上限。

PC FPS只代表SDL更新率。无屏校准结合最坏场景绘制P95和PC脏区理想传输预算；并不模拟扫描、TE、实际总线间隙或MCU指令。
