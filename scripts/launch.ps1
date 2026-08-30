# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Launches Still Wakes the Deep for a head-tracking test run.

.DESCRIPTION
    Goes through steam.exe -applaunch, because the install ships no
    steam_appid.txt and the exe exits immediately when started directly.
    Adds -nosplash, and optionally a small windowed resolution. Which map the
    game boots into is set by scripts/fast-boot.ps1 -BootMap, not from here -
    the engine ignores a map name on the command line in this build.

.PARAMETER Windowed
    Launch windowed at -ResX by -ResY instead of the saved display mode.

.PARAMETER ListMaps
    Print the persistent maps in the install as /Game package paths, ready to
    paste into fast-boot.ps1 -BootMap, and exit.
#>
param(
    [switch]$Windowed,
    [int]$ResX = 1280,
    [int]$ResY = 720,
    [switch]$ListMaps
)

$ErrorActionPreference = 'Stop'

$gamePath = $env:STILL_WAKES_THE_DEEP_PATH
if (-not $gamePath) {
    $candidates = @(
        'C:\Program Files (x86)\Steam\steamapps\common\Still Wakes the Deep',
        'C:\Program Files\Steam\steamapps\common\Still Wakes the Deep'
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $gamePath = $c; break }
    }
}
if (-not $gamePath -or -not (Test-Path $gamePath)) {
    throw 'Could not locate Still Wakes the Deep install. Set $env:STILL_WAKES_THE_DEEP_PATH.'
}

if ($ListMaps) {
    $manifest = Join-Path $gamePath 'Manifest_UFSFiles_Win64.txt'
    Select-String -Path $manifest -Pattern 'Habitat/Content/(\S*/Persistent/\S+)\.umap' |
        ForEach-Object { '/Game/' + $_.Matches[0].Groups[1].Value } |
        Sort-Object -Unique
    return
}

$steam = Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe'
if (-not (Test-Path $steam)) {
    throw "steam.exe not found: $steam"
}

$gameArgs = @('-nosplash')
if ($Windowed) { $gameArgs += @('-windowed', "-ResX=$ResX", "-ResY=$ResY") }

Write-Host "Launching via Steam: $($gameArgs -join ' ')" -ForegroundColor Cyan
Start-Process -FilePath $steam -ArgumentList (@('-applaunch', '1622910') + $gameArgs)
