// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Frozen. See legacy_config.h.

#include "legacy_config.h"

#include <cmath>
#include <string>
#include <vector>

#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/protocol/port_utils.h"

namespace swtd_ht::legacy {

namespace {

// Per-key fallbacks for a rejected smoothing value. They differ on purpose: a
// malformed RemoteSmoothing must not drop back to the LOCAL default.
constexpr float kLocalSmoothingFallback = 0.0f;
constexpr float kRemoteSmoothingFallback = 0.15f;

constexpr float kMultiplierDefault = 1.5f;
constexpr float kMultiplierMax = 5.0f;
constexpr float kFovOffsetMin = -30.0f;
constexpr float kFovOffsetMax = 60.0f;

// Read a float and hold it to a range. strtod parses "nan" and "inf", and
// std::clamp does not reject a NaN, so a value that is not finite takes the
// fallback and one outside the range is clamped into it, with a log line
// either way.
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
// report. 0 is also what ReadHex returns for text that will not parse at all.
int sanitize_vk(int v) {
    if (v < 0x01 || v > 0xFE) {
        Log::Line("config: [Hotkeys] YawModeKey 0x%02X is not a virtual-key code, using 0x22 (Page Down)", v);
        return 0x22;
    }
    return v;
}

// Warned once per process. The old single Smoothing value is not carried into
// the two keys that replaced it.
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

bool Load(const std::string& ini_path, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(ini_path)) return false;

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
    // Falls back to whatever LimitY resolved to, not to the struct default.
    out.limit_y_down       = ini.ReadFloat("Position", "LimitYDown",       out.limit_y);
    out.limit_z            = ini.ReadFloat("Position", "LimitZ",           out.limit_z);
    out.limit_z_back       = ini.ReadFloat("Position", "LimitZBack",       out.limit_z_back);
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");

    out.torch_follows_head = ini.ReadBool ("Torch",    "Enabled",          out.torch_follows_head);
    out.torch_multiplier   = read_ranged(ini, "Torch", "Multiplier",
        out.torch_multiplier, 0.0f, kMultiplierMax, kMultiplierDefault);
    out.torch_flare_follows_beam = ini.ReadBool("Torch", "FlareFollowsBeam",
                                               out.torch_flare_follows_beam);

    out.yaw_mode_key       = sanitize_vk(
        ini.ReadHex("Hotkeys", "YawModeKey", out.yaw_mode_key));

    out.inject_hotkeys     = ini.ReadBool ("Dev",      "InjectHotkeys",    out.inject_hotkeys);
    out.widget_dump        = ini.ReadBool ("Dev",      "WidgetDump",       out.widget_dump);
    return true;
}

std::vector<Key> ReadKeys() {
    return {
        {"Network", "UdpPort"},
        {"General", "EnableOnStartup"},
        {"General", "WorldSpaceYaw"},
        {"Rotation", "YawSensitivity"},
        {"Rotation", "PitchSensitivity"},
        {"Rotation", "RollSensitivity"},
        {"Rotation", "InvertYaw"},
        {"Rotation", "InvertPitch"},
        {"Rotation", "InvertRoll"},
        {"Rotation", "LocalSmoothing"},
        {"Rotation", "RemoteSmoothing"},
        {"Camera", "FovOffset"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitYDown"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Torch", "Enabled"},
        {"Torch", "Multiplier"},
        {"Torch", "FlareFollowsBeam"},
        {"Hotkeys", "YawModeKey"},
        {"Dev", "InjectHotkeys"},
        {"Dev", "WidgetDump"},
    };
}

}  // namespace swtd_ht::legacy
