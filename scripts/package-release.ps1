# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force
$buildDir = Join-Path $projectDir 'build/Release'
$releaseDir = Join-Path $projectDir 'release'

$asi = Join-Path $buildDir 'StillWakesTheDeepHeadTracking.asi'
if (-not (Test-Path $asi)) {
    throw "Built .asi not found at $asi. Run 'pixi run build' first."
}

$version = (Select-String -Path (Join-Path $projectDir 'CMakeLists.txt') -Pattern 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' | Select-Object -First 1).Matches[0].Groups[1].Value
if (-not $version) { throw 'Could not read version from CMakeLists.txt' }

if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Path $releaseDir | Out-Null

# Stage installer ZIP contents in a temp folder
$stage = Join-Path $env:TEMP "swtd-ht-stage-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stage | Out-Null

# Launcher manifest (lopari ingests this). Ship it at the ZIP root and stamp
# the real release version (the committed copy stays at 0.0.0).
$modManifest = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $modManifest)) { throw "launcher-manifest.json not found at $modManifest" }
$manifest = Get-Content -Raw $modManifest | ConvertFrom-Json
$manifest.mod_info.version = $version
$manifest | ConvertTo-Json -Depth 10 | Set-Content -Path (Join-Path $stage 'launcher-manifest.json') -Encoding UTF8

# Plugin payload
$plugins = New-Item -ItemType Directory -Path (Join-Path $stage 'plugins')
Copy-Item -Force $asi (Join-Path $plugins.FullName 'StillWakesTheDeepHeadTracking.asi')

# Vendor (loader)
$vendorSrc = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorDst = New-Item -ItemType Directory -Path (Join-Path $stage 'vendor/ultimate-asi-loader')
if (Test-Path $vendorSrc) {
    Copy-Item -Force (Join-Path $vendorSrc '*') $vendorDst.FullName -Recurse
} else {
    Write-Warning 'vendor/ultimate-asi-loader missing - the installer will hard-error at runtime. Run pixi run update-deps.'
}

# Installer scripts + game-detection shim. Copy-SharedBundle stages the whole
# shim set (find-game.ps1, GamePathDetection.psm1, games.json, the uninstall
# body's cecil-marker-check.ps1); hand-copying only find-game.ps1 + games.json
# shipped an installer that aborted with "Installer ZIP is corrupt" because the
# module find-game.ps1 imports was missing from shared/.
Copy-Item -Force (Join-Path $projectDir 'scripts/install.cmd') $stage
Copy-Item -Force (Join-Path $projectDir 'scripts/uninstall.cmd') $stage
Copy-SharedBundle -StagingDir $stage

# Docs
foreach ($f in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -Force (Join-Path $projectDir $f) $stage
}

$installerZip = Join-Path $releaseDir "StillWakesTheDeepHeadTracking-v$version-installer.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $installerZip -Force
Write-Host "Built $installerZip" -ForegroundColor Green

# Nexus ZIP: only the files that drop into Habitat/Binaries/Win64/
$nexusStage = Join-Path $env:TEMP "swtd-ht-nexus-$([Guid]::NewGuid().ToString('N'))"
$nexusInner = New-Item -ItemType Directory -Path (Join-Path $nexusStage 'Habitat/Binaries/Win64') -Force
Copy-Item -Force $asi $nexusInner.FullName
$nexusZip = Join-Path $releaseDir "StillWakesTheDeepHeadTracking-v$version-nexus.zip"
# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $projectDir $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $nexusStage -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Compress-Archive -Path (Join-Path $nexusStage '*') -DestinationPath $nexusZip -Force
Write-Host "Built $nexusZip" -ForegroundColor Green

Remove-Item $stage -Recurse -Force
Remove-Item $nexusStage -Recurse -Force
