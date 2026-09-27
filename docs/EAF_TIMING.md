# EAF per-wallpaper timing (7.9.15)

Official EAF payload is unchanged. CABadge stores JXWT v1 at byte64 of the existing 4KiB wallpaper record header. Little-endian 28-byte header: magic[4], version u16, mode u16 (1=CONSTANT, 2=PER_FRAME), frame_count u32, default/frame_period_us u32, table_bytes u32, EAF CRC32 u32, timing CRC32 u32. The table immediately follows the header: u16 milliseconds per frame. Timing CRC covers header bytes0..23 plus table. Maximum360 frames/748 bytes. No partition change.

## 素材转换说明

转换工具仅在本地维护，不随仓库发布。使用兼容的 EAF 转换器将 GIF/视频转换为不透明 360×360、8-bit 调色板、RLE 分块（每块最多24行）的 EAF；当前固件不接受 JPEG/Huffman/透明编码。每个文件最多3MiB、360帧，超过需分段；每段是独立壁纸，不会自动跨文件连播。

视频可按20或30FPS抽帧，GIF应保留每帧duration。等时长保存CONSTANT，不等时长保存PER_FRAME；配套生成同名 `.eaf.timing`，格式见上文。缺失/零时长回退33333us，非零时长限制10ms至60s。图片可选择完整保留加黑边或居中裁切；圆屏边缘仍会裁切，视频音频不保留。

没有转换工具时，可以先使用仓库自带的 `firmware-v7/assets/wallpapers/live_test.eaf` 及其 timing 文件验证播放。已有 `.eaf` 可直接用以下上传工具，缺少 timing 时按旧版30FPS默认节奏播放。

`python tools/upload_eaf.py F:/CABadgeBuild/wallpapers/name.eaf --port COM3`

Automatically validates/uploads the adjacent .eaf.timing using the existing USB begin command extension. Old 8-byte begin remains supported. Pixel CRC plus timing CRC identifies a resource; different timing is a separate upload, never an in-place destructive rewrite. Old clients/files without timing retain33333us. Static uploads are unchanged. Metadata is committed before the original record commit marker; interrupted uploads use existing cleanup. Selection/NVS stores the existing wallpaper ID. Metadata remains borrowed from the immutable Flash mapping until compositor stop/join before unmap.

Direct stream prepare returns the duration of the decoded frame. Worker adds it to the absolute deadline, subtracts work already spent and waits only the remaining time. An overrun preserves sequential frames and rebases the deadline, avoiding busy catch-up; timing targets faster than the hardware are not guaranteed. Resume resets the clock and starts at the retained current frame. No renderer/DMA/ownership changes. Only the legacy fallback remains fixed30 in the formal backend; the untouched optional emote prototype still uses30FPS.

Software check: `python firmware-v7/test_eaf_timing.py`. Board results: outputs/cabadge-v7.9.15-eaf-timing/basic-check.json and root 首板测试记录.md R81. Fixed30 and variable80/180/420ms, pause/resume, static switch, reboot persistence and legacy playback passed USB checks; visual artifacts/physical gestures require user confirmation. Test resources removed; originalSTATIC ID9 restored.

Luo Xiaohei: original video nominal30FPS (actual33/34ms timestamps). The existing conversion is20FPS,210 frames/10.5s per EAF part. Matching50,000us sidecars are in F:/CABadgeBuild/wallpapers/luoxiaohei; old deviceID18 was not rewritten and still uses legacy30 until reimported with metadata. EAF alone cannot recover the original timing.


