# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Strips the boot-time video playback out of a local Still Wakes the Deep
    install so the dev loop for this mod is short.

.DESCRIPTION
    Two changes, both reversible, both local to the machine:

    1. Every loose .mp4 in Habitat\Content\Movies is replaced with a 0.2s black
       stub (h264 + silent AAC, matching what the originals carry). The real
       file is kept alongside as <name>.mp4.bak. A stub rather than a deleted
       file so anything waiting on a "movie finished" event still gets one.
    2. Game.ini and Engine.ini are written into the game's user config dir with
       an empty startup-movie list, raised level-streaming time limits and a
       GameDefaultMap pointing past the startup map, then marked read-only so
       the engine does not rewrite them on exit.

    Steam's "verify integrity of game files" puts the real .mp4s back; re-run
    with -Action Apply afterwards.

.PARAMETER Action
    Apply (default), Restore, or Status.

.PARAMETER BootMap
    Map the engine opens instead of Maps/Minimal/startup, whose flow is the
    DISCLAIMER / PHOTOSENSITIVE / DATA CORRUPTION screens and then the main
    menu. The default drops straight into the intro chapter, walkable, roughly
    22s from process start on this machine. Pass an empty string to leave
    GameDefaultMap alone and boot through the normal chain.
#>
param(
    [ValidateSet('Apply', 'Restore', 'Status')]
    [string]$Action = 'Apply',

    [string]$BootMap = '/Game/Habitat/Maps/Story/Persistent/Menu_P'
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

$moviesDir = Join-Path $gamePath 'Habitat\Content\Movies'
if (-not (Test-Path $moviesDir)) {
    throw "Movies directory not found: $moviesDir"
}

$configDir = Join-Path $env:LOCALAPPDATA 'Habitat\Saved\Config\Windows'
$marker = '; managed by scripts/fast-boot.ps1 - delete this file to revert'

$gameIni = @"
$marker

[/Script/MoviePlayer.MoviePlayerSettings]
!StartupMovies=ClearArray
bWaitForMoviesToComplete=False
bMoviesAreSkippable=True
"@

# 5ms/5ms/5ms are the engine defaults. Raising them lets a load spend more of
# each tick actually loading; the cost is coarser hitching while levels stream
# in, which is an acceptable trade on a debug install.
$engineIni = @"
$marker

[/Script/Engine.StreamingSettings]
AsyncLoadingTimeLimit=25.0
PriorityAsyncLoadingExtraTime=50.0
LevelStreamingActorsUpdateTimeLimit=25.0
LevelStreamingComponentsRegistrationGranularity=50
UnregisterComponentsTimeLimit=25.0
LevelStreamingComponentsUnregistrationGranularity=50
"@

if ($BootMap) {
    $engineIni += @"


[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=$BootMap
"@
}

function Set-ManagedIni {
    param([string]$Path, [string]$Content)

    if (Test-Path $Path) {
        Set-ItemProperty -Path $Path -Name IsReadOnly -Value $false
        if ((Get-Content -Path $Path -Raw) -notmatch [regex]::Escape($marker)) {
            $keep = "$Path.fastboot-backup"
            Move-Item -Force $Path $keep
            Write-Host "  Kept the game's own $(Split-Path -Leaf $Path) as $(Split-Path -Leaf $keep)" -ForegroundColor Yellow
        }
    }
    Set-Content -Path $Path -Value $Content -Encoding utf8
    Set-ItemProperty -Path $Path -Name IsReadOnly -Value $true
    Write-Host "  Wrote $(Split-Path -Leaf $Path) (read-only)" -ForegroundColor Green
}

function Remove-ManagedIni {
    param([string]$Path)

    if (Test-Path $Path) {
        Set-ItemProperty -Path $Path -Name IsReadOnly -Value $false
        if ((Get-Content -Path $Path -Raw) -match [regex]::Escape($marker)) {
            Remove-Item -Force $Path
            Write-Host "  Removed $(Split-Path -Leaf $Path)" -ForegroundColor Green
        }
    }
    $keep = "$Path.fastboot-backup"
    if (Test-Path $keep) {
        Move-Item -Force $keep $Path
        Write-Host "  Restored the game's own $(Split-Path -Leaf $Path)" -ForegroundColor Green
    }
}

switch ($Action) {

    'Apply' {
        $ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
        if (-not $ffmpeg) {
            throw 'ffmpeg is not on PATH; it is needed to build the stub video.'
        }

        $stub = Join-Path ([System.IO.Path]::GetTempPath()) 'swtd-fastboot-stub.mp4'
        & $ffmpeg -y -hide_banner -loglevel error `
            -f lavfi -i color=c=black:s=1920x1080:r=30:d=0.2 `
            -f lavfi -i anullsrc=channel_layout=stereo:sample_rate=48000 `
            -t 0.2 -c:v libx264 -preset veryfast -pix_fmt yuv420p -profile:v high `
            -c:a aac -b:a 64k -movflags +faststart $stub
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed to build the stub video (exit $LASTEXITCODE)." }

        $stubBytes = (Get-Item $stub).Length
        Write-Host "Stubbing videos in $moviesDir" -ForegroundColor Cyan

        foreach ($mp4 in Get-ChildItem -Path $moviesDir -Filter '*.mp4') {
            $bak = "$($mp4.FullName).bak"
            if (-not (Test-Path $bak)) {
                Move-Item $mp4.FullName $bak
            }
            Copy-Item -Force $stub $mp4.FullName
            $saved = [math]::Round(((Get-Item $bak).Length - $stubBytes) / 1MB, 1)
            Write-Host ("  {0,-32} stubbed (-{1} MB)" -f $mp4.Name, $saved)
        }

        Write-Host "Writing config overrides to $configDir" -ForegroundColor Cyan
        New-Item -ItemType Directory -Force -Path $configDir | Out-Null
        Set-ManagedIni -Path (Join-Path $configDir 'Game.ini') -Content $gameIni
        Set-ManagedIni -Path (Join-Path $configDir 'Engine.ini') -Content $engineIni

        Write-Host 'Fast boot applied.' -ForegroundColor Green
    }

    'Restore' {
        Write-Host "Restoring videos in $moviesDir" -ForegroundColor Cyan
        foreach ($bak in Get-ChildItem -Path $moviesDir -Filter '*.mp4.bak') {
            $mp4 = $bak.FullName -replace '\.bak$', ''
            Copy-Item -Force $bak.FullName $mp4
            Write-Host ("  {0,-32} restored" -f (Split-Path -Leaf $mp4))
        }

        Write-Host "Removing config overrides from $configDir" -ForegroundColor Cyan
        Remove-ManagedIni -Path (Join-Path $configDir 'Game.ini')
        Remove-ManagedIni -Path (Join-Path $configDir 'Engine.ini')

        Write-Host 'Fast boot reverted.' -ForegroundColor Green
    }

    'Status' {
        Write-Host "Movies in $moviesDir" -ForegroundColor Cyan
        foreach ($mp4 in Get-ChildItem -Path $moviesDir -Filter '*.mp4') {
            $state = if ($mp4.Length -lt 64KB) { 'STUBBED' } else { 'original' }
            Write-Host ("  {0,-32} {1,-9} {2,10:N0} bytes" -f $mp4.Name, $state, $mp4.Length)
        }

        Write-Host "Config in $configDir" -ForegroundColor Cyan
        foreach ($name in @('Game.ini', 'Engine.ini')) {
            $path = Join-Path $configDir $name
            $state = if (-not (Test-Path $path)) {
                'absent'
            } elseif ((Get-Content -Path $path -Raw) -match [regex]::Escape($marker)) {
                'fast-boot'
            } else {
                "the game's own"
            }
            Write-Host ("  {0,-12} {1}" -f $name, $state)
        }
    }
}
