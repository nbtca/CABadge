$ErrorActionPreference = 'Stop'
$env:TEMP = $env:TMP = 'F:\CABadgeBuild\temp'
$env:PLATFORMIO_CORE_DIR = 'F:\CABadgeBuild\platformio'
New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
$firmware = Split-Path $PSScriptRoot
$stage = 'F:\CABadgeBuild\staging\usb-screen-v7'
$export = [IO.Path]::GetFullPath((Join-Path $firmware '..\outputs\cabadge-v7-mem-20260921'))
New-Item -ItemType Directory -Force -Path $stage,$export,(Join-Path $stage 'src\ui'),(Join-Path $stage 'components\lvgl') | Out-Null
Copy-Item -Path (Join-Path $PSScriptRoot 'device\*') -Destination $stage -Recurse -Force
$html = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'device\src\wallpaper.html'))
[IO.File]::WriteAllText((Join-Path $stage 'src\wallpaper_html.h'), 'static const unsigned char wallpaper_html[]={' + ($html -join ',') + ',0};')
foreach ($name in 'badge_ui.c','badge_ui.h','wifi_panel.c','assets.c') {
    Copy-Item -LiteralPath (Join-Path $firmware "ui\$name") -Destination (Join-Path $stage "src\ui\$name") -Force
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'protocol.h') -Destination (Join-Path $stage 'src\protocol.h') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'shake_detector.h') -Destination (Join-Path $stage 'src\shake_detector.h') -Force
foreach ($name in 'wallpaper_store.h','wallpaper_store.c','management_protocol.h','bridge_protocol.h','perf_stats.h') { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $stage "src\$name") -Force }
Copy-Item -LiteralPath (Join-Path $firmware 'lv_conf.h') -Destination (Join-Path $stage 'lv_conf.h') -Force
& robocopy (Join-Path $firmware 'vendor\lvgl-9.4.0') (Join-Path $stage 'components\lvgl\lvgl') /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'Cannot stage LVGL sources.' }
Copy-Item -LiteralPath (Join-Path $stage 'sdkconfig.defaults') -Destination (Join-Path $stage 'sdkconfig.usb-screen') -Force
$env:PYTHONUTF8 = '1'
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
foreach ($name in 'badge_ui.c','badge_ui.h','wifi_panel.c','assets.c') {
    $sources[$name] = (Get-FileHash -LiteralPath (Join-Path $stage "src\ui\$name") -Algorithm SHA256).Hash.ToLowerInvariant()
}
[ordered]@{
    pcb_sha256='c7c59dff5e6f2065a2157a81414d9d21c5cb1196a7b277656e15007753801a8d'
    purpose='Shared v7 LVGL UI with physical ST77916 LCD, touch and USB services'
    performance='Modes 1/2 legacy three scenes; 3 six scenes with DMA callback timing and per-frame bytes; 4 manual touch for 8 seconds; mode 0 disables profiling. Detailed probe fences each frame; runtime probe measures normal overlapped UI. SPI completions are not panel scan Hz; input-to-next-flush is not photon latency'
    render_optimization='ESP-IDF performance -O2; opaque RGB565 wallpaper cached in RAM; opaque 180px RGB565 thumbnails cached on source change; static member page cached as RGB565; native snap scrolling without per-frame scale'
    hardware_test='PENDING; not flashed or physically verified'
    product='CABadge'
    firmware_version='7.3.7-mem'
    memory='Octal PSRAM 80 MHz; data cache 32 KB / 64-byte line; PSRAM boot memtest enabled; internal DMA 2x32 rows'
    ui='Native horizontal wallpaper paging; blue member card; interruptible navigation; down control/up functions'
    management='P0 management protocol; UI v7: explicit 5-minute 128-bit authorization; local HTTP status/control; BLE GATT Status/Control/Session; no BLE image transfer'
    display='360x360 RGB565; semantic bridge v1; legacy USB pixel transport removed'
    actual_lcd='ST77916 QSPI 40MHz; two RGB565 PARTIAL draw buffers in PSRAM; core1 worker CS-held DMA with two internal 32-row buffers; TE unconnected, tear-free not guaranteed; CST816 touch; BL GPIO1 capped at 51/1024; CHG_ALLOW GPIO38 LOW'
    radio='Wi-Fi; NimBLE peripheral CABadge plus central scan/connect; one inbound and one outbound BLE link'
    motion='SC7A20 50Hz; shake wakes only while screen asleep; no navigation or photo change'
    wallpaper='USB and authenticated local HTTP RGB565 upload; 1MB dual-slot partition at 0x710000; persistent default reset; explicit WPA2 direct hotspot'
    hashes=$hashes
    shared_ui_sources=$sources
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $export 'verification.json') -Encoding UTF8
Write-Host "Exported: $export"
