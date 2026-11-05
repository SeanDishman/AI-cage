$ErrorActionPreference = 'Stop'
$uiRoot = $PSScriptRoot
& (Join-Path $uiRoot 'build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'C++ desktop helper build failed.' }
Push-Location -LiteralPath $uiRoot
try {
    npm.cmd ci --ignore-scripts --no-audit --no-fund
    if ($LASTEXITCODE -ne 0) { throw 'Desktop dependencies failed to install.' }
    node node_modules/electron/install.js
    if ($LASTEXITCODE -ne 0) { throw 'Electron download failed.' }
    npm.cmd run package:desktop
    if ($LASTEXITCODE -ne 0) { throw 'Desktop packaging failed.' }
} finally { Pop-Location }
