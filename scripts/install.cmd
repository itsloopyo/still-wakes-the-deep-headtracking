@echo off
:: ============================================
:: Still Wakes the Deep Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-asi.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper. Everything below the
:: CONFIG BLOCK is copied verbatim from
:: cameraunlock-core/scripts/templates/install-wrapper-asi.cmd.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=still-wakes-the-deep"
set "MOD_DISPLAY_NAME=Still Wakes the Deep Head Tracking"
set "MOD_DLLS=StillWakesTheDeepHeadTracking.asi"
set "MOD_INTERNAL_NAME=StillWakesTheDeepHeadTracking"
set "MOD_VERSION=0.2.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
set "ASI_LOADER_NAME=winmm.dll"
set "MOD_CONTROLS=Controls: End = toggle, PageUp = position toggle (chords: Ctrl+Shift+Y/G)."
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-asi.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-asi.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-asi.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
