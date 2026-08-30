// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cmath>
#include <cstdio>
#include <windows.h>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/protocol/port_utils.h"
#include "logging.h"

namespace swtd_ht {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

// Per-key fallbacks for a rejected smoothing value, matching the documented
// defaults in config.h. They differ on purpose: a malformed RemoteSmoothing must
// not drop back to the LOCAL default, which would leave a phone on WiFi running
// with no smoothing at all on raw network jitter.
constexpr float kLocalSmoothingFallback = 0.0f;
constexpr float kRemoteSmoothingFallback = 0.15f;

// The torch multiplier's ceiling is the same 5.0 resident-evil-requiem uses -
// past that the beam has left the screen on any believable head movement. The
// FOV bracket is what an offset can usefully be: past +60 the edges of the
// picture are unusable fisheye, and -30 is about as narrow as a first-person
// view gets before it is a telescope.
constexpr float kMultiplierMax = 5.0f;
constexpr float kFovOffsetMin = -30.0f;
constexpr float kFovOffsetMax = 60.0f;

std::string ini_path(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

// Read a float and hold it to a range.
//
// The strtod behind IniReader::ReadFloat parses "nan" and "inf" as perfectly
// valid floats, and std::clamp does NOT reject a NaN because both of its
// comparisons are false. A NaN smoothing would travel all the way into
// CalculateSmoothingFactor, skip that function's own speed clamp for the same
// reason, and exp(NaN) would return NaN - which then poisons the smoothed
// FRotator and FVector written back through the GetPlayerViewPoint hook for the
// rest of the session, with nothing in the log to explain the dead camera. The
// torch multiplier and the FOV offset reach the beam rotation and the projection
// matrix the same way.
//
// This is validation, never a floor: a finite value inside the range is returned
// untouched, so a deliberately configured 0.0 stays 0.0.
float read_ranged(const cameraunlock::IniReader& ini, const char* section,
                  const char* key, float current, float lo, float hi,
                  float fallback) {
    const float v = ini.ReadFloat(section, key, current);
    if (!std::isfinite(v)) {
        Log::Line("config: [%s] %s is not a finite number, using %.2f",
            section, key, fallback);
        return fallback;
    }
    if (v < lo || v > hi) {
        const float clamped = (v < lo) ? lo : hi;
        Log::Line("config: [%s] %s %.2f is outside %.1f-%.1f, using %.2f",
            section, key, v, lo, hi, clamped);
        return clamped;
    }
    return v;
}

// A virtual-key code outside 0x01-0xFE is not a key GetAsyncKeyState can ever
// report, so the toggle would simply never fire and the only clue would be a
// hotkey that does nothing. 0 is also what ReadHex returns for text that will
// not parse at all.
int sanitize_vk(int v) {
    if (v < 0x01 || v > 0xFE) {
        Log::Line("config: [Hotkeys] YawModeKey 0x%02X is not a virtual-key code, using 0x22 (Page Down)", v);
        return 0x22;
    }
    return v;
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                                    const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    Log::Line(
        "WARNING: Config key [%s] %s has been retired and is IGNORED. Smoothing is "
        "now two keys: LocalSmoothing (default 0, applies to a tracker on this "
        "machine) and RemoteSmoothing (default 0.15, applies to a tracker on the "
        "network). The old value is not migrated because the semantics changed - it "
        "carried a hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

}  // namespace

void config_load(const std::string& exe_dir, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(ini_path(exe_dir))) return;

    bool port_valid = false;
    out.udp_port = cameraunlock::NormalizeUdpPort(
        ini.ReadInt("Network", "UdpPort", out.udp_port), 4242, port_valid);
    if (!port_valid)
        Log::Line("config: [Network] UdpPort is outside 1024-65535, using 4242");

    out.enable_on_startup  = ini.ReadBool ("General",  "EnableOnStartup",  out.enable_on_startup);
    out.world_space_yaw    = ini.ReadBool ("General",  "WorldSpaceYaw",    out.world_space_yaw);

    out.yaw_sensitivity    = ini.ReadFloat("Rotation", "YawSensitivity",   out.yaw_sensitivity);
    out.pitch_sensitivity  = ini.ReadFloat("Rotation", "PitchSensitivity", out.pitch_sensitivity);
    out.roll_sensitivity   = ini.ReadFloat("Rotation", "RollSensitivity",  out.roll_sensitivity);
    out.invert_yaw         = ini.ReadBool ("Rotation", "InvertYaw",        out.invert_yaw);
    out.invert_pitch       = ini.ReadBool ("Rotation", "InvertPitch",      out.invert_pitch);
    out.invert_roll        = ini.ReadBool ("Rotation", "InvertRoll",       out.invert_roll);

    out.local_smoothing    = read_ranged(ini, "Rotation", "LocalSmoothing",
        out.local_smoothing,  0.0f, 1.0f, kLocalSmoothingFallback);
    out.remote_smoothing   = read_ranged(ini, "Rotation", "RemoteSmoothing",
        out.remote_smoothing, 0.0f, 1.0f, kRemoteSmoothingFallback);

    WarnRetiredSmoothingKey(ini, "Rotation", "Smoothing");

    out.fov_offset         = read_ranged(ini, "Camera", "FovOffset",
        out.fov_offset, kFovOffsetMin, kFovOffsetMax, 0.0f);

    out.position_enabled   = ini.ReadBool ("Position", "Enabled",          out.position_enabled);
    out.position_sensitivity_x = ini.ReadFloat("Position", "SensitivityX", out.position_sensitivity_x);
    out.position_sensitivity_y = ini.ReadFloat("Position", "SensitivityY", out.position_sensitivity_y);
    out.position_sensitivity_z = ini.ReadFloat("Position", "SensitivityZ", out.position_sensitivity_z);
    out.limit_x            = ini.ReadFloat("Position", "LimitX",           out.limit_x);
    out.limit_y            = ini.ReadFloat("Position", "LimitY",           out.limit_y);
    out.limit_y_down       = ini.ReadFloat("Position", "LimitYDown",       out.limit_y_down);
    out.limit_z            = ini.ReadFloat("Position", "LimitZ",           out.limit_z);
    out.limit_z_back       = ini.ReadFloat("Position", "LimitZBack",       out.limit_z_back);
    // No position smoothing key: position uses the same LocalSmoothing /
    // RemoteSmoothing pair as rotation.
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");

    out.torch_follows_head = ini.ReadBool ("Torch",    "Enabled",          out.torch_follows_head);
    out.torch_multiplier   = read_ranged(ini, "Torch", "Multiplier",
        out.torch_multiplier, 0.0f, kMultiplierMax, 1.5f);
    out.torch_flare_follows_beam = ini.ReadBool("Torch", "FlareFollowsBeam",
                                               out.torch_flare_follows_beam);

    out.yaw_mode_key       = sanitize_vk(
        ini.ReadHex("Hotkeys", "YawModeKey", out.yaw_mode_key));

    out.inject_hotkeys     = ini.ReadBool ("Dev",      "InjectHotkeys",    out.inject_hotkeys);
    out.widget_dump        = ini.ReadBool ("Dev",      "WidgetDump",       out.widget_dump);
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
        "LimitYDown=0.20\n"
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
        "InjectHotkeys=0\n");
    std::fclose(f);
}

}  // namespace swtd_ht
