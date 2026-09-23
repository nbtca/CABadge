$ErrorActionPreference = 'Stop'
$env:TEMP = $env:TMP = 'F:\CABadgeBuild\temp'
$env:PLATFORMIO_CORE_DIR = 'F:\CABadgeBuild\platformio\idf61'
$env:IDF_TOOLS_PATH = 'F:\CABadgeBuild\platformio\idf61\tools'
$env:IDF_COMPONENT_CACHE_PATH = 'F:\CABadgeBuild\platformio\idf61\.cache\components'
$env:PIP_CACHE_DIR = 'F:\CABadgeBuild\platformio\idf61\.cache\pip'
New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
$firmware = Split-Path $PSScriptRoot
$stage = 'F:\CABadgeBuild\staging\usb-screen-v7-idf61'
$export = [IO.Path]::GetFullPath((Join-Path $firmware '..\outputs\cabadge-v7.9.2-memory'))
if ($env:CABADGE_DISPLAY_PERF -eq '1') { $export = Join-Path $firmware '..\outputs\cabadge-v7.9.2-memory-diagnostic' }
New-Item -ItemType Directory -Force -Path $stage,$export,(Join-Path $stage 'src\ui'),(Join-Path $stage 'components\lvgl') | Out-Null
Copy-Item -Path (Join-Path $PSScriptRoot 'device\*') -Destination $stage -Recurse -Force
$html = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'device\src\wallpaper.html'))
[IO.File]::WriteAllText((Join-Path $stage 'src\wallpaper_html.h'), 'static const unsigned char wallpaper_html[]={' + ($html -join ',') + ',0};')
foreach ($name in 'ui_transition_cache.c','ui_transition_cache.h','ui_transition_compositor.c','ui_transition_compositor.h','badge_ui.c','badge_ui.h','wifi_panel.c','assets.c','apps.c','apps.h','miner.c','miner.h','app_assets.c','grok.c','grok.h','grok_data.h') {
    Copy-Item -LiteralPath (Join-Path $firmware "ui\$name") -Destination (Join-Path $stage "src\ui\$name") -Force
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'protocol.h') -Destination (Join-Path $stage 'src\protocol.h') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'shake_detector.h') -Destination (Join-Path $stage 'src\shake_detector.h') -Force
foreach ($name in 'wallpaper_store.h','wallpaper_store.c','management_protocol.h','bridge_protocol.h','perf_stats.h') { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $stage "src\$name") -Force }
Copy-Item -LiteralPath (Join-Path $firmware 'lv_conf.h') -Destination (Join-Path $stage 'lv_conf.h') -Force
& robocopy (Join-Path $firmware 'vendor\lvgl-9.4.0') (Join-Path $stage 'components\lvgl\lvgl') /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'Cannot stage LVGL sources.' }
$pngSource = Join-Path $stage 'components\lvgl\lvgl\src\libs\lodepng\lodepng.c'
$pngCode = [IO.File]::ReadAllText($pngSource)
if (-not $pngCode.Contains('CABadge adaptation: bounded PSRAM')) {
    $pngCode = $pngCode.Replace("`r`n","`n")
    $needle = "        size_t newsize = size + (p->allocsize >> 1u);`n        void * data = lodepng_realloc(p->data, newsize);"
    if (-not $pngCode.Contains($needle)) { throw 'LVGL PNG allocator changed; review bounded-growth patch.' }
    $replacement = @'
        size_t newsize = size + (p->allocsize >> 1u);
        void * data = lodepng_realloc(p->data, newsize);
        /* CABadge adaptation: bounded PSRAM may fit the required bytes but not
         * the vector's speculative 50% growth. Keep the original on failure. */
        if(!data && newsize != size) {
            newsize = size;
            data = lodepng_realloc(p->data, newsize);
        }
'@
    [IO.File]::WriteAllText($pngSource,$pngCode.Replace($needle,$replacement),[Text.UTF8Encoding]::new($false))
}
$osSource = Join-Path $stage 'components\lvgl\lvgl\src\osal\lv_freertos.c'
[IO.File]::WriteAllText($osSource, [IO.File]::ReadAllText($osSource).Replace('#include "atomic.h"', '#include "freertos/atomic.h"'), [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $stage 'sdkconfig.defaults') -Destination (Join-Path $stage 'sdkconfig.usb-screen') -Force
$env:PYTHONUTF8 = '1'
$env:LVGL_VERSION = '9.4.0'
$env:PLATFORMIO_BUILD_DIR = Join-Path $stage '.pio\build'
$ErrorActionPreference = 'Continue'
& (Join-Path $firmware '..\firmware\bringup\.venv\Scripts\python.exe') -m platformio run --project-dir $stage
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw 'USB screen firmware build failed.' }
$hashes = [ordered]@{}
foreach ($name in 'bootloader.bin','partitions.bin','firmware.bin','firmware.elf') {
    Copy-Item -LiteralPath (Join-Path $env:PLATFORMIO_BUILD_DIR "usb-screen\$name") -Destination (Join-Path $export $name) -Force
    $hashes[$name] = (Get-FileHash -LiteralPath (Join-Path $export $name) -Algorithm SHA256).Hash.ToLowerInvariant()
}
Copy-Item -LiteralPath (Join-Path $env:PLATFORMIO_BUILD_DIR 'usb-screen\config\sdkconfig.h') -Destination (Join-Path $export 'sdkconfig.verified.h') -Force
$sources = [ordered]@{}
foreach ($name in 'ui_transition_cache.c','ui_transition_cache.h','ui_transition_compositor.c','ui_transition_compositor.h','badge_ui.c','badge_ui.h','wifi_panel.c','assets.c','apps.c','apps.h','miner.c','miner.h','app_assets.c','grok.c','grok.h','grok_data.h') {
    $sources[$name] = (Get-FileHash -LiteralPath (Join-Path $stage "src\ui\$name") -Algorithm SHA256).Hash.ToLowerInvariant()
}
$sources['lodepng.c'] = (Get-FileHash -LiteralPath $pngSource -Algorithm SHA256).Hash.ToLowerInvariant()
$sources['app_service.c'] = (Get-FileHash -LiteralPath (Join-Path $stage 'src\app_service.c') -Algorithm SHA256).Hash.ToLowerInvariant()
[ordered]@{
    pcb_sha256='c7c59dff5e6f2065a2157a81414d9d21c5cb1196a7b277656e15007753801a8d'
    purpose='Shared v7 LVGL UI with physical ST77916 LCD, touch and USB services'
    performance='Adapter 0.7.1: aggregate draw-to-DMA-complete time available; old queue/wire/copy subphase metrics unavailable. Modes 1/2 legacy three scenes; 3 six scenes with DMA callback timing and per-frame bytes; 4 manual touch for 8 seconds; mode 0 disables profiling. Detailed probe fences each frame; runtime probe measures normal overlapped UI. SPI completions are not panel scan Hz; input-to-next-flush is not photon latency'
    transition_cache='GUI-owned up to 3MiB PSRAM single LCD-native RGB565 cache; navigation prewarm, visual revisions, hot priority/LRU and active pins; Direct or real-widget fallback; Flash L2 disabled (no suitable reserved partition)'
    transition_compositor='Exclusive physical display ownership; existing animation offsets; stride-aware 16-row packing; shared two 11520-byte internal DMA blocks used by LVGL and compositor; true DMA drain before release; no per-frame LVGL image rendering; TE unavailable'
    diagnostic_transition_perf=($env:CABADGE_DISPLAY_PERF -eq '1')
    diagnostic_display_perf=($env:CABADGE_DISPLAY_PERF -eq '1')
    transition_cache_enabled=$true
    transition_cache_copies=1
    transition_cache_l2_bytes=0
    diagnostic_cache_debug=($env:CABADGE_TRANSITION_CACHE_DEBUG -eq '1')
    render_optimization='ESP-IDF performance -O2; Flash-backed RGB565 built-in wallpaper; opaque 180px RGB565 thumbnails cached on source change; member page uses shared transition cache only; native snap scrolling without per-frame scale'
    hardware_test='PENDING; not flashed or physically verified'
    product='CABadge'
    firmware_version='7.9.2-memory'
    sdk='ESP-IDF 6.1; PlatformIO espressif32 7.1.3; framework-espidf 4.60100.0'
    memory='Octal PSRAM 80 MHz; data cache 32 KB / 64-byte line; PSRAM boot memtest enabled; official SPI DMA queue depth 2, maximum chunk 32 rows'
    ui='Native horizontal wallpaper paging; blue member card; interruptible navigation; down control/up functions'
    management='P0 management protocol; UI v7: direct local management without access code; local HTTP status/control; BLE GATT Status/Control; no BLE image transfer'
    display='360x360 RGB565; semantic bridge v1; legacy USB pixel transport removed'
    actual_lcd='ST77916 QSPI 40MHz; two RGB565 PARTIAL draw buffers in PSRAM; shared 16-row internal staging prevents large automatic bounce buffers; official esp_lvgl_adapter 0.7.1 / esp_lcd_st77916 2.0.2, CS-held esp_lcd SPI DMA, queue depth 2 / 32-row chunks; GUI core1 priority4; TE unconnected, tear-free not guaranteed; CST816 touch; BL GPIO1 squared 0..1023/1024; CHG_ALLOW GPIO38 LOW'
    radio='Wi-Fi; NimBLE peripheral CABadge plus central scan/connect; one inbound and one outbound BLE link'
    motion='SC7A20 50Hz; shake wakes only while screen asleep; no navigation or photo change'
    wallpaper='USB and direct local HTTP RGB565 upload; 31-image library in expanded 0x8f0000 partition at 0x710000; legacy migration; open direct hotspot'
    hashes=$hashes
    shared_ui_sources=$sources
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $export 'verification.json') -Encoding UTF8
Write-Host "Exported: $export"
