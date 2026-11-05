param([string]$Distro = 'ShannonDocker')
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $projectRoot
& cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'Windows configure failed' }
& cmake --build build
if ($LASTEXITCODE -ne 0) { throw 'Windows build failed' }
$linuxRoot = (& wsl -d $Distro --exec wslpath -a $projectRoot).Trim()
& wsl -d $Distro -u root --exec cmake -S $linuxRoot -B "$linuxRoot/build-linux" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'Linux configure failed' }
& wsl -d $Distro -u root --exec cmake --build "$linuxRoot/build-linux" -j 4
if ($LASTEXITCODE -ne 0) { throw 'Linux build failed' }
[IO.File]::WriteAllText((Join-Path $projectRoot 'wsl-distro.txt'), "$Distro`n")
Write-Host 'Built. Run .\build\AICage.exe setup, then start.'
