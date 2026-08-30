# Still Wakes the Deep Head Tracking

![Still Wakes the Deep running with this mod](https://raw.githubusercontent.com/itsloopyo/still-wakes-the-deep-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Still Wakes the Deep that moves the view with your head while your mouse or controller keeps control of look and interaction, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the view; what you can reach and grab stays on your mouse
- **6DOF positional tracking** - lean and peek around corners with head position
- **Torch follows your head** - the beam lights where you look, not where the mouse points
- **A field-of-view setting** - the game ships without one; the mod adds it

## Requirements

- Still Wakes the Deep, Steam edition ([store page](https://store.steampowered.com/app/1622910/)).
- A tracking source that sends the OpenTrack UDP protocol, such as [OpenTrack](https://github.com/opentrack/opentrack) driving a webcam or a VR headset, or a phone app that speaks it directly.
- Windows 10 or 11, 64-bit.

## Installation

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

`HeadTracking.ini` is written into that folder on first launch.

## Setting Up OpenTrack

In OpenTrack, set **Output** to **UDP over network**, address `127.0.0.1`, port `4242`, then start tracking. Any sample rate works; the mod estimates the incoming rate per stream and interpolates it up to your frame rate.

Centering is done in your tracker: OpenTrack's Center bind, the CENTER button in a phone app, or SteamVR's reset. The mod applies the pose it is sent.

### VR Headset Setup

1. Connect the headset to the PC over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave OpenTrack's **Output** on UDP `127.0.0.1:4242`.

### Webcam Setup

Set OpenTrack's **Input** to the **neuralnet tracker**, which tracks your face from a plain webcam and needs no markers, clips or IR hardware. Leave **Output** on UDP `127.0.0.1:4242`.

### Phone App Setup

The mod accepts one thing: the OpenTrack UDP protocol on port `4242`. A phone app is usable here if it sends that protocol itself, or ships a PC-side companion that does.

For an app that does send it, how you wire it up depends on how much filtering the app does before the packet leaves the phone. An app that filters on-device can point straight at your PC's LAN IP on port `4242`. A raw or lightly filtered feed sent direct will jitter, because the mod's smoothing is sized to take the edge off a clean signal rather than to rescue a noisy one; that app should go through OpenTrack instead, so OpenTrack's filters and curves can clean the feed up first. The test is quick: try direct, hold your head still, and if the view drifts or shakes, route it through OpenTrack.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone already in their pocket. It filters on-device, so it can send direct. Any app that filters enough noise works exactly the same way.

Smoothing is picked per connection from the packet's source address, and a phone on WiFi counts as remote, so it gets `RemoteSmoothing`. So does a tracker running on this same PC that sends to the LAN address instead of `127.0.0.1`, because the classifier sees a transport and not a machine.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

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

## Configuration

`HeadTracking.ini` is written next to the game exe in `<game-root>\Habitat\Binaries\Win64\` on first launch. Edit it and restart the game to apply.

```ini
[Network]
UdpPort=4242

[General]
EnableOnStartup=1
; Yaw mode: 1 = horizon-locked yaw (default), 0 = camera-local yaw.
; Page Down (or Ctrl+Shift+H) toggles it in game.
WorldSpaceYaw=1

[Rotation]
YawSensitivity=1.0
PitchSensitivity=1.0
RollSensitivity=1.0
InvertYaw=0
InvertPitch=0
InvertRoll=0
; Smoothing 0.0 (responsive) to 1.0 (heavy). Covers rotation and position.
; The value is picked per connection from the packet source address:
; LocalSmoothing for a tracker running on this PC (loopback),
; RemoteSmoothing for a phone or other device on the network.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Camera]
; Degrees added to the game's field of view. Still Wakes the Deep has no FOV
; setting of its own, so this is the only way to widen the view. It is an
; offset rather than a fixed FOV, so the game keeps the FOV changes it makes
; itself, and cutscenes and menus stay at the framing it chose. Range -30 to
; +60; 0 leaves the game alone. HeadTracking.log prints the FOV the game
; renders at on a line starting `fov:`, so you can see what you are adding to.
FovOffset=0.0

[Position]
Enabled=1
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
; Lean limits in metres. Z is asymmetric: more range forward than back.
LimitX=0.30
LimitY=0.20
LimitYDown=0.20
LimitZ=0.40
LimitZBack=0.10

[Torch]
; Point the torch where you are looking rather than where you are aiming.
; Multiplier scales the head pose the beam is given. The default leads the
; view, because turning your head puts your eyes off the centre of the screen
; and a beam matched to the view lands short of what you are looking at.
; 1.0 moves the beam with the view, 0.0 leaves it where the game aimed it.
Enabled=1
Multiplier=1.5
; The torch's glare card hangs off the torch body rather than the beam, so with
; the beam on your head the glare gets left behind and reads as pinned to the
; world. This moves it onto the beam, where it picks up the beam's own sway.
FlareFollowsBeam=1

[Hotkeys]
; Virtual-key code for the yaw-mode toggle. Ctrl+Shift+H does the same job and
; is not configurable.
YawModeKey=0x22

[Dev]
; Ctrl+Shift+U / Ctrl+Shift+J cycle which view-point caller is head-tracked.
; Only needed to re-confirm the render caller after a game patch moves it.
InjectHotkeys=0
```

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
- `InvertYaw`, `InvertPitch` and `InvertRoll` in `HeadTracking.ini` flip an axis your tracker sends the other way round.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The ASI loader shim is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

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

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

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
