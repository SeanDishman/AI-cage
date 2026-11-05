$ErrorActionPreference = 'Stop'
$uiRoot = $PSScriptRoot
$binary = Join-Path $uiRoot 'dist\AICage-win32-x64\AICage Desktop.exe'
if (-not (Test-Path -LiteralPath $binary)) { & (Join-Path $uiRoot 'build-desktop.ps1') }
if (-not (Test-Path -LiteralPath (Join-Path $uiRoot '..\build\AICage.exe'))) { throw 'Build the sandbox first with scripts\build.ps1.' }
# Electron owns the requested native window; its C++ helper stays hidden.
Start-Process -FilePath $binary -WorkingDirectory $uiRoot
