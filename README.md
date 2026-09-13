# Still Wakes the Deep Head Tracking

![Still Wakes the Deep running with this mod](https://raw.githubusercontent.com/itsloopyo/still-wakes-the-deep-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Still Wakes the Deep that moves the view with your head while your mouse or controller keeps control of look and interaction, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the view; what you can reach and grab stays on your mouse
- **6DOF positional tracking** - lean and peek around corners with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android
- **Torch follows your head** - the beam lights where you look, not where the mouse points
- **A field-of-view setting** - the game ships without one; the mod adds it

## Requirements

- Still Wakes the Deep, Steam edition ([store page](https://store.steampowered.com/app/1622910/)).
- A tracking source that sends the OpenTrack UDP protocol, such as [OpenTrack](https://github.com/opentrack/opentrack) driving a webcam or a VR headset, or a phone app that speaks it directly.
- Windows 10 or 11, 64-bit.

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

`HeadTracking.ini` is written into that folder on first launch.

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
