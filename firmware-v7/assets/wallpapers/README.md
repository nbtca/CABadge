# EAF 动态壁纸

7.9.14-eaf-direct 保留 `espressif/esp_lv_eaf_player 0.3.0` 的底层格式解析、RLE 解码和调色板转换，旁路 `lv_eaf` 对象/定时器。目标 33,333 us/帧（30 FPS），无限循环；首页逐帧由 Direct Compositor 输出，不经过 LVGL renderer。完成提交率不等于面板扫描刷新率。

`live_test.eaf` 是 360×360 测试动画。资源**不编进应用**；导入后作为 `EAF` 条目存入现有 `wallpaper` 分区的普通壁纸槽，和静态壁纸一样选择、删除、保存当前选择。开机不会重新导入已删除资源。

在项目根目录使用：

```powershell
python tools/upload_eaf.py firmware-v7/assets/wallpapers/live_test.eaf --port COM3
```

其他素材的格式、转换与 timing 要求见 [动态壁纸转换说明](../../../docs/EAF_TIMING.md)。转换工具不随仓库发布，上传工具保留。7.9.15起可用配套 timing metadata 保留原始播放节奏；没有 metadata 的旧 EAF 默认30FPS。

## 读取与生命周期

- 组件的文件路径 API 会完整读文件入 RAM，因此本项目使用 `esp_partition_mmap` 和 `esp_eaf_format_init`，直接引用 Flash 压缩数据，不注册第二套文件系统。
- 正常只保存一张 259,200 B RGB565 解码帧及 8,640 B 可复用索引块；帧索引、后端及解析临时缓冲使用 PSRAM。每帧调用官方 RLE 解码器，按调色板直接写入 LCD 字节序的当前帧；不复制完整帧到 LVGL buffer。构建时仅对固定版本组件关闭分配失败后回退内部 RAM 的路径；软件 JPEG 关闭。
- 干净首页实时播放；按下开始手势、菜单/应用、转场、息屏，以及状态入口/提示/HUD 显示时暂停并交回 LVGL；返回干净首页恢复。切换壁纸删除旧播放器后再解除旧 Flash 映射。暂停保留当前解码帧，不重复创建播放器。
- 动态首页不进入 Raster Cache。需要该背景的转场安全回退到暂停帧的 LVGL 路径，其他静态页面的 Direct Compositor 不变。
- 动态缩略图在首次选择后从已解码帧生成，纳入已有缩略图 LRU；尚未选择/已淘汰的动态项仍可按文字选择，不在后台另开播放器制作预览。
- USB 管理状态 `wallpaper.types` 给出 STATIC/EAF；`wallpaper.eaf` 提供 loaded/playing/frame/creates 和创建时堆差额，便于基本检查，不开启串口文本日志。

Flash 格式：保留旧静态 `JXWL` 记录，新增 `JXWE` EAF 记录；每槽大小、已有图片 ID、分区表均不变。7.9.13 起，大 EAF 使用连续空槽（一个 3 MiB 文件占 12 槽），静态图片仍用一槽。空间不足或没有足够连续空槽时拒绝上传，不移动或覆盖其他壁纸。EAF 上传按接收进度逐扇区擦除、分块写 Flash，校验后才发布记录，不申请完整文件大小的 PSRAM；未完成上传在重启扫描时清理。删除正在播放的 EAF 时先暂停源读取，再擦除与释放。旧版固件不识别 EAF 槽，回退旧版后请勿继续上传，以免旧版把动态槽当作空闲覆盖。


## 直出所有权与内存（7.9.14）

复用 `ui_transition_compositor` 的现有 4 KiB 栈任务和 `physical_display` 的两块 11,520 B DMA staging。worker 在上一帧 DMA 排空后才解码下一帧；每帧按 16 行分块搬运到 staging，共 23 次 draw 提交。无另一张完整中转 framebuffer，不把完整压缩动画读入 RAM。

暂停由 GUI 发送停止消息并等待 worker 确认，依赖真实 LCD drain 完成；再恢复 LVGL 显示所有权。在 GUI 锁内把同一当前帧原地转为 CPU RGB565，供冻结画面、缩略图和转场使用。恢复后 worker 直接解码下一帧为 LCD 字节序。真实页面及触摸仍由 LVGL 管理，worker 不调用 LVGL。动态背景涉及的旧安全转场路径保留。

USB `wallpaper.eaf.timing` 提供聚合耗时，无逐帧文本日志：`decode_us`、`staging_us`、`submit_us`、`dma_drain_us`、`interval_us`。submit 包含前一块 DMA 等待；dma_drain 仅为最后一块剩余等待，不能当成整帧 DMA 时间。统计在每次直出恢复时重置。

本轮短时实板：罗小黑 210 帧素材，最后 180 帧完成间隔平均 33.378 ms（约 29.96 次/秒），最差 39.319 ms。菜单/息屏暂停、恢复不重建、静态/动态切换经 USB 状态确认；没有长期压力及物理扫描率测量，无 TE 下视觉撕裂仍需实屏确认。详见主测试记录 R79。
