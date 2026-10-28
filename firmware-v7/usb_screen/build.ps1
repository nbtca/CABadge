param(
    [string]$BuildVariant = '',
    [AllowEmptyString()][string]$MemberName = 'Egger',
    [switch]$ConfigurationOnly
)
$ErrorActionPreference = 'Stop'
$firmware = Split-Path $PSScriptRoot
$repository = Split-Path $firmware
$buildRoot = if ($env:CABADGE_BUILD_ROOT) { [IO.Path]::GetFullPath($env:CABADGE_BUILD_ROOT) } else { 'F:\CABadgeBuild' }
$baseVersion = (Get-Content -Raw -LiteralPath (Join-Path $firmware 'version.json') | ConvertFrom-Json).version
if ($baseVersion -notmatch '^\d+\.\d+\.\d+(?:-[a-z0-9-]+)?$') { throw 'Invalid firmware version.' }
if ($BuildVariant -and ($BuildVariant -cnotmatch '^[a-z0-9][a-z0-9-]{0,47}$' -or $BuildVariant -match '^(default|con|prn|aux|nul|com[0-9]|lpt[0-9])$')) { throw 'Use a safe lowercase build variant (1..48 letters, digits, hyphens).' }
$version = if ($BuildVariant) { $baseVersion + '-' + $BuildVariant } else { $baseVersion }
if ($version.Length -gt 31) { throw 'The version including BuildVariant must fit the ESP-IDF 31-byte version limit.' }
if ($MemberName -notmatch '^[\x20-\x21\x23-\x5b\x5d-\x7e]{1,32}$' -or -not $MemberName.Trim()) { throw 'MemberName must be 1..32 printable ASCII characters without quotes or backslashes.' }
if ($MemberName -cne 'Egger' -and -not $BuildVariant) { throw 'A custom member requires an explicit BuildVariant.' }
if ($env:CABADGE_EAF_INTERPOLATE -eq '1' -and $env:CABADGE_EAF_EMOTE -eq '1') { throw 'Interpolation experiment requires the Direct EAF backend.' }
$flags = [ordered]@{}
foreach ($name in 'CABADGE_DISPLAY_PERF','CABADGE_EAF_EMOTE','CABADGE_EAF_INTERPOLATE','CABADGE_TRANSITION_CACHE_DEBUG') { $flags[$name] = ([Environment]::GetEnvironmentVariable($name) -eq '1') }
$checkoutId = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($repository.ToLowerInvariant()))).Substring(0,12).ToLowerInvariant()
$configuration = if ($BuildVariant) { $BuildVariant + '-' + [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($MemberName))).Substring(0,8).ToLowerInvariant() } else { 'default' }
if ($flags.CABADGE_DISPLAY_PERF) { $configuration += '-diagnostic' }
if ($flags.CABADGE_EAF_EMOTE) { $configuration += '-emote-prototype' }
if ($flags.CABADGE_EAF_INTERPOLATE) { $configuration += '-interpolation' }
if ($flags.CABADGE_TRANSITION_CACHE_DEBUG) { $configuration += '-cache-debug' }
$buildId = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
# Fresh sources per invocation prevent deleted files from surviving into a later build.
$stage = Join-Path $buildRoot "staging\$checkoutId\usb-screen-$configuration\build-$buildId"
$export = Join-Path $repository "outputs\builds\$checkoutId\cabadge-v$version-$configuration-$buildId"
$python = Join-Path $buildRoot 'venv\Scripts\python.exe'
if ($ConfigurationOnly) {
    [ordered]@{version=$version;base_version=$baseVersion;build_variant=$BuildVariant;member_name=$MemberName;build_root=$buildRoot;stage=$stage;export=$export;python=$python;build_flags=$flags} | ConvertTo-Json
    return
}
if (-not (Test-Path -LiteralPath $python)) { throw "Run tools/bootstrap.ps1 first; Python is missing: $python" }
$sourceCommit = (& git -C $repository rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source commit.' }
$sourceDirty = [bool](& git -C $repository status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source status.' }
$env:TEMP = $env:TMP = Join-Path $buildRoot 'temp'
$env:PLATFORMIO_CORE_DIR = Join-Path $buildRoot 'platformio\idf61'
$env:IDF_TOOLS_PATH = Join-Path $env:PLATFORMIO_CORE_DIR 'tools'
$env:IDF_COMPONENT_CACHE_PATH = Join-Path $env:PLATFORMIO_CORE_DIR '.cache\components'
$env:PIP_CACHE_DIR = Join-Path $buildRoot 'pip-cache'
New-Item -ItemType Directory -Force -Path $env:TEMP,$stage,(Join-Path $stage 'src\ui'),(Join-Path $stage 'components\lvgl') | Out-Null
# One manifest owns both staging and the recorded input fingerprints.
$copy = [ordered]@{}
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'device') -File -Recurse | Sort-Object FullName) { $copy[$file.FullName] = [IO.Path]::GetRelativePath((Join-Path $PSScriptRoot 'device'),$file.FullName) }
foreach ($name in 'ui_transition_cache.c','ui_transition_cache.h','ui_transition_compositor.c','ui_transition_compositor.h','badge_ui.c','badge_ui.h','heart_rate.h','battery_policy.h','wifi_panel.c','assets.c','apps.c','apps.h','motion.h','miner.c','miner.h','app_assets.c','grok.c','grok.h','grok_data.h','fluidbox.c','fluidbox.h','timetiles.c','timetiles.h') { $copy[(Join-Path $firmware "ui\$name")] = "src\ui\$name" }
foreach ($directory in 'timetiles','fluidbox') {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $firmware "ui\$directory") -File -Recurse | Sort-Object FullName) { $copy[$file.FullName] = 'src\ui\' + [IO.Path]::GetRelativePath((Join-Path $firmware 'ui'),$file.FullName) }
}
foreach ($name in 'protocol.h','shake_detector.h','wallpaper_timing.h','wallpaper_store.h','wallpaper_store.c','management_protocol.h','bridge_protocol.h','perf_stats.h') { $copy[(Join-Path $PSScriptRoot $name)] = "src\$name" }
$copy[(Join-Path $firmware 'lv_conf.h')] = 'lv_conf.h'
$copy[(Join-Path $firmware 'version.json')] = 'version.json'
$copy[$PSCommandPath] = 'cabadge-build.ps1'
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $firmware 'vendor\lvgl-9.4.0') -File -Recurse | Sort-Object FullName) { $copy[$file.FullName] = 'components\lvgl\lvgl\' + [IO.Path]::GetRelativePath((Join-Path $firmware 'vendor\lvgl-9.4.0'),$file.FullName) }
$inputHashes = [ordered]@{}
foreach ($path in $copy.Keys) {
    $relative = $copy[$path]
    $destination = Join-Path $stage $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    Copy-Item -LiteralPath $path -Destination $destination -Force
    $inputHashes[[IO.Path]::GetRelativePath($repository,$path).Replace('\','/')] = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
}
$sourceFiles = @($copy.Values)
$sourceFingerprint = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes(($inputHashes | ConvertTo-Json -Compress)))).ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $stage 'src/ui/cabadge_build.h'), "#ifndef CABADGE_BUILD_H`n#define CABADGE_BUILD_H`n#define UI_VERSION `"$version`"`n#define CABADGE_MEMBER_NAME `"$MemberName`"`n#endif`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $stage 'version.cmake'), "set(PROJECT_VER `"$version`")`n", [Text.UTF8Encoding]::new($false))
if ($env:CABADGE_EAF_EMOTE -eq '1') {
    Add-Content -LiteralPath (Join-Path $stage 'src\idf_component.yml') -Value '  espressif2022/esp_emote_gfx: "3.0.5"'
    Add-Content -LiteralPath (Join-Path $stage 'sdkconfig.defaults') -Value "`n# CONFIG_GFX_EAF_JPEG_DECODE_SUPPORT is not set`n# CONFIG_GFX_EAF_HEATSHRINK_SUPPORT is not set`n# CONFIG_GFX_FONT_FREETYPE_SUPPORT is not set"
}
$html = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'device\src\wallpaper.html'))
[IO.File]::WriteAllText((Join-Path $stage 'src\wallpaper_html.h'), 'static const unsigned char wallpaper_html[]={' + ($html -join ',') + ',0};')
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
& $python -m platformio run --project-dir $stage
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw 'USB screen firmware build failed.' }
New-Item -ItemType Directory -Force -Path $export | Out-Null
$hashes = [ordered]@{}
foreach ($name in 'bootloader.bin','partitions.bin','firmware.bin','firmware.elf') {
    Copy-Item -LiteralPath (Join-Path $env:PLATFORMIO_BUILD_DIR "usb-screen\$name") -Destination (Join-Path $export $name) -Force
    $hashes[$name] = (Get-FileHash -LiteralPath (Join-Path $export $name) -Algorithm SHA256).Hash.ToLowerInvariant()
}
Copy-Item -LiteralPath (Join-Path $env:PLATFORMIO_BUILD_DIR 'usb-screen\config\sdkconfig.h') -Destination (Join-Path $export 'sdkconfig.verified.h') -Force
$sources = [ordered]@{}
foreach ($relative in $sourceFiles) {
    $sources[$relative.Replace('\','/')] = (Get-FileHash -LiteralPath (Join-Path $stage $relative) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$sources['src/ui/cabadge_build.h'] = (Get-FileHash -LiteralPath (Join-Path $stage 'src/ui/cabadge_build.h') -Algorithm SHA256).Hash.ToLowerInvariant()
$sources['components/lvgl/lvgl/src/libs/lodepng/lodepng.c'] = (Get-FileHash -LiteralPath $pngSource -Algorithm SHA256).Hash.ToLowerInvariant()
New-Item -ItemType Directory -Force (Join-Path $export 'assets\wallpapers') | Out-Null
Copy-Item -LiteralPath (Join-Path $firmware 'assets\wallpapers\live_test.eaf') -Destination (Join-Path $export 'assets\wallpapers\live_test.eaf') -Force
Copy-Item -LiteralPath (Join-Path $firmware 'assets\wallpapers\live_test.eaf.timing') -Destination (Join-Path $export 'assets\wallpapers\live_test.eaf.timing') -Force
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
    render_optimization='ESP-IDF performance -O2; unified Flash wallpaper library; opaque 180px RGB565 thumbnail LRU; dynamic EAF home excluded from raster cache; other transition parameters unchanged'
    hardware_test='PENDING; not flashed or physically verified'
    product='CABadge'
    firmware_version=$version
    base_version=$baseVersion
    build_variant=$BuildVariant
    member_name=$MemberName
    source_commit=$sourceCommit
    source_dirty=$sourceDirty
    source_fingerprint=$sourceFingerprint
    build_flags=$flags
    sdk='ESP-IDF 6.1; PlatformIO espressif32 7.1.3; framework-espidf 4.60100.0'
    memory='Octal PSRAM 80 MHz; data cache 32 KB / 64-byte line; PSRAM boot memtest enabled; official SPI DMA queue depth 2, maximum chunk 32 rows'
    ui='Native horizontal wallpaper paging; blue member card; interruptible navigation; down control/up functions'
    management='P0 management protocol; UI v7: direct local management without access code; local HTTP status/control; BLE GATT Status/Control; no BLE image transfer'
    display='360x360 RGB565; semantic bridge v1; legacy USB pixel transport removed'
    actual_lcd='ST77916 QSPI 40MHz; two RGB565 PARTIAL draw buffers in PSRAM; shared 16-row internal staging prevents large automatic bounce buffers; official esp_lvgl_adapter 0.7.1 / esp_lcd_st77916 2.0.2, CS-held esp_lcd SPI DMA, queue depth 2 / 32-row chunks; GUI core1 priority4; TE unconnected, tear-free not guaranteed; CST816 touch; BL GPIO1 squared 0..1023/1024; CHG_ALLOW GPIO38 LOW'
    timetiles='TimeTiles reconstructed core/assets; large 330px clock composition, 11x13 grid/9px tiles; quiet back/more chrome, date calibration shortcut and modes/settings in More; larger countdown; exclusive effects canvas/full-height Code Rain; double cycles six saved themes, canvas/title triple resolves before double, shared shake event drives TIME TILES FLY; six alarms/private NVS/shared SNTP unchanged; no new task/display path; user manual physical acceptance pending'
    fluidbox='MIT eMUQI upstream 75d0c076; five modes/1200 particles; round 360px geometry; PSRAM BSS + two native RGB565 images; background sim/render tasks, GUI-owned presentation; SC7A20 acceleration only, no angular inertia; TE unavailable; USB type41/sub12 mode0..4/reset5'
    radio='Wi-Fi; existing NimBLE peripheral CABadge plus one outbound central; standard Heart Rate 0x180D/0x2A37 Notify, 30s stale, selected-peer reconnect 2..30s; no new task or BLE framework; physical test pending'
    motion='SC7A20 400Hz +/-2g HR/BDU, GUI reads latest XYZ at ~100Hz; hardware XYZ double-click 250mg, pulse80ms, interval80-480ms, click-only HPF; stable face-down1200ms sleep/face-up300ms auto-wake; Grok tilt/temporary shake and gravity-ball; no page/photo change; physical axes/flip passed, revised real-shell taps pending acceptance'
    wallpaper='USB and direct local HTTP RGB565 upload; 31-image library in expanded 0x8f0000 partition at 0x710000; legacy migration; open direct hotspot'
    eaf_emote_prototype=($env:CABADGE_EAF_EMOTE -eq '1')
    eaf_interpolation_experiment=($env:CABADGE_EAF_INTERPOLATE -eq '1')
    eaf_interpolation='Experimental USB type41/sub11 OFF/ON, nonpersistent and default OFF; adjacent native RGB565 50/50 channel blend; 2 extra PSRAM source frames (518400 bytes); one inserted frame per gap, split original duration, no version bump; source decode/blend/staging/submit/final-DMA/work and completed-frame interval measured separately; no optical flow, unchanged 40MHz QSPI/16-row DMA path'
    eaf='esp_lv_eaf_player 0.3.0; official block decoder -> single RGB565 frame -> shared Direct Compositor worker with per-resource absolute deadlines; JXWT v1 CONSTANT/PER_FRAME metadata (legacy 33333us); frozen LVGL image only during handoff; RLE8 up to 3MiB/360 frames/24 rows per block; contiguous shared library slots; streamed Flash upload; Flash mmap source; PSRAM-only decoder allocations; software JPEG disabled'
    hashes=$hashes
    staged_sources=$sources
    source_inputs=$inputHashes
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $export 'verification.json') -Encoding UTF8
Write-Host "Exported: $export"
