// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cstdio>
#include <windows.h>

#include "legacy_config/legacy_config.h"

namespace swtd_ht {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

std::string ini_path(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

}  // namespace

void config_load(const std::string& exe_dir, Config& out) {
    legacy::Config read;
    legacy::Load(ini_path(exe_dir), read);

    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.yaw_sensitivity = read.yaw_sensitivity;
    out.pitch_sensitivity = read.pitch_sensitivity;
    out.roll_sensitivity = read.roll_sensitivity;
    out.invert_yaw = read.invert_yaw;
    out.invert_pitch = read.invert_pitch;
    out.invert_roll = read.invert_roll;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.fov_offset = read.fov_offset;
    out.position_enabled = read.position_enabled;
    out.position_sensitivity_x = read.position_sensitivity_x;
    out.position_sensitivity_y = read.position_sensitivity_y;
    out.position_sensitivity_z = read.position_sensitivity_z;
    out.limit_x = read.limit_x;
    out.limit_y = read.limit_y;
    out.limit_y_down = read.limit_y_down;
    out.limit_z = read.limit_z;
    out.limit_z_back = read.limit_z_back;
    out.torch_follows_head = read.torch_follows_head;
    out.torch_multiplier = read.torch_multiplier;
    out.torch_flare_follows_beam = read.torch_flare_follows_beam;
    out.inject_hotkeys = read.inject_hotkeys;
    out.widget_dump = read.widget_dump;
    out.yaw_mode_key = read.yaw_mode_key;
}

void config_write_default_if_missing(const std::string& exe_dir) {
    const std::string p = ini_path(exe_dir);
    if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    FILE* f = nullptr;
    fopen_s(&f, p.c_str(), "w");
    if (!f) return;
    std::fprintf(f,
        "; Still Wakes the Deep Head Tracking - configuration\n"
        "; Edit values, restart the game to apply.\n\n"
        "[Network]\n"
        "UdpPort=4242\n\n"
        "[General]\n"
        "EnableOnStartup=1\n"
        "; Yaw mode: 1 = horizon-locked yaw (default), 0 = camera-local yaw.\n"
        "; Page Down (or Ctrl+Shift+H) toggles it in game.\n"
        "WorldSpaceYaw=1\n\n"
        "[Rotation]\n"
        "YawSensitivity=1.0\n"
        "PitchSensitivity=1.0\n"
        "RollSensitivity=1.0\n"
        "InvertYaw=0\n"
        "InvertPitch=0\n"
        "InvertRoll=0\n"
        "; Smoothing 0.0 (responsive) - 1.0 (heavy). Covers rotation and position.\n"
        "; The value is picked per connection from the packet source address:\n"
        "; LocalSmoothing for a tracker running on this PC (loopback),\n"
        "; RemoteSmoothing for a phone or other device on the network.\n"
        "LocalSmoothing=0.0\n"
        "RemoteSmoothing=0.15\n\n"
        "[Camera]\n"
        "; Degrees added to the game's field of view. Still Wakes the Deep has no\n"
        "; FOV setting of its own, so this is the only way to widen the view. It is\n"
        "; an offset rather than a fixed FOV, so the game keeps the FOV changes it\n"
        "; makes itself, and cutscenes and menus stay at the framing it chose. Head\n"
        "; tracking still runs during a cutscene; only the offset stands down.\n"
        "; HeadTracking.log prints the FOV the game renders at, so you can see what\n"
        "; you are adding to. Range -30 to +60; 0 leaves the game alone.\n"
        "FovOffset=0.0\n\n"
        "[Position]\n"
        "Enabled=1\n"
        "SensitivityX=1.0\n"
        "SensitivityY=1.0\n"
        "SensitivityZ=1.0\n"
        "LimitX=0.30\n"
        "LimitY=0.20\n"
        "LimitYDown=%.2f\n"
        "LimitZ=0.40\n"
        "LimitZBack=0.10\n\n"
        "[Torch]\n"
        "; Point the torch where you are looking rather than where you are aiming.\n"
        "; Multiplier scales the head pose the beam is given. The default leads the\n"
        "; view, because turning your head puts your eyes off the centre of the screen\n"
        "; and a beam matched to the view lands short of what you are looking at.\n"
        "; 1.0 moves the beam with the view, 0.0 leaves it where the game aimed it.\n"
        "Enabled=1\n"
        "Multiplier=1.5\n"
        "; The torch's glare card hangs off the torch body rather than the beam, so\n"
        "; with the beam on your head the glare gets left behind and reads as pinned\n"
        "; to the world. This moves it onto the beam, where it picks up the beam's\n"
        "; own sway.\n"
        "FlareFollowsBeam=1\n\n"
        "[Hotkeys]\n"
        "; Virtual-key code for the yaw-mode toggle. Ctrl+Shift+H does the same\n"
        "; job and is not configurable.\n"
        "YawModeKey=0x22\n\n"
        "[Dev]\n"
        "; Ctrl+Shift+U / Ctrl+Shift+J cycle which GetPlayerViewPoint caller is\n"
        "; head-tracked. Only needed to re-confirm the render caller after a\n"
        "; game patch moves it.\n"
        "InjectHotkeys=0\n",
        static_cast<double>(cameraunlock::PositionSettings{}.limit_y_down));
    std::fclose(f);
}

}  // namespace swtd_ht
