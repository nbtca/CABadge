> 历史设计/阶段记录：保留当时版本、方案和测量值，不作为当前安装或验收说明。当前正式版为 v1.1.0 / 7.9.2-memory，参见[文档索引](../docs/INDEX.md)。

# 官方显示适配层迁移

候选：7.7.0-adapter；回退：outputs/cabadge-v7-entry-20260923/firmware.bin。

- 依赖清单与锁文件固定 esp_lvgl_adapter 0.7.1 / esp_lcd_st77916 2.0.2，仍使用本地 LVGL 9.4.0；不混入第二份 LVGL。
- Adapter 创建显示与双PSRAM PARTIAL缓冲，负责GUI任务和tick；项目不再另起lv_timer_handler循环。
- GUI任务在core1，优先级4，栈12288B；业务结果通过5ms GUI定时器应用。网络/文件后台工作保持原结构。启动阶段创建UI后才启动Adapter，因此没有并发UI初始化。
- esp_lcd负责QSPI40MHz与DMA：32行最大传输、队列2，分块间保持CS，仅末块通知完成。使用屏厂原初始化表，不使用通用默认表。
- 自定义draw hook只做统计并转交官方panel，RGB565字节交换由Adapter执行一次。完成ISR先通知Adapter，再发布统计；下一笔提交前确保上笔SPI事务完全回收。LVGL的flush_wait使用官方IO排空等待，避免LVGL9.4默认空转。
- 触摸由Adapter注册，custom_touch_read保留原CST816D 50Hz有界读取与错误取消处理，共用原I2C总线。
- 保留现有可关闭FPS显示。LCD指标仍是传输完成率，不是玻璃扫描Hz；旧queue/wire/copy细分不可用，详细探针标明dma_subphase_available=false。总传屏时间口径为draw开始至完成ISR，包含入队及驱动内部搬运，不能直接与旧字段作百分比归因。GUI计时通过链接包装lv_timer_handler并扣除服务轮询墙钟时间，不改官方源码。
- TE未连，不承诺防扫描撕裂。实屏颜色、触控、过渡、撕裂须用户确认。

构建：powershell -NoProfile -ExecutionPolicy Bypass -File firmware-v7/usb_screen/build.ps1

检查：python -B firmware-v7/test_physical_display.py；python -B firmware-v7/test_async_flush.py；python -B firmware-v7/test_apps_menu.py。

回退只写应用区0x10000，不擦除NVS/壁纸。迁移前源码保存在 outputs/cabadge-v7-adapter-20260923/source-before；不要用git reset覆盖此前用户改动。

当前状态：7.7.0-adapter 已构建、烧录并读回；用户反馈本次实板显示和触摸正常、无明显异常。显示统计及口径见 `../outputs/display-audit-20260923/AUDIT.md`，长期稳定性和真实手指滚动仍待测。
