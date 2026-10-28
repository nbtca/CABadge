param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$buildRoot = if ($env:CABADGE_BUILD_ROOT) { [IO.Path]::GetFullPath($env:CABADGE_BUILD_ROOT) } else { 'F:\CABadgeBuild' }
$env:TEMP = $env:TMP = Join-Path $buildRoot 'temp'
$env:PIP_CACHE_DIR = Join-Path $buildRoot 'pip-cache'
New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
$venv = Join-Path $buildRoot 'venv'
$runtime = Join-Path $venv 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $runtime)) {
    & $Python -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create the build environment; use Python 3.10 or newer with venv.' }
}
& $runtime -m pip install -r (Join-Path $PSScriptRoot 'requirements-dev.txt')
if ($LASTEXITCODE -ne 0) { throw 'Build dependency installation failed.' }
Write-Host "Build environment ready: $runtime"
Write-Host 'LVGL 9.4.0 and the project fonts must be present in firmware-v7/vendor; see docs/BUILD.md.'
