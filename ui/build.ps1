param([switch]$Tests)
$ErrorActionPreference = 'Stop'
$uiRoot = $PSScriptRoot
cmake -S $uiRoot -B (Join-Path $uiRoot 'build') -G Ninja -DCMAKE_BUILD_TYPE=Release "-DBUILD_TESTING=$($Tests.IsPresent.ToString().ToUpperInvariant())"
if ($LASTEXITCODE -ne 0) { throw 'UI configure failed' }
cmake --build (Join-Path $uiRoot 'build') -j 4
if ($LASTEXITCODE -ne 0) { throw 'UI build failed' }
