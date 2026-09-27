# EAF emote prototype — 2026-09-27

结论：保留7.9.14-eaf-direct默认后端。原型已编译/烧录/短测，之后恢复原正式BIN；没有改原组件算法，没有改显示时钟、buffer、queue或动画参数。

## 可复现构建

```powershell
$env:CABADGE_EAF_EMOTE='1'
powershell -NoProfile -ExecutionPolicy Bypass -File firmware-v7/usb_screen/build.ps1
Remove-Item Env:CABADGE_EAF_EMOTE
```

仅可选构建的暂存manifest加入 `espressif2022/esp_emote_gfx: 3.0.5`，CMake选择 `eaf_emote.c` 代替 `eaf_wallpaper.c`；默认无新增依赖。输出 `outputs/cabadge-v7.9.14-eaf-direct-emote-prototype`，不得当作正式发布。版本主字符串仍7.9.14；USB `wallpaper.eaf.backend` 明确标记prototype。

## 核对的实际API

Registry: https://components.espressif.com/components/espressif2022/esp_emote_gfx/versions/3.0.5/readme

发布包在暂存managed_components/espressif2022__esp_emote_gfx；关键头/源与官方repo提交349c189a03357936def4759ff640843139c53530逐文件归一换行比较一致。

- include/core/gfx_disp.h：gfx_disp_config_t.buffers允许外部小块缓冲；flush_cb坐标右下角排他，gfx_disp_flush_ready同步完成。
- src/core/display/gfx_render.c / gfx_render_part_area：分块绘制，但每块提交后立即等待WAIT_FLUSH_DONE；配置两个buffer也不自动重叠下一块绘制与当前DMA。
- src/widget/anim/gfx_anim.c：gfx_anim_get_pixel_buffer_size仅width×block_height索引块；palette在Internal，draw_animation缓存最近解码块。gfx_anim_set_segment指定FPS和无限循环；gfx_anim_stop后gfx_anim_start重置到start_frame。
- src/widget/anim/gfx_anim_decoder_eaf.c：使用Flash指针，不复制整文件；get_total_frames把底层数量减1，而上层再按数量设置end=total-1，存在最后一帧被排除风险（本210帧素材范围实际会被夹到0..208）。原型未修改官方算法。
- src/core/runtime/gfx_core.c：独立gfx_render任务，原型用官方默认7168字节栈、priority4、无affinity。不新建触摸栈。
- EAF当前文件没有逐帧时长；API段落FPS不是从该文件恢复的原生视频timing。

## 所有权与内存

GUI沿用physical_display_transition_acquire/release。官方draw直接写原有两个11520字节Internal DMA staging，无整帧RGB565中转。flush调用原ST77916后真实drain再ready，worker不调用LVGL。

暂停时GUI取gfx recursive lock，等待整帧flush结束；生成一张259200B CPU-endian暂停图给现有LVGL，释放LCD。暂停期间一直由同一GUI任务持有gfx lock，防止emote写入已经归还的共享staging。恢复时acquire LCD、隐藏并释放暂停图、start并unlock。删除时先在锁内删obj/display，再解锁并deinit任务，避免已释放/被复用buffer仍被访问。内存不足时不播放；暂停图分配失败隐藏失效图，属于原型尚未完善的降级体验。

只有播放态省全帧；菜单及转场交接仍需要暂停图，暂停峰值不具有同样节省。Cache idle检查扩展为任何physical direct owner，默认Direct语义不变。日志设GFX_LOG_LEVEL_NONE，不污染USB协议。

## 同素材约6秒短对比

ID18，罗小黑上半段2470354B，CRC一致；各自目标30FPS，无大规模循环。

| 指标 | Direct | emote prototype |
|---|---:|---:|
| 完成帧数 | 172 | 137 |
| 平均完成间隔ms | 33.491 | 40.889 |
| 最差完成间隔ms | 52.118 | 52.888 |
| 完成FPS | 29.86 | 24.46 |
| decode / render平均ms | decode 11.720 | decode+draw 11.673 |
| 最大decode / render ms | 26.679 | 15.426 |
| staging copy ms | 7.043 | 无该整帧搬运阶段 |
| submit累计ms | 11.092 | 9.499 |
| drain等待ms | 最后一块0.305 | 所有块累计14.368 |
| 每帧draw次数 | 23 | 23 |
| 后端PSRAM创建差额B（播放态） | 272572 | 20436 |
| Internal创建差额B | 164 | 3132 |
| 播放完整帧B | 259200 | 0 |

解码指标范围不同；emote为官方draw累计时间，包含块解码与调色板绘制，不含全部定时器/prepare开销。Direct submit包含前块DMA等待，两个drain指标范围不同，不能直接把0.305对14.368当总DMA比较。堆差额为创建时观测，不是整机峰值；Direct任务为已有复用任务，emote另建任务。原型静态RAM60760B，对比59416B多1344B。PSRAM暂停图还需259200B。

test_eaf_emote.py运行实际flush函数，验证完成顺序及冻结图stride/字节序。编译最终SUCCESS80.12s、无warning/error匹配，烧录hash通过。USB显示播放frame_bytes=0、菜单259200、恢复creates不变；静态切换删除context、重新选择恢复，零flush错误。没有人工目视、物理扫描率或长期内存测试。短样本不能外推所有EAF。

证据：F:/CABadgeBuild/logs/emote-compare-direct.json、emote-compare-prototype.json、emote-restored-direct.json、build-emote-prototype-final2.log、flash-emote-prototype-final.log、flash-restore-direct-after-emote.log；产物comparison.json与dependencies.lock。
