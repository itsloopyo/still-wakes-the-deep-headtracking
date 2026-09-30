# Still Wakes the Deep Head Tracking

![Still Wakes the Deep running with this mod](https://raw.githubusercontent.com/itsloopyo/still-wakes-the-deep-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Still Wakes the Deep that moves the view with your head while your mouse or controller keeps control of look and interaction, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the view; what you can reach and grab stays on your mouse
- **6DOF positional tracking** - lean and peek around corners with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- Still Wakes the Deep, Steam edition ([store page](https://store.steampowered.com/app/1622910/)).
- A tracking source that sends the OpenTrack UDP protocol, such as [OpenTrack](https://github.com/opentrack/opentrack) driving a webcam or a VR headset, or a phone app that speaks it directly.
- Windows 10 or 11, 64-bit.

Compatible camera addresses and layouts are checked at startup. If discovery
fails on an unrecognised build, tracking stays off and the log records why.
The existing Steam build profile remains available.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Still Wakes the Deep**, and click
**Play with head tracking**.

### Standalone Installer

1. Download `StillWakesTheDeepHeadTracking-v<version>-installer.zip` from the [Releases](../../releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the install folder yourself. Either set the environment variable:

```powershell
$env:STILL_WAKES_THE_DEEP_PATH = "D:\Games\Still Wakes the Deep"
```

or pass the path as the first argument:

```powershell
install.cmd "D:\Games\Still Wakes the Deep"
```

### Manual Installation

The Nexus ZIP (`StillWakesTheDeepHeadTracking-v<version>-nexus.zip`) carries only the files that drop into the game, laid out in the folders they belong in.

1. Copy `vendor\ultimate-asi-loader\dinput8.dll` from the installer ZIP into `<game-root>\Habitat\Binaries\Win64\`, renamed to `winmm.dll`. This is the ASI loader. The game only loads `dinput8.dll` from System32, so the loader has to proxy `winmm.dll`, which the game EXE imports directly.
2. Copy `StillWakesTheDeepHeadTracking.asi` into that same folder, alongside `StillWakesTheDeep.exe`.

`CameraUnlock.ini`, the mod's settings file, is created in that folder on first launch.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has. Both are the
defaults of the key lists in `CameraUnlock.ini` (see Configuration), where each
action can be given other keys:

| Action | Nav-cluster | Chord |
|--------|-------------|-------|
| Toggle tracking | `End` | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up` | `Ctrl+Shift+G` |
| Toggle yaw mode (world / camera-local) | `Page Down` | `Ctrl+Shift+H` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

The tracking mode and the yaw mode are saved to `CameraUnlock.ini` the moment
you change them, so the game starts in them next time. Toggling tracking with
`End` lasts for the session only; whether tracking starts on is
`EnableOnStartup`.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `Habitat\Binaries\Win64\CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`
- `LightMultiplier=1.5`

With every setting at its default, the file reads:

```ini
; Still Wakes the Deep head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[Light]
; How far the light turns for each degree your head turns.
; 1 matches the view, 0 keeps the light on your aim.
LightMultiplier=default
; true: the torch's glare moves with the beam. The glare hangs off the torch body,
; so with the beam on your head it would otherwise stay behind, pinned to the world.
FlareFollowsBeam=true

[Camera]
; Degrees added to the game's field of view, -30 to 60. 0 leaves it as it is.
; The game has no field of view setting of its own. Cutscenes and menus keep the
; game's framing. HeadTracking.log shows the field of view the game draws at on
; its fov: line.
FovOffset=0.0

[Dev]
; For development. Steps which of the game's view point callers is given the head
; pose, to find the render path again after a game patch.
InjectNextKey=
; For development. Steps the other way.
InjectPreviousKey=
; For development. true: write the game's crosshair and prompt widgets to
; HeadTracking.log, to find them again after a game patch.
WidgetDump=false
```
<!-- /cameraunlock:config -->

### Field of view

Still Wakes the Deep has no field of view setting of its own, so `FovOffset`
under `[Camera]` is the way to widen the view. It is degrees added to the field
of view the game renders with, from -30 to 60, and 0 leaves the game alone. It
is an offset rather than a fixed field of view, so the game keeps the changes it
makes itself, and cutscenes and menus stay at the framing the game chose. Head
tracking still runs during a cutscene; only the offset stands down.
`HeadTracking.log` prints the field of view the game renders at on a line
starting `fov:`, so you can see what you are adding to.

### The torch

The torch points where you are looking rather than where you are aiming.
`LightMultiplier` scales the head pose the beam is given. The
default, 1.5, leads the view, because turning your head puts your eyes off the
centre of the screen and a beam matched to the view lands short of what you are
looking at. 1.0 moves the beam with the view, and 0 leaves it where the game
aimed it.

The torch's glare hangs off the torch body rather than the beam, so with the
beam on your head the glare would be left behind, pinned to the world.
`FlareFollowsBeam` moves it onto the beam, where it picks up the beam's own
sway.

## Troubleshooting

**Mod not loading**

- Confirm `winmm.dll` and `StillWakesTheDeepHeadTracking.asi` both sit in `<game-root>\Habitat\Binaries\Win64\`, next to `StillWakesTheDeep.exe`.
- Look for `HeadTracking.log` in that same folder. It is written on every launch, and the previous session is kept as `HeadTracking.prev.log`. No log at all means the loader never ran.

**No tracking response**

- Check your tracker is sending UDP to `127.0.0.1:4242` and is actually tracking.
- Another program may already hold the port. The mod logs `Failed to bind UDP port 4242` and retries twice a second, so closing the other program is enough: it picks the port up within about a second and logs `Bound UDP port 4242 ... tracking is live`.
- Press `End` (or `Ctrl+Shift+Y`) in case tracking was toggled off.

**Jittery or unstable tracking**

- Raise `RemoteSmoothing` if your tracker is a phone or another device on the network. That is the value a network connection gets.
- If the app sends a raw feed, route it through OpenTrack and use OpenTrack's filters rather than leaning on the mod's smoothing.

**Wrong rotation axis**

- If yaw feels wrong when you are looking steeply up or down, press `Page Down` (or `Ctrl+Shift+H`) to switch yaw mode. World-locked, the default, keeps yaw on the horizon; camera-local follows the camera's current up-axis, which leans the picture as you turn.
- The mod applies the pose as your tracker sends it. If an axis moves the wrong way, invert it in your tracker.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. `CameraUnlock.ini` stays in place so a reinstall keeps your settings. The ASI loader shim is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires Visual Studio 2022 or newer with the C++ workload, CMake, and [pixi](https://pixi.sh).

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/still-wakes-the-deep-headtracking
cd still-wakes-the-deep-headtracking
pixi run build
pixi run test
pixi run package
```

Outputs land in `release/`.

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- Game by [The Chinese Room](https://www.thechineseroom.co.uk/) and [Secret Mode](https://secretmode.com/), on [Steam](https://store.steampowered.com/app/1622910/).
- Loader: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (MIT).
- Hooking: [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause).
- Tracking protocol: [OpenTrack](https://github.com/opentrack/opentrack) (ISC).
- Shared infrastructure: [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) (MIT).
- Full notices in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by The Chinese Room or Secret Mode. Use at your own risk.
