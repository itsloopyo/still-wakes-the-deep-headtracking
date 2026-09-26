// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// v0.2.0's reader and startup code (tag v0.2.0, commit a18bd1a; the rolling dev
// build 93cb371 and the v0.1.0 tag have the same src/ and core pin).
//
// src/config.cpp, src/config.h and src/logging.h beside this file are byte
// copies of v0.2.0's, compiled here as they shipped, with their namespace
// renamed by the macro below so they can sit in one program beside this
// build's swtd_ht. Every cameraunlock-core source they include holds the same
// bytes at v0.2.0's pin (0f7a634) and at this repo's (CMakeLists.txt checks
// both). What is transcribed is the startup code that consumed the settings,
// which cannot be compiled into a test because it hooks the game:
//
//   src/headtracking_mod.cpp  lines 43-75    ApplyConfigToSession
//                             lines 102-105  LoadConfig
//                             lines 178-183  the torch and hotkey installs
//   src/view_hook.cpp         lines 469-470  the startup enable and yaw mode
//   src/mod_hotkeys.cpp       lines 79-95    the hotkey registrations, as data

#define swtd_ht swtd_ht_v020
#include "src/config.cpp"
#undef swtd_ht

#include "oracle_reader.h"

namespace swtd_oracle {

namespace {

constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;
constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;
constexpr int kVkU = 0x55;
constexpr int kVkJ = 0x4A;

constexpr unsigned kNav = 0;
constexpr unsigned kChord = 3;

}  // namespace

Published Read(const std::string& exe_dir) {
    swtd_ht_v020::Config g_config;
    swtd_ht_v020::config_write_default_if_missing(exe_dir);
    swtd_ht_v020::config_load(exe_dir, g_config);

    Published p;
    p.udp_port = g_config.udp_port;
    p.tracking_enabled = g_config.enable_on_startup;
    p.world_space_yaw = g_config.world_space_yaw;

    p.yaw_sens = g_config.yaw_sensitivity;
    p.pitch_sens = g_config.pitch_sensitivity;
    p.roll_sens = g_config.roll_sensitivity;
    p.invert_yaw = g_config.invert_yaw;
    p.invert_pitch = g_config.invert_pitch;
    p.invert_roll = g_config.invert_roll;

    p.pos_sens_x = g_config.position_sensitivity_x;
    p.pos_sens_y = g_config.position_sensitivity_y;
    p.pos_sens_z = g_config.position_sensitivity_z;
    p.limit_x = g_config.limit_x;
    p.limit_y = g_config.limit_y;
    p.limit_y_down = g_config.limit_y_down;
    p.limit_z = g_config.limit_z;
    p.limit_z_back = g_config.limit_z_back;

    p.local_smoothing = g_config.local_smoothing;
    p.remote_smoothing = g_config.remote_smoothing;

    p.tracking_mode = g_config.position_enabled ? 0 : 1;

    p.fov_offset = g_config.fov_offset;
    p.torch_follows_head = g_config.torch_follows_head;
    p.torch_multiplier = g_config.torch_multiplier;
    p.torch_flare_follows_beam = g_config.torch_flare_follows_beam;
    p.widget_dump = g_config.widget_dump;

    p.hotkeys.push_back({kToggle, kVkEnd, kNav});
    p.hotkeys.push_back({kCycleMode, kVkPageUp, kNav});
    p.hotkeys.push_back({kYawMode, g_config.yaw_mode_key, kNav});
    p.hotkeys.push_back({kToggle, kVkY, kChord});
    p.hotkeys.push_back({kCycleMode, kVkG, kChord});
    p.hotkeys.push_back({kYawMode, kVkH, kChord});
    if (g_config.inject_hotkeys) {
        p.hotkeys.push_back({kInjectNext, kVkU, kChord});
        p.hotkeys.push_back({kInjectPrevious, kVkJ, kChord});
    }
    return p;
}

}  // namespace swtd_oracle
