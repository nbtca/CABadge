$ErrorActionPreference = 'Stop'
$env:TEMP = $env:TMP = 'F:\CABadgeBuild\temp'
$env:ZIG_GLOBAL_CACHE_DIR = 'F:\CABadgeBuild\zig-cache'
$env:ZIG_LOCAL_CACHE_DIR = 'F:\CABadgeBuild\zig-local-v7'
$env:PLATFORMIO_CORE_DIR = 'F:\CABadgeBuild\platformio'
New-Item -ItemType Directory -Force -Path $env:TEMP,$env:ZIG_GLOBAL_CACHE_DIR,$env:ZIG_LOCAL_CACHE_DIR | Out-Null
Set-Location $PSScriptRoot
$zig = Join-Path $PSScriptRoot '.venv/Lib/site-packages/ziglang/zig.exe'
$cmake = Join-Path $PSScriptRoot '.venv/Scripts/cmake.exe'
$ninja = Join-Path $PSScriptRoot '.venv/Scripts/ninja.exe'
& (Join-Path $PSScriptRoot '.venv/Scripts/python.exe') generate_assets.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake -S . -B build -G Ninja "-DCMAKE_C_COMPILER=$zig" '-DCMAKE_C_COMPILER_ARG1=cc' "-DCMAKE_MAKE_PROGRAM=$ninja" '-DCMAKE_BUILD_TYPE=Debug' '-DCMAKE_C_FLAGS=-O1'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build build -j 6
exit $LASTEXITCODE
