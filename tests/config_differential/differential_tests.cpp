// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The differential test for the conversion from HeadTracking.ini to
// CameraUnlock.ini.
//
// Three readings of every input:
//
//   Oracle     v0.2.0's reader and startup code, the newest published build
//              (oracle/oracle_reader.cpp)
//   Import     the frozen reader in src/legacy_config/, through the startup
//              code the commit that froze it ran it through
//   Migration  the config owner's Load in a folder holding only the input as
//              HeadTracking.ini, which imports it into a new CameraUnlock.ini,
//              then the canonical reader and table on it, through this build's
//              startup code
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since v0.2.0 that change how the file is
// read. There is one. 443fc59 made a file without a usable [Position]
// LimitYDown take LimitY for it, where v0.2.0 took 0.20; every other field
// must agree bit for bit.
//
// Comparison 2, import against migration, is the proof for the conversion; see
// the section of that name below for what it allows.
//
// Inputs: the file v0.2.0 writes on its first run (no release shipped or seeded
// a HeadTracking.ini, and the dev build and the v0.1.0 tag write the same
// bytes), no file, an empty file, core's mutation corpus over the first-run
// file, that file with YawModeKey set to every code from 0x00 to 0xFF, and that
// file with LimitY raised and LimitYDown removed, which shows 443fc59.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle/oracle_reader.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"

namespace {

namespace fs = std::filesystem;
namespace cfg = cameraunlock::config;
namespace legacy = swtd_ht::legacy;
namespace testing = cameraunlock::config::testing;

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
}

std::string ReadFileBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteFileBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

// ---- Scratch folders ---------------------------------------------------------
//
// One folder per reading: GetPrivateProfileString, which every reader here sits
// on, is free to cache the file it last read. `game` stands for the folder the
// game exe runs from, and Defaults.ini sits in `global` beside it. Every folder
// lives under one root for the run, removed once at the end.

void RemoveTree(const fs::path& root) {
    if (!fs::exists(root)) return;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    fs::remove_all(root);
}

const fs::path& ScratchRoot() {
    static const fs::path root = [] {
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        fs::path r = fs::path(temp) / ("swtd_ht_diff_" + std::to_string(GetCurrentProcessId()));
        RemoveTree(r);
        return r;
    }();
    return root;
}

class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        root_ = ScratchRoot() / std::to_string(s_next++);
        fs::create_directories(root_ / "game");
    }

    fs::path game() const { return root_ / "game"; }
    fs::path legacy() const { return game() / "HeadTracking.ini"; }
    fs::path canonical() const { return game() / "CameraUnlock.ini"; }
    fs::path defaults() const { return root_ / "global" / "Defaults.ini"; }
    // The folder as ExeDirNarrow gave it, with no trailing backslash.
    std::string exe_dir() const { return game().string(); }

    void WriteLegacy(const std::string& bytes) const { WriteFileBytes(legacy(), bytes); }
    void WriteDefaults(const std::string& bytes) const {
        fs::create_directories(defaults().parent_path());
        WriteFileBytes(defaults(), bytes);
    }

private:
    fs::path root_;
};

// ---- What a reading does -------------------------------------------------------
//
// A Record names everything the running mod acts on after reading the file:
// `field.*` the settings, `start.*` the state the session starts in, `hotkey.*`
// the bindings that can fire, each as `modifiers:code` (Ctrl 1, Shift 2, as
// cameraunlock::input::KeyModifiers numbers them) in ascending order. Floats are
// their bits.

using Record = std::map<std::string, std::string>;

std::string Bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned>(bits));
    return text;
}

std::string Flag(bool value) { return value ? "1" : "0"; }

const char* const kActionNames[] = {"Toggle", "CycleTrackingMode", "YawMode", "InjectNext", "InjectPrevious"};

// The bindings a set of HotkeyPoller registrations can fire. The poller skips
// code 0, and GetAsyncKeyState reports no code above 0xFF or below 0 down.
void AddHotkeys(Record& r, const std::vector<swtd_oracle::Registration>& registrations) {
    std::map<int, std::vector<std::pair<unsigned, int>>> byAction;
    for (int action = 0; action < 5; ++action) byAction[action];
    for (const auto& [action, vk, modifiers] : registrations) {
        if (vk < 0x01 || vk > 0xFF) continue;
        byAction[action].push_back({modifiers, vk});
    }
    for (auto& [action, items] : byAction) {
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
        std::string text;
        for (const auto& [modifiers, vk] : items) {
            char item[32];
            std::snprintf(item, sizeof(item), "%s%u:0x%02X", text.empty() ? "" : " ", modifiers, static_cast<unsigned>(vk));
            text += item;
        }
        r[std::string("hotkey.") + kActionNames[action]] = text;
    }
}

const char* ModeName(int mode) {
    switch (mode) {
        case 0: return "RotationAndPosition";
        case 1: return "RotationOnly";
        case 2: return "PositionOnly";
        default: return "none";
    }
}

Record ObserveOracle(const swtd_oracle::Published& g) {
    Record r;
    r["field.udp_port"] = std::to_string(g.udp_port);
    r["field.rot.yaw_sensitivity"] = Bits(g.yaw_sens);
    r["field.rot.pitch_sensitivity"] = Bits(g.pitch_sens);
    r["field.rot.roll_sensitivity"] = Bits(g.roll_sens);
    r["field.rot.invert_yaw"] = Flag(g.invert_yaw);
    r["field.rot.invert_pitch"] = Flag(g.invert_pitch);
    r["field.rot.invert_roll"] = Flag(g.invert_roll);
    r["field.local_smoothing"] = Bits(g.local_smoothing);
    r["field.remote_smoothing"] = Bits(g.remote_smoothing);
    r["field.fov_offset"] = Bits(g.fov_offset);
    r["field.pos.sensitivity_x"] = Bits(g.pos_sens_x);
    r["field.pos.sensitivity_y"] = Bits(g.pos_sens_y);
    r["field.pos.sensitivity_z"] = Bits(g.pos_sens_z);
    r["field.pos.limit_x"] = Bits(g.limit_x);
    r["field.pos.limit_y"] = Bits(g.limit_y);
    r["field.pos.limit_y_down"] = Bits(g.limit_y_down);
    r["field.pos.limit_z"] = Bits(g.limit_z);
    r["field.pos.limit_z_back"] = Bits(g.limit_z_back);
    r["field.torch_multiplier"] = Bits(g.torch_multiplier);
    r["start.enabled"] = Flag(g.tracking_enabled);
    r["start.mode"] = ModeName(g.tracking_mode);
    r["start.world_space_yaw"] = Flag(g.world_space_yaw);
    r["start.torch_follows_head"] = Flag(g.torch_follows_head);
    r["start.flare_follows_beam"] = Flag(g.torch_flare_follows_beam);
    r["start.widget_dump"] = Flag(g.widget_dump);
    AddHotkeys(r, g.hotkeys);
    return r;
}

// The frozen reader's settings through the startup code of the commit that
// froze it, which is v0.2.0's: src/headtracking_mod.cpp, src/view_hook.cpp and
// src/mod_hotkeys.cpp were unchanged since the tag.
Record ObserveImport(const legacy::Config& c) {
    swtd_oracle::Published g;
    g.udp_port = c.udp_port;
    g.tracking_enabled = c.enable_on_startup;
    g.world_space_yaw = c.world_space_yaw;
    g.yaw_sens = c.yaw_sensitivity;
    g.pitch_sens = c.pitch_sensitivity;
    g.roll_sens = c.roll_sensitivity;
    g.invert_yaw = c.invert_yaw;
    g.invert_pitch = c.invert_pitch;
    g.invert_roll = c.invert_roll;
    g.local_smoothing = c.local_smoothing;
    g.remote_smoothing = c.remote_smoothing;
    g.fov_offset = c.fov_offset;
    g.tracking_mode = c.position_enabled ? 0 : 1;
    g.pos_sens_x = c.position_sensitivity_x;
    g.pos_sens_y = c.position_sensitivity_y;
    g.pos_sens_z = c.position_sensitivity_z;
    g.limit_x = c.limit_x;
    g.limit_y = c.limit_y;
    g.limit_y_down = c.limit_y_down;
    g.limit_z = c.limit_z;
    g.limit_z_back = c.limit_z_back;
    g.torch_follows_head = c.torch_follows_head;
    g.torch_multiplier = c.torch_multiplier;
    g.torch_flare_follows_beam = c.torch_flare_follows_beam;
    g.widget_dump = c.widget_dump;
    g.hotkeys = {{swtd_oracle::kToggle, 0x23, 0},
                 {swtd_oracle::kCycleMode, 0x21, 0},
                 {swtd_oracle::kYawMode, c.yaw_mode_key, 0},
                 {swtd_oracle::kToggle, 0x59, 3},
                 {swtd_oracle::kCycleMode, 0x47, 3},
                 {swtd_oracle::kYawMode, 0x48, 3}};
    if (c.inject_hotkeys) {
        g.hotkeys.push_back({swtd_oracle::kInjectNext, 0x55, 3});
        g.hotkeys.push_back({swtd_oracle::kInjectPrevious, 0x4A, 3});
    }
    return ObserveOracle(g);
}

std::vector<std::string> Differences(const Record& a, const Record& b) {
    std::vector<std::string> out;
    for (const auto& [name, value] : a) {
        const auto it = b.find(name);
        if (it == b.end()) {
            out.push_back(name + " only on the left");
        } else if (it->second != value) {
            out.push_back(name + ": " + value + " / " + it->second);
        }
    }
    for (const auto& [name, value] : b) {
        if (a.find(name) == a.end()) out.push_back(name + " only on the right");
    }
    return out;
}

// ---- Comparison 1's one difference -----------------------------------------------
//
// 443fc59, after v0.2.0: a file whose [Position] LimitYDown gives IniReader no
// number (no line, an empty value, text that is not a number) takes LimitY for
// it. v0.2.0 took its default, 0.20. Whether the key gave a number is read the
// way both readers read it, with a default no file holds.
bool LimitYDownFellBack(const fs::path& legacyPath) {
    cameraunlock::IniReader ini;
    if (!ini.Open(legacyPath.string())) return true;
    constexpr float kUnset = -98765.0f;
    return Bits(ini.ReadFloat("Position", "LimitYDown", kUnset)) == Bits(kUnset);
}

Record Comparison1Expected(Record oracle, const Record& imported, bool fellBack) {
    if (fellBack) oracle["field.pos.limit_y_down"] = imported.at("field.pos.limit_y");
    return oracle;
}

// ---- Inputs --------------------------------------------------------------------

fs::path DataPath(const char* name) {
    return fs::path(SWTD_SOURCE_DIR) / "tests" / "config_differential" / "data" / name;
}

// What v0.2.0's config_write_default_if_missing wrote on the first run: the
// file every player who ran a published build holds, edited or not.
std::string FirstRun() { return ReadFileBytes(DataPath("first-run-v0.2.0.ini")); }

const char* const kFirstRunName = "v0.2.0 first-run file";

// Every key the frozen reader reads, and how the corpus varies each one. The
// reader refuses a port outside 1024-65535 and a yaw key outside 0x01-0xFE,
// and clamps the smoothing pair into 0-1, FovOffset into -30 to 60 and the
// torch multiplier into 0-5; it checks nothing else.
std::vector<testing::MutationKey> CorpusKeys() {
    return {
        {"Network", "UdpPort", "5771", {"80", "70000"}},
        {"General", "EnableOnStartup", "0", {}},
        {"General", "WorldSpaceYaw", "0", {}},
        {"Rotation", "YawSensitivity", "0.5", {}},
        {"Rotation", "PitchSensitivity", "0.5", {}},
        {"Rotation", "RollSensitivity", "0.5", {}},
        {"Rotation", "InvertYaw", "1", {}},
        {"Rotation", "InvertPitch", "1", {}},
        {"Rotation", "InvertRoll", "1", {}},
        {"Rotation", "LocalSmoothing", "0.3", {"-0.5", "1.5"}},
        {"Rotation", "RemoteSmoothing", "0.6", {"-0.5", "1.5"}},
        {"Camera", "FovOffset", "15.0", {"-45", "75"}},
        {"Position", "Enabled", "0", {}},
        {"Position", "SensitivityX", "0.5", {}},
        {"Position", "SensitivityY", "0.5", {}},
        {"Position", "SensitivityZ", "0.5", {}},
        {"Position", "LimitX", "0.5", {}},
        {"Position", "LimitY", "0.35", {}},
        {"Position", "LimitYDown", "0.15", {}},
        {"Position", "LimitZ", "0.6", {}},
        {"Position", "LimitZBack", "0.15", {}},
        {"Torch", "Enabled", "0", {}},
        {"Torch", "Multiplier", "1.0", {"-1", "7.5"}},
        {"Torch", "FlareFollowsBeam", "0", {}},
        {"Hotkeys", "YawModeKey", "0x2E", {"0x100"}, true},
        {"Dev", "InjectHotkeys", "1", {}},
        {"Dev", "WidgetDump", "1", {}},
    };
}

std::vector<cfg::LegacyKey> CorpusReads() {
    std::vector<cfg::LegacyKey> keys;
    for (const legacy::Key& key : legacy::ReadKeys()) keys.push_back({key.section, key.key});
    return keys;
}

struct Input {
    std::string name;
    bool present;
    std::string bytes;
};

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) throw std::logic_error("'" + from + "' is not in the text");
    return text.replace(at, from.size(), to);
}

std::vector<Input> Inputs() {
    std::vector<Input> inputs = {
        {kFirstRunName, true, FirstRun()},
        {"no file", false, {}},
        {"empty file", true, {}},
        {"LimitY=0.35 without LimitYDown", true,
         Replace(Replace(FirstRun(), "LimitY=0.20\r\n", "LimitY=0.35\r\n"), "LimitYDown=0.20\r\n", "")},
    };
    for (testing::IniMutation& m : testing::GenerateIniMutations(FirstRun(), CorpusReads(), CorpusKeys())) {
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    }
    for (int code = 0x00; code <= 0xFF; ++code) {
        char text[32];
        std::snprintf(text, sizeof(text), "YawModeKey=0x%02X", static_cast<unsigned>(code));
        inputs.push_back({text, true, Replace(FirstRun(), "YawModeKey=0x22", text)});
    }
    return inputs;
}

// ---- Comparison ------------------------------------------------------------------

void Compare(const std::vector<Input>& inputs) {
    int compared = 0;
    int fellBack = 0;
    for (const Input& input : inputs) {
        const std::string& name = input.name;
        Scratch s;
        if (input.present) s.WriteLegacy(input.bytes);
        const Record oracle = ObserveOracle(swtd_oracle::Read(s.exe_dir()));

        // v0.2.0 writes its default file where none exists, and nothing else.
        if (!input.present) {
            Check(ReadFileBytes(s.legacy()) == FirstRun(), name + ": v0.2.0's first run writes the committed first-run file");
        } else {
            Check(ReadFileBytes(s.legacy()) == input.bytes, name + ": v0.2.0 leaves an existing file as it was");
        }

        // The frozen reader, on its own copy of the input, so it finds no
        // file where the player had none.
        Scratch t;
        if (input.present) t.WriteLegacy(input.bytes);
        legacy::Config read;
        const bool present = legacy::Load(t.legacy().string(), read);
        Check(present == input.present, name + ": the frozen reader finds the file exactly when it is there");
        Check(input.present || !fs::exists(t.legacy()), name + ": the frozen reader writes no file");
        const Record imported = ObserveImport(read);

        const bool fell = input.present && LimitYDownFellBack(t.legacy());
        if (fell && imported.at("field.pos.limit_y") != Bits(0.20f)) ++fellBack;
        const std::vector<std::string> diff = Differences(Comparison1Expected(oracle, imported, fell), imported);
        for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", name.c_str(), d.c_str());
        Check(diff.empty(), name + ": comparison 1, the oracle and the import agree apart from 443fc59");
        ++compared;
    }
    std::printf("comparison 1: %d inputs, %d of them with a LimitY other than 0.20 that 443fc59 carries into LimitYDown\n",
                compared, fellBack);
    Check(fellBack > 0, "the inputs reach 443fc59's difference");
}

// ---- Comparison 2 ------------------------------------------------------------------
//
// The migration: the config owner's Load in a game folder holding only the input
// as HeadTracking.ini, which imports it through config::Import into a new
// CameraUnlock.ini, then the canonical reader and table on that file. It must
// start the mod exactly as the import did, apart from the changes core's
// data/config-format.json approves:
//
//   pose_shaping  a sensitivity or inversion the player set away from the
//                 shipped identity is dropped, and the session runs at
//                 identity. Every shipped value is identity, so nothing folds.
//   N2            a position limit that is not a number (the reader holds the
//                 limits to no range) imports as the row's default.
//   N3            a yaw key on Ctrl, Shift or Alt alone is unbound and keeps
//                 its Ctrl+Shift chord.
//
// A finite limit outside the canonical 0 to 10 has no approved rule, so the owner
// defers that import: no CameraUnlock.ini, the legacy file as it was, and the
// session on what the import gave, which is still held to the import here.
//
// A row the player never changed from what v0.2.0 shipped follows Defaults.ini:
// the import lists it in follows_defaults_ini, the tracking mode pair as one
// unit, and the migration writes it `default`. The test derives the untouched
// rows from what the frozen reader read and holds the import's list to them on
// every input. The first-run file, an empty file and no file leave every row
// untouched and give the committed file byte for byte.
//
// Each input with a file migrates three times: over a Defaults.ini the owner
// creates with the built-in values, from a read-only HeadTracking.ini, and over
// a Defaults.ini that differs from the built-in value on every global row. The
// first two give the settings the import read, since v0.2.0 shipped the
// built-in values. Over the third an untouched row takes Defaults.ini's value
// and a changed row keeps the player's.

using Drop = std::tuple<cfg::DropRule, std::string, std::string>;

// The settings the running mod acts on from a canonical Config, through this
// build's startup code: headtracking_mod.cpp ApplyConfigToSession, which leaves
// the processors at identity sensitivity and inversion, view_hook::Install, and
// mod_hotkeys.cpp Register, which puts each list through ParseKeyBindings and
// RegisterKeyBindings.
Record ObserveCanonical(const swtd_ht::Config& c) {
    swtd_oracle::Published g;
    g.udp_port = c.udp_port;
    g.tracking_enabled = c.enable_on_startup;
    g.world_space_yaw = c.world_space_yaw;
    g.yaw_sens = 1.0f;
    g.pitch_sens = 1.0f;
    g.roll_sens = 1.0f;
    g.local_smoothing = c.local_smoothing;
    g.remote_smoothing = c.remote_smoothing;
    g.fov_offset = c.fov_offset;
    g.tracking_mode = static_cast<int>(swtd_ht::config::StartupTrackingMode(c));
    g.pos_sens_x = 1.0f;
    g.pos_sens_y = 1.0f;
    g.pos_sens_z = 1.0f;
    g.limit_x = c.position_limit_x;
    g.limit_y = c.position_limit_y;
    g.limit_y_down = c.position_limit_y_down;
    g.limit_z = c.position_limit_z;
    g.limit_z_back = c.position_limit_z_back;
    g.torch_follows_head = c.light_follows_head;
    g.torch_multiplier = c.light_multiplier;
    g.torch_flare_follows_beam = c.flare_follows_beam;
    g.widget_dump = c.widget_dump;
    const std::pair<int, const std::string*> lists[] = {
        {swtd_oracle::kToggle, &c.toggle_key},
        {swtd_oracle::kCycleMode, &c.cycle_tracking_mode_key},
        {swtd_oracle::kYawMode, &c.yaw_mode_key},
        {swtd_oracle::kInjectNext, &c.inject_next_key},
        {swtd_oracle::kInjectPrevious, &c.inject_previous_key},
    };
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        Check(parsed.ok(), "a migrated key list parses: " + *list);
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            g.hotkeys.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    return ObserveOracle(g);
}

// The drops the approved changes call for, from what the frozen reader read, and
// the settings the session then runs on: comparison 2's whole allowance.
struct Allowed {
    std::vector<Drop> dropped;
    Record observed;
    // A finite limit the canonical rows cannot hold.
    bool deferred = false;
};

Allowed ApplyApprovedChanges(const legacy::Config& read) {
    Allowed a;
    legacy::Config c = read;
    const auto shaping = [&a](auto& value, auto shipped, const char* section, const char* key) {
        if (value != shipped) a.dropped.push_back({cfg::DropRule::PoseShaping, section, key});
        value = shipped;
    };
    shaping(c.yaw_sensitivity, 1.0f, "Rotation", "YawSensitivity");
    shaping(c.pitch_sensitivity, 1.0f, "Rotation", "PitchSensitivity");
    shaping(c.roll_sensitivity, 1.0f, "Rotation", "RollSensitivity");
    shaping(c.invert_yaw, false, "Rotation", "InvertYaw");
    shaping(c.invert_pitch, false, "Rotation", "InvertPitch");
    shaping(c.invert_roll, false, "Rotation", "InvertRoll");
    shaping(c.position_sensitivity_x, 1.0f, "Position", "SensitivityX");
    shaping(c.position_sensitivity_y, 1.0f, "Position", "SensitivityY");
    shaping(c.position_sensitivity_z, 1.0f, "Position", "SensitivityZ");

    const int yaw = c.yaw_mode_key;
    if ((yaw >= 0x10 && yaw <= 0x12) || (yaw >= 0xA0 && yaw <= 0xA5)) {
        a.dropped.push_back({cfg::DropRule::ModifierKey, "Hotkeys", "YawModeKey"});
        c.yaw_mode_key = 0;
    }

    const swtd_ht::Config defaults = swtd_ht::config::Table().defaults();
    const auto limit = [&a](float& value, float row_default, const char* key) {
        if (!std::isfinite(value)) {
            a.dropped.push_back({cfg::DropRule::NonFiniteNumber, "Position", key});
            value = row_default;
        } else if (value < 0.0f || value > 10.0f) {
            a.deferred = true;
        }
    };
    limit(c.limit_x, defaults.position_limit_x, "LimitX");
    limit(c.limit_y, defaults.position_limit_y, "LimitY");
    limit(c.limit_y_down, defaults.position_limit_y_down, "LimitYDown");
    limit(c.limit_z, defaults.position_limit_z, "LimitZ");
    limit(c.limit_z_back, defaults.position_limit_z_back, "LimitZBack");

    std::sort(a.dropped.begin(), a.dropped.end());
    a.observed = ObserveImport(c);
    return a;
}

std::vector<std::string> CanonicalDiagnostics(const std::string& bytes, swtd_ht::Config& out) {
    std::vector<std::string> found;
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(bytes);
    for (const cfg::CanonicalDiagnostic& d : doc.diagnostics) found.push_back("reader: " + cfg::DescribeCanonicalDiagnostic(d));
    const cfg::ConfigTable<swtd_ht::Config> table = swtd_ht::config::Table();
    out = table.defaults();
    for (const cfg::CanonicalDiagnostic& d : cfg::ApplyCanonical(doc, table, out).diagnostics) {
        found.push_back("table: " + cfg::DescribeCanonicalDiagnostic(d));
    }
    return found;
}

bool AsciiCrlf(const std::string& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c > 0x7E) return false;
        if (c == '\r' && (i + 1 == bytes.size() || bytes[i + 1] != '\n')) return false;
        if (c == '\n' && (i == 0 || bytes[i - 1] != '\r')) return false;
        if (c < 0x20 && c != '\r' && c != '\n') return false;
    }
    return !bytes.empty() && bytes.back() == '\n';
}

// A file's bytes, last write time and attributes, which no load may change.
struct FileState {
    std::string bytes;
    unsigned long long written = 0;
    DWORD attributes = 0;
    bool operator==(const FileState& other) const {
        return bytes == other.bytes && written == other.written && attributes == other.attributes;
    }
};

std::optional<FileState> StateOf(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("cannot read the attributes of " + path.string());
    }
    FileState state;
    state.bytes = ReadFileBytes(path);
    state.written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                    data.ftLastWriteTime.dwLowDateTime;
    state.attributes = data.dwFileAttributes;
    return state;
}

std::set<std::string> Names(const fs::path& folder) {
    std::set<std::string> names;
    for (const auto& entry : fs::directory_iterator(folder)) names.insert(entry.path().filename().string());
    return names;
}

bool LogSays(const std::vector<std::string>& log, const std::string& text) {
    for (const std::string& line : log) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// A Defaults.ini holding a value other than the built-in one on every global row
// the table binds, so a migration that wrote `default` where the imported value
// is not what `default` gives would read back differently over it.
const char* const kSkewedDefaults =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5252\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=false\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.5\r\nRemoteSmoothing=0.5\r\n\r\n"
    "[Position]\r\nPositionEnabled=true\r\nPositionLimitX=0.5\r\nPositionLimitY=0.5\r\nPositionLimitYDown=0.5\r\n"
    "PositionLimitZ=0.5\r\nPositionLimitZBack=0.5\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\n\r\n"
    "[Light]\r\nLightFollowsHead=false\r\nLightMultiplier=0.5\r\n";

using cfg::schema::Concept;

// Every row the table binds that follows Defaults.ini.
const std::set<Concept>& GlobalRows() {
    static const std::set<Concept> rows = {
        Concept::UdpPort,          Concept::EnableOnStartup,    Concept::WorldSpaceYaw,
        Concept::RotationEnabled,  Concept::PositionEnabled,    Concept::LocalSmoothing,
        Concept::RemoteSmoothing,  Concept::PositionLimitX,     Concept::PositionLimitY,
        Concept::PositionLimitYDown, Concept::PositionLimitZ,   Concept::PositionLimitZBack,
        Concept::ToggleKey,        Concept::CycleTrackingModeKey, Concept::YawModeKey,
        Concept::LightFollowsHead, Concept::LightMultiplier,
    };
    return rows;
}

// The rows the player never changed from what v0.2.0 shipped, the mode pair as
// one unit. The toggle and mode hotkeys were bound in code, so no player
// changed them.
std::set<Concept> UntouchedRows(const legacy::Config& l) {
    const legacy::Config d;
    std::set<Concept> changed;
    const auto differs = [&changed](bool different, std::initializer_list<Concept> rows) {
        if (different) changed.insert(rows);
    };
    differs(l.udp_port != d.udp_port, {Concept::UdpPort});
    differs(l.enable_on_startup != d.enable_on_startup, {Concept::EnableOnStartup});
    differs(l.world_space_yaw != d.world_space_yaw, {Concept::WorldSpaceYaw});
    differs(l.position_enabled != d.position_enabled, {Concept::RotationEnabled, Concept::PositionEnabled});
    differs(l.local_smoothing != d.local_smoothing, {Concept::LocalSmoothing});
    differs(l.remote_smoothing != d.remote_smoothing, {Concept::RemoteSmoothing});
    differs(l.limit_x != d.limit_x, {Concept::PositionLimitX});
    differs(l.limit_y != d.limit_y, {Concept::PositionLimitY});
    differs(l.limit_y_down != d.limit_y_down, {Concept::PositionLimitYDown});
    differs(l.limit_z != d.limit_z, {Concept::PositionLimitZ});
    differs(l.limit_z_back != d.limit_z_back, {Concept::PositionLimitZBack});
    differs(l.yaw_mode_key != d.yaw_mode_key, {Concept::YawModeKey});
    differs(l.torch_follows_head != d.torch_follows_head, {Concept::LightFollowsHead});
    differs(l.torch_multiplier != d.torch_multiplier, {Concept::LightMultiplier});
    std::set<Concept> untouched;
    for (const Concept row : GlobalRows()) {
        if (changed.count(row) == 0) untouched.insert(row);
    }
    return untouched;
}

std::string ConceptNames(const std::set<Concept>& rows) {
    std::string text;
    for (const Concept row : rows) {
        text += (text.empty() ? "" : ", ") + std::string(cfg::schema::kConcepts[static_cast<std::size_t>(row)].name);
    }
    return text.empty() ? "none" : text;
}

// `row`'s field copied from `from` into `to`.
void CopyRow(Concept row, const swtd_ht::Config& from, swtd_ht::Config& to) {
    switch (row) {
        case Concept::UdpPort: to.udp_port = from.udp_port; break;
        case Concept::EnableOnStartup: to.enable_on_startup = from.enable_on_startup; break;
        case Concept::WorldSpaceYaw: to.world_space_yaw = from.world_space_yaw; break;
        case Concept::RotationEnabled: to.rotation_enabled = from.rotation_enabled; break;
        case Concept::PositionEnabled: to.position_enabled = from.position_enabled; break;
        case Concept::LocalSmoothing: to.local_smoothing = from.local_smoothing; break;
        case Concept::RemoteSmoothing: to.remote_smoothing = from.remote_smoothing; break;
        case Concept::PositionLimitX: to.position_limit_x = from.position_limit_x; break;
        case Concept::PositionLimitY: to.position_limit_y = from.position_limit_y; break;
        case Concept::PositionLimitYDown: to.position_limit_y_down = from.position_limit_y_down; break;
        case Concept::PositionLimitZ: to.position_limit_z = from.position_limit_z; break;
        case Concept::PositionLimitZBack: to.position_limit_z_back = from.position_limit_z_back; break;
        case Concept::ToggleKey: to.toggle_key = from.toggle_key; break;
        case Concept::CycleTrackingModeKey: to.cycle_tracking_mode_key = from.cycle_tracking_mode_key; break;
        case Concept::YawModeKey: to.yaw_mode_key = from.yaw_mode_key; break;
        case Concept::LightFollowsHead: to.light_follows_head = from.light_follows_head; break;
        case Concept::LightMultiplier: to.light_multiplier = from.light_multiplier; break;
        default: throw std::logic_error("no field for a row the table does not bind");
    }
}

// The settings kSkewedDefaults gives, each checked to differ from the built-in
// value, the tracking mode pair taken together.
swtd_ht::Config SkewedConfig() {
    swtd_ht::Config skewed;
    const std::vector<std::string> diagnostics = CanonicalDiagnostics(kSkewedDefaults, skewed);
    if (!diagnostics.empty()) throw std::logic_error("the skewed Defaults.ini draws " + diagnostics.front());
    const swtd_ht::Config builtin = swtd_ht::config::Table().defaults();
    for (const Concept row : GlobalRows()) {
        swtd_ht::Config probe = builtin;
        CopyRow(row, skewed, probe);
        if (row == Concept::RotationEnabled || row == Concept::PositionEnabled) {
            CopyRow(Concept::RotationEnabled, skewed, probe);
            CopyRow(Concept::PositionEnabled, skewed, probe);
        }
        if (Differences(ObserveCanonical(probe), ObserveCanonical(builtin)).empty()) {
            throw std::logic_error(std::string("the skewed Defaults.ini leaves ") +
                                   cfg::schema::kConcepts[static_cast<std::size_t>(row)].name + " at the built-in value");
        }
    }
    return skewed;
}

// The folder beside this executable the migrated files are written to, for
// lint-migrated.mjs, which CTest runs after this test.
fs::path MigratedFolder() {
    std::vector<wchar_t> exe(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (length == 0) throw std::runtime_error("cannot find this executable's path");
        if (length < exe.size()) return fs::path(std::wstring(exe.data(), length)).parent_path() / "migrated";
        exe.resize(exe.size() * 2);
    }
}

cfg::ConfigOwnerOptions<swtd_ht::Config> OwnerOptions(const Scratch& s) {
    return swtd_ht::config::OwnerOptions(s.game().wstring(), cfg::DefaultsFile::At(s.defaults().wstring()));
}

// Runs the owner's Load in `s`, whose game folder holds the input as
// HeadTracking.ini or nothing, checks what a load must do beyond comparison 2,
// and returns the settings the session runs on. A file it creates by migrating
// goes into `migrated_files`.
swtd_ht::Config Migrate(const Input& input, bool deferred, const Scratch& s, const std::string& label,
                        std::set<std::string>& migrated_files) {
    const std::optional<FileState> legacy_before = StateOf(s.legacy());
    const std::optional<FileState> defaults_before = StateOf(s.defaults());

    const cfg::ConfigLoadResult<swtd_ht::Config> loaded = cfg::ConfigOwner<swtd_ht::Config>(OwnerOptions(s)).Load();
    const cfg::ConfigLoadStatus want = !input.present ? cfg::ConfigLoadStatus::Created
                                     : deferred       ? cfg::ConfigLoadStatus::Deferred
                                                      : cfg::ConfigLoadStatus::Migrated;
    if (loaded.status != want) {
        std::printf("  %s: %s, %s\n", label.c_str(), cfg::ConfigLoadStatusName(loaded.status), loaded.reason.c_str());
    }
    Check(loaded.status == want, label + ": the load is " + cfg::ConfigLoadStatusName(want));
    Check(StateOf(s.legacy()) == legacy_before, label + ": a load leaves HeadTracking.ini's bytes, write time and attributes");
    Check(!defaults_before || StateOf(s.defaults()) == defaults_before, label + ": a load leaves Defaults.ini as it was");
    if (loaded.status != want) return loaded.config;

    if (deferred) {
        Check(Names(s.game()) == std::set<std::string>{"HeadTracking.ini"},
              label + ": a deferred import leaves HeadTracking.ini alone in the game folder");
        return loaded.config;
    }
    Check(Names(s.game()) == (input.present ? std::set<std::string>{"CameraUnlock.ini", "HeadTracking.ini"}
                                            : std::set<std::string>{"CameraUnlock.ini"}),
          label + ": the game folder holds HeadTracking.ini and CameraUnlock.ini and nothing else");

    const std::string migrated = ReadFileBytes(s.canonical());
    Check(cfg::HasCanonicalStamp(migrated), label + ": CameraUnlock.ini carries the stamp");
    Check(AsciiCrlf(migrated), label + ": CameraUnlock.ini is ASCII with CRLF line ends");
    swtd_ht::Config reread;
    const std::vector<std::string> diagnostics = CanonicalDiagnostics(migrated, reread);
    for (const std::string& d : diagnostics) std::printf("  %s: CameraUnlock.ini, %s\n", label.c_str(), d.c_str());
    Check(diagnostics.empty(), label + ": CameraUnlock.ini reads with no diagnostic");
    if (input.present) migrated_files.insert(migrated);

    // The next start reads CameraUnlock.ini, imports nothing and writes nothing.
    const std::optional<FileState> created = StateOf(s.canonical());
    const cfg::ConfigLoadResult<swtd_ht::Config> again = cfg::ConfigOwner<swtd_ht::Config>(OwnerOptions(s)).Load();
    Check(again.status == cfg::ConfigLoadStatus::Canonical, label + ": the next start reads CameraUnlock.ini");
    Check(Differences(ObserveCanonical(again.config), ObserveCanonical(loaded.config)).empty(),
          label + ": the next start runs on the same settings");
    Check(StateOf(s.canonical()) == created && StateOf(s.legacy()) == legacy_before,
          label + ": the next start changes neither file");
    Check(!input.present || LogSays(again.log, "is left as it was and is not read"),
          label + ": the next start logs that HeadTracking.ini is not read");
    return loaded.config;
}

void ImportAgainstMigration(const std::vector<Input>& inputs) {
    const std::string committed = ReadFileBytes(fs::path(SWTD_SOURCE_DIR) / "CameraUnlock.ini");
    const cfg::ConfigTable<swtd_ht::Config> table = swtd_ht::config::Table();
    const swtd_ht::Config skewed_values = SkewedConfig();
    std::set<std::string> migrated_files;
    int compared = 0;
    int deferred = 0;
    int dropping = 0;
    int all_untouched = 0;
    int mode_changed = 0;
    int modifier_keys = 0;
    for (const Input& input : inputs) {
        const std::string& name = input.name;

        // The import, run as the owner runs it but on a read-only copy: it reads
        // what the frozen reader reads, drops what the approved changes drop, and
        // writes nothing.
        cfg::ImportResult imported;
        legacy::Config read;
        {
            Scratch ro;
            if (input.present) {
                ro.WriteLegacy(input.bytes);
                SetFileAttributesW(ro.legacy().c_str(), FILE_ATTRIBUTE_READONLY);
            }
            const std::optional<FileState> before = StateOf(ro.legacy());
            swtd_ht::Config unused = table.defaults();
            imported = swtd_ht::config::Import().run({ro.legacy().wstring(), ro.legacy().string(), false}, unused);
            const std::set<std::string> left = input.present ? std::set<std::string>{"HeadTracking.ini"}
                                                             : std::set<std::string>{};
            Check(StateOf(ro.legacy()) == before && Names(ro.game()) == left,
                  name + ": the import leaves a read-only folder as it was");
            legacy::Load(ro.legacy().string(), read);
        }
        Check(imported.status == (input.present ? cfg::ImportStatus::Imported : cfg::ImportStatus::Absent),
              name + ": the import reads every input, as the published build did");

        const std::set<Concept> follows(imported.follows_defaults_ini.begin(), imported.follows_defaults_ini.end());
        Check(follows.size() == imported.follows_defaults_ini.size(), name + ": follows_defaults_ini names a row twice");
        const std::set<Concept> untouched = UntouchedRows(read);
        Check(follows == untouched, name + ": follows Defaults.ini " + ConceptNames(follows) + ", the player left " +
                                        ConceptNames(untouched) + " untouched");
        if (untouched == GlobalRows()) ++all_untouched;
        if (untouched.count(Concept::RotationEnabled) == 0) ++mode_changed;
        const bool unedited = name == kFirstRunName || name == "no file" || name == "empty file";
        if (unedited) Check(untouched == GlobalRows(), name + ": a file no player edited leaves a row changed");

        const Allowed allowed = ApplyApprovedChanges(read);
        if (!allowed.dropped.empty()) ++dropping;
        for (const Drop& d : allowed.dropped) {
            if (std::get<0>(d) == cfg::DropRule::ModifierKey) ++modifier_keys;
        }
        if (allowed.deferred) ++deferred;
        std::vector<Drop> dropped;
        for (const cfg::DroppedValue& d : imported.dropped) dropped.push_back({d.rule, d.section, d.key});
        std::sort(dropped.begin(), dropped.end());
        Check(dropped == allowed.dropped, name + ": the import drops exactly what the approved changes drop");
        Check(imported.pose_shaping.size() == 9, name + ": the import records all nine pose-shaping settings");
        for (const cfg::PoseShapingValue& p : imported.pose_shaping) {
            const bool changed = std::find(allowed.dropped.begin(), allowed.dropped.end(),
                                           Drop{cfg::DropRule::PoseShaping, p.section, p.key}) != allowed.dropped.end();
            Check(p.folded != changed, name + ": [" + p.section + "] " + p.key + " folds exactly when it is the shipped value");
        }

        // Over a Defaults.ini the owner creates with the built-in values.
        swtd_ht::Config migrated;
        {
            Scratch s;
            if (input.present) s.WriteLegacy(input.bytes);
            migrated = Migrate(input, allowed.deferred, s, name, migrated_files);
            const std::vector<std::string> diff = Differences(allowed.observed, ObserveCanonical(migrated));
            for (const std::string& d : diff) std::printf("  comparison 2, %s: %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": comparison 2, the migration runs as the import read, less the approved changes");

            if (!allowed.deferred) {
                // Over the built-in values the table's own defaults stand for Defaults.ini.
                swtd_ht::Config reread;
                CanonicalDiagnostics(ReadFileBytes(s.canonical()), reread);
                Check(Differences(ObserveCanonical(reread), ObserveCanonical(migrated)).empty(),
                      name + ": CameraUnlock.ini reads back as the settings the session runs on");
                const std::string bytes = ReadFileBytes(s.canonical());
                for (const Concept row : follows) {
                    const std::string key = cfg::schema::kConcepts[static_cast<std::size_t>(row)].key;
                    Check(bytes.find("\r\n" + key + "=default\r\n") != std::string::npos,
                          name + ": " + key + " is not written default");
                }
            }

            // Fresh equals upgrade: the published build's first-run file, an
            // empty file and no file at all end as the committed file.
            if (unedited) {
                Check(ReadFileBytes(s.canonical()) == committed, name + ": gives the committed file byte for byte");
            }
        }

        if (!input.present) {
            ++compared;
            continue;
        }

        // From a read-only HeadTracking.ini, which keeps its attribute.
        {
            Scratch ro;
            ro.WriteLegacy(input.bytes);
            SetFileAttributesW(ro.legacy().c_str(), FILE_ATTRIBUTE_READONLY);
            const swtd_ht::Config c = Migrate(input, allowed.deferred, ro, name + " (read-only)", migrated_files);
            Check(Differences(allowed.observed, ObserveCanonical(c)).empty(),
                  name + ": a read-only HeadTracking.ini imports as a writable one does");
            Check((GetFileAttributesW(ro.legacy().c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
                  name + ": HeadTracking.ini keeps its read-only attribute");
        }

        // Over a Defaults.ini that differs everywhere, an untouched row takes its
        // value and a changed row keeps the player's.
        {
            Scratch skewed;
            skewed.WriteLegacy(input.bytes);
            skewed.WriteDefaults(kSkewedDefaults);
            const swtd_ht::Config c =
                Migrate(input, allowed.deferred, skewed, name + " (skewed Defaults.ini)", migrated_files);
            swtd_ht::Config want = migrated;
            for (const Concept row : follows) CopyRow(row, skewed_values, want);
            const std::vector<std::string> diff = Differences(ObserveCanonical(want), ObserveCanonical(c));
            for (const std::string& d : diff) std::printf("  comparison 2, %s (skewed Defaults.ini): %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": over a Defaults.ini that differs everywhere, the untouched rows take its "
                                       "values and the changed rows keep the player's");
        }
        ++compared;
    }
    std::printf("comparison 2: %d inputs, %d with a value the approved changes drop, %d deferred on a limit "
                "outside 0 to 10\n", compared, dropping, deferred);
    Check(dropping > 0 && deferred > 0, "the inputs reach the approved drops and the deferral");
    std::printf("  %d inputs left every row at v0.2.0's default, %d changed the tracking mode, %d put the yaw key on "
                "a modifier key alone\n", all_untouched, mode_changed, modifier_keys);
    Check(all_untouched > 0 && mode_changed > 0 && all_untouched < compared,
          "the inputs both leave rows untouched and change them, the tracking mode among them");
    Check(modifier_keys > 0, "the inputs reach a yaw key on a modifier key alone");

    // Core's canonical config lint runs over these next (lint-migrated.mjs).
    const fs::path lint = MigratedFolder();
    fs::remove_all(lint);
    fs::create_directories(lint);
    std::size_t n = 0;
    for (const std::string& file : migrated_files) {
        WriteFileBytes(lint / (std::to_string(n++) + ".ini"), file);
    }
    std::printf("%zu distinct migrated files written to %s\n", migrated_files.size(), lint.string().c_str());
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        const std::vector<Input> inputs = Inputs();
        Compare(inputs);
        ImportAgainstMigration(inputs);
        RemoveTree(ScratchRoot());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
