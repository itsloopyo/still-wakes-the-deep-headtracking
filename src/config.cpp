// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/hotkey_codec.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"

namespace swtd_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using cfg::schema::Concept;
using ::cameraunlock::input::FormatKeyBindings;
using ::cameraunlock::input::KeyModifiers;

constexpr const wchar_t* kIniName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyIniName = L"HeadTracking.ini";

// data/games.json's display_name for still-wakes-the-deep.
constexpr const char* kDisplayName = "Still Wakes the Deep";

// FovOffset's bounds, the ones every earlier build clamped it to. The hook holds
// the resulting field of view to 10 to 170 (AimProjection::kFovMinDegrees and
// kFovMaxDegrees).
constexpr double kMinFovOffset = -30.0;
constexpr double kMaxFovOffset = 60.0;

constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;

// The keys every build before the canonical format bound in code rather than in
// the file.
constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;
constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;
constexpr int kVkU = 0x55;
constexpr int kVkJ = 0x4A;

std::unique_ptr<cfg::ConfigOwner<Config>> g_owner;

void Save(const char* rows, const std::function<void(Config&)>& change) {
    // No owner when the bootstrap could not read the game's folder.
    if (!g_owner) {
        Log::Line("config: %s not saved: CameraUnlock.ini has no known folder this session", rows);
        return;
    }
    const cfg::ConfigSaveResult result = g_owner->Save(change);
    if (result.status != cfg::ConfigSaveStatus::Saved) {
        Log::Line("config: %s %s: %s", rows, cfg::ConfigSaveStatusName(result.status), result.reason.c_str());
    }
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
}

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    // Every earlier build opened HeadTracking.ini by its ANSI path, and the
    // frozen reader does the same. Where it finds no file, the published build
    // ran on its defaults.
    legacy::Config read;
    const bool present = legacy::Load(input.ansi_path, read);

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> pose_shaping;

    // Every sensitivity and inversion shipped at identity, and the position
    // offset is already converted to the game's centimetres in code, so nothing
    // folds: the mod applies the pose as the tracker sends it, and a value the
    // player changed is dropped.
    const auto shaping = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, pose_shaping, dropped);
    };
    shaping(read.yaw_sensitivity, 1.0f, "Rotation", "YawSensitivity");
    shaping(read.pitch_sensitivity, 1.0f, "Rotation", "PitchSensitivity");
    shaping(read.roll_sensitivity, 1.0f, "Rotation", "RollSensitivity");
    shaping(read.invert_yaw, false, "Rotation", "InvertYaw");
    shaping(read.invert_pitch, false, "Rotation", "InvertPitch");
    shaping(read.invert_roll, false, "Rotation", "InvertRoll");
    shaping(read.position_sensitivity_x, 1.0f, "Position", "SensitivityX");
    shaping(read.position_sensitivity_y, 1.0f, "Position", "SensitivityY");
    shaping(read.position_sensitivity_z, 1.0f, "Position", "SensitivityZ");

    // The reader keeps the port inside 1024-65535, and the smoothing pair, the
    // FOV offset and the torch multiplier finite and inside their ranges, so
    // each carries over as it is.
    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.fov_offset = read.fov_offset;
    // [Torch] Enabled is retired. Turning it off pinned the beam to the game's own aim,
    // which LightMultiplier=0 says exactly, so an imported file keeps what its author meant.
    out.light_multiplier = read.torch_follows_head ? read.torch_multiplier : 0.0f;
    out.flare_follows_beam = read.torch_flare_follows_beam;
    out.widget_dump = read.widget_dump;

    // [Position] Enabled chose the startup mode and nothing else: the cycle
    // reached every mode either way.
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(
        read.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                              : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = channels.rotation_enabled;
    out.position_enabled = channels.position_enabled;

    // The reader holds the limits to no range, so one that is not a number takes
    // the row's default (N2), and a finite one outside the rows' 0 to 10 the
    // nearest end of it (N4).
    const Config defaults = Table().defaults();
    const auto finite = [&](float value, float row_default, const char* key) {
        return cfg::LegacyFiniteOrDefault(value, row_default, "Position", key, dropped);
    };
    out.position_limit_x = cfg::LegacyClampToRange<Concept::PositionLimitX>(
        finite(read.limit_x, defaults.position_limit_x, "LimitX"), "Position", "LimitX", dropped);
    out.position_limit_y = cfg::LegacyClampToRange<Concept::PositionLimitY>(
        finite(read.limit_y, defaults.position_limit_y, "LimitY"), "Position", "LimitY", dropped);
    out.position_limit_y_down = cfg::LegacyClampToRange<Concept::PositionLimitYDown>(
        finite(read.limit_y_down, defaults.position_limit_y_down, "LimitYDown"), "Position", "LimitYDown", dropped);
    out.position_limit_z = cfg::LegacyClampToRange<Concept::PositionLimitZ>(
        finite(read.limit_z, defaults.position_limit_z, "LimitZ"), "Position", "LimitZ", dropped);
    out.position_limit_z_back = cfg::LegacyClampToRange<Concept::PositionLimitZBack>(
        finite(read.limit_z_back, defaults.position_limit_z_back, "LimitZBack"), "Position", "LimitZBack", dropped);

    // End, Page Up and the Ctrl+Shift chords were bound in code; only the yaw
    // key was in the file, and the reader keeps it inside 0x01-0xFE. The
    // inject-mode chords were bound only with [Dev] InjectHotkeys on.
    // A yaw key on Ctrl, Shift or Alt alone is unbound (N3) and keeps the chord.
    out.toggle_key = FormatKeyBindings({{KeyModifiers::kNone, kVkEnd}, {kChord, kVkY}});
    out.cycle_tracking_mode_key = FormatKeyBindings({{KeyModifiers::kNone, kVkPageUp}, {kChord, kVkG}});
    const std::string yaw_key = cfg::LegacyVirtualKeyToBindings(read.yaw_mode_key, "Hotkeys", "YawModeKey", dropped);
    const std::string yaw_chord = FormatKeyBindings({{kChord, kVkH}});
    out.yaw_mode_key = yaw_key.empty() ? yaw_chord : yaw_key + ", " + yaw_chord;
    out.inject_next_key = read.inject_hotkeys ? FormatKeyBindings({{kChord, kVkU}}) : std::string();
    out.inject_previous_key = read.inject_hotkeys ? FormatKeyBindings({{kChord, kVkJ}}) : std::string();

    // A setting the player never changed from what v0.2.0 shipped follows
    // Defaults.ini. The toggle and mode hotkeys were bound in code. Each limit
    // is compared as read: one that is not a number is no player's choice and
    // follows Defaults.ini (N2), and one N4 clamped is the player's.
    const legacy::Config shipped;
    cfg::LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, read.udp_port, shipped.udp_port);
    follows.Setting(Concept::EnableOnStartup, read.enable_on_startup, shipped.enable_on_startup);
    follows.Setting(Concept::WorldSpaceYaw, read.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(read.position_enabled, shipped.position_enabled);
    follows.Setting(Concept::LocalSmoothing, read.local_smoothing, shipped.local_smoothing);
    follows.Setting(Concept::RemoteSmoothing, read.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(Concept::PositionLimitX, read.limit_x, shipped.limit_x);
    follows.Setting(Concept::PositionLimitY, read.limit_y, shipped.limit_y);
    follows.Setting(Concept::PositionLimitYDown, read.limit_y_down, shipped.limit_y_down);
    follows.Setting(Concept::PositionLimitZ, read.limit_z, shipped.limit_z);
    follows.Setting(Concept::PositionLimitZBack, read.limit_z_back, shipped.limit_z_back);
    follows.NotInLegacy(Concept::ToggleKey);
    follows.NotInLegacy(Concept::CycleTrackingModeKey);
    follows.Setting(Concept::YawModeKey, read.yaw_mode_key, shipped.yaw_mode_key);
    // One row now, and it follows Defaults.ini only when the player left both old keys alone:
    // [Torch] Enabled=false is a choice, and it lands here as LightMultiplier=0.
    follows.Setting(Concept::LightMultiplier,
                    read.torch_follows_head == shipped.torch_follows_head &&
                        (!std::isfinite(read.torch_multiplier) ||
                         read.torch_multiplier == shipped.torch_multiplier));

    return present ? cfg::ImportResult::Imported(std::move(dropped), std::move(pose_shaping), follows.Concepts())
                   : cfg::ImportResult::Absent(std::move(dropped), std::move(pose_shaping), follows.Concepts());
}

}  // namespace

cfg::ConfigTable<Config> Table() {
    cfg::ConfigTable<Config> table;
    table.Concept<Concept::UdpPort>(&Config::udp_port)
        .Concept<Concept::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<Concept::WorldSpaceYaw>(&Config::world_space_yaw)
        .Writable()
        .Concept<Concept::RotationEnabled>(&Config::rotation_enabled)
        .Writable()
        .Concept<Concept::LocalSmoothing>(&Config::local_smoothing)
        .Concept<Concept::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<Concept::PositionEnabled>(&Config::position_enabled)
        .Writable()
        .Concept<Concept::PositionLimitX>(&Config::position_limit_x)
        .Concept<Concept::PositionLimitY>(&Config::position_limit_y)
        .Concept<Concept::PositionLimitYDown>(&Config::position_limit_y_down)
        .Concept<Concept::PositionLimitZ>(&Config::position_limit_z)
        .Concept<Concept::PositionLimitZBack>(&Config::position_limit_z_back)
        .Concept<Concept::ToggleKey>(&Config::toggle_key)
        .Concept<Concept::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
        .Concept<Concept::YawModeKey>(&Config::yaw_mode_key)
        .Concept<Concept::LightMultiplier>(&Config::light_multiplier)
        .Local("Light", "FlareFollowsBeam", &Config::flare_follows_beam, cfg::BoolCodec(),
               "true: the torch's glare moves with the beam. The glare hangs off the torch body,\n"
               "so with the beam on your head it would otherwise stay behind, pinned to the world.")
        .Local("Camera", "FovOffset", &Config::fov_offset, cfg::FloatCodec(),
               "Degrees added to the game's field of view, -30 to 60. 0 leaves it as it is.\n"
               "The game has no field of view setting of its own. Cutscenes and menus keep the\n"
               "game's framing. HeadTracking.log shows the field of view the game draws at on\n"
               "its fov: line.")
        .Range(kMinFovOffset, kMaxFovOffset)
        .Local("Dev", "InjectNextKey", &Config::inject_next_key, cfg::HotkeyCodec(),
               "For development. Steps which of the game's view point callers is given the head\n"
               "pose, to find the render path again after a game patch.")
        .Local("Dev", "InjectPreviousKey", &Config::inject_previous_key, cfg::HotkeyCodec(),
               "For development. Steps the other way.")
        .Local("Dev", "WidgetDump", &Config::widget_dump, cfg::BoolCodec(),
               "For development. true: write the game's crosshair and prompt widgets to\n"
               "HeadTracking.log, to find them again after a game patch.");
    return table;
}

cfg::RenderHeader Header() {
    cfg::RenderHeader header;
    header.display_name = kDisplayName;
    return header;
}

cfg::LegacyImport<Config> Import() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    for (const legacy::Key& key : legacy::ReadKeys()) import.keys.push_back({key.section, key.key});
    return import;
}

cfg::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = exe_dir + L"\\" + kIniName;
    options.table = Table();
    options.import = Import();
    options.legacy_path = exe_dir + L"\\" + kLegacyIniName;
    options.header = Header();
    options.defaults = std::move(defaults);
    return options;
}

Config Load(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    g_owner = std::make_unique<cfg::ConfigOwner<Config>>(OwnerOptions(exe_dir, std::move(defaults)));
    const cfg::ConfigLoadResult<Config> result = g_owner->Load();
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
    if (!result.reason.empty()) Log::Line("config: %s", result.reason.c_str());
    Log::Line("config: %s", cfg::ConfigLoadStatusName(result.status));
    return result.config;
}

cameraunlock::TrackingMode StartupTrackingMode(const Config& config) {
    const auto mode = cameraunlock::DecodeTrackingMode(config.rotation_enabled, config.position_enabled);
    if (!mode) throw std::logic_error("RotationEnabled and PositionEnabled are both false, which the table never gives");
    return *mode;
}

void SaveWorldSpaceYaw(bool world_space_yaw) {
    Save("[General] WorldSpaceYaw", [world_space_yaw](Config& c) { c.world_space_yaw = world_space_yaw; });
}

void SaveTrackingMode(cameraunlock::TrackingMode mode) {
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    Save("[General] RotationEnabled and [Position] PositionEnabled", [channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

}  // namespace swtd_ht::config
