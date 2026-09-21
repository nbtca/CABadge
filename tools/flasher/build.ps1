$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$flasherCache = Join-Path $env:LOCALAPPDATA 'JXBadge\flasher-build'
$flasherDist = Join-Path $projectRoot 'outputs\flasher'
New-Item -ItemType Directory -Force -Path $flasherCache, $flasherDist | Out-Null
python -m PyInstaller --noconfirm --windowed --onedir --name JXBadgeFlasher --collect-all esptool --collect-submodules serial.tools --exclude-module IPython --exclude-module pytest --exclude-module numpy --exclude-module scipy --exclude-module matplotlib --exclude-module pandas --distpath $flasherDist --workpath (Join-Path $flasherCache 'work') --specpath $flasherCache (Join-Path $PSScriptRoot 'app.py')
if ($LASTEXITCODE -ne 0) { throw 'Application packaging failed.' }
$releaseRoot = Join-Path $flasherDist 'JXBadgeFlasher'
foreach ($firmware in @('wifi-connect-20260919', 'headless-bringup-20260919', 'bringup-20260919', 'display-bringup-20260919')) {
    $destination = Join-Path $releaseRoot "firmwares\$firmware"
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($name in @('bootloader.bin', 'partitions.bin', 'firmware.bin', 'verification.json')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot "outputs\$firmware\$name") -Destination (Join-Path $destination $name) -Force
    }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $releaseRoot 'README.md') -Force
Write-Host "Application: $(Join-Path $releaseRoot 'JXBadgeFlasher.exe')"
