// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>
#include <tuple>
#include <vector>

// The oracle: what v0.2.0, the newest published build, ran on after reading
// HeadTracking.ini. oracle_reader.cpp compiles its reader and transcribes the
// startup code that consumed it.
namespace swtd_oracle {

enum Action { kToggle = 0, kCycleMode = 1, kYawMode = 2, kInjectNext = 3, kInjectPrevious = 4 };

// One HotkeyPoller registration: the action, the code, and 3 where the
// callback is ChordGuarded (fires only while Ctrl and Shift are both held), 0
// where it is NavGuarded (fires unless Ctrl and Shift are both held).
using Registration = std::tuple<int, int, unsigned>;

struct Published {
    int udp_port = 0;
    bool tracking_enabled = false;
    bool world_space_yaw = false;
    float yaw_sens = 0, pitch_sens = 0, roll_sens = 0;
    bool invert_yaw = false, invert_pitch = false, invert_roll = false;
    float local_smoothing = 0, remote_smoothing = 0;
    float fov_offset = 0;
    // The mode ApplyConfigToSession handed the session: 0 rotation and
    // position, 1 rotation only (cameraunlock::TrackingMode's numbers).
    int tracking_mode = 0;
    float pos_sens_x = 0, pos_sens_y = 0, pos_sens_z = 0;
    float limit_x = 0, limit_y = 0, limit_y_down = 0, limit_z = 0, limit_z_back = 0;
    bool torch_follows_head = false;
    float torch_multiplier = 0;
    bool torch_flare_follows_beam = false;
    bool widget_dump = false;
    std::vector<Registration> hotkeys;
};

// `exe_dir` is the folder the game exe runs from, with no trailing backslash,
// as ExeDirNarrow returned it. Like v0.2.0, this writes the default file there
// first when none exists.
Published Read(const std::string& exe_dir);

}  // namespace swtd_oracle
