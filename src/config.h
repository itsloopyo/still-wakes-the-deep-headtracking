// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace swtd_ht {

struct Config {
    int udp_port = 4242;
    bool enable_on_startup = true;

    // true = yaw turns about the world up-axis, so looking at the floor and
    // turning your head still pans across it. false = yaw turns about the
    // camera's own up-axis, which leans the horizon on a pitched turn.
    // Runtime-toggleable; this is only the value the mod starts in.
    bool world_space_yaw = true;

    float yaw_sensitivity = 1.0f;
    float pitch_sensitivity = 1.0f;
    float roll_sensitivity = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;

    // Smoothing is picked per connection from the packet source address: a
    // tracker on this machine (loopback) uses local_smoothing, a remote network
    // device uses remote_smoothing. Both cover rotation and position.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    // Degrees added to the field of view the game renders with. Still Wakes
    // the Deep has no FOV control of its own - its settings object carries
    // ColourBlindMode, ReticleSize, HeadRollAmount and the rest, and nothing
    // for FOV - so the only route to a wider view is the mod's. 0.0 leaves the
    // game's own FOV alone. This is an offset rather than an absolute value so
    // that whatever the game does with its own FOV survives: the exe carries a
    // HabitatMovementCameraFOVData block with AdditionalFOV and velocity
    // thresholds in it, and pinning FOV to one number would flatten it.
    float fov_offset = 0.0f;

    bool position_enabled = true;
    float position_sensitivity_x = 1.0f;
    float position_sensitivity_y = 1.0f;
    float position_sensitivity_z = 1.0f;
    float limit_x = cameraunlock::PositionSettings{}.limit_x;
    float limit_y = cameraunlock::PositionSettings{}.limit_y;
    float limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    float limit_z = cameraunlock::PositionSettings{}.limit_z;
    float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    // The torch points where the head is looking rather than where the mouse
    // is aiming, by torch_multiplier times the head pose. The default leads the
    // view: turning your head puts your eyes off the centre of the screen, so a
    // beam aligned with the view lands short of what you are looking at.
    // 1.0 matches the view exactly, 0.0 leaves the beam where the game aimed it.
    // The number and the reasoning are the fleet's, not this game's - see
    // cameraunlock/effects/head_follow_light.h.
    bool torch_follows_head = true;
    float torch_multiplier = cameraunlock::effects::kDefaultLightMultiplier;

    // The torch's glare card is a sibling of the spring arm rather than a child
    // of it, so aiming the arm with the head leaves the glare behind on the
    // torch root and it reads as pinned to the world. Re-parent it onto the arm
    // so it travels with the beam. It picks up the arm's rotation lag and the
    // game's torch wander in the process, which on the root it did not have.
    bool torch_flare_follows_beam = true;

    // Ctrl+Shift+U / Ctrl+Shift+J cycle which GetPlayerViewPoint caller gets
    // the head pose. Only useful for re-confirming the render caller after a
    // game patch, so off unless asked for.
    bool inject_hotkeys = false;

    // Dev only: periodically list the live UMG objects whose name or class
    // looks like a reticle or interaction prompt, so the widgets to move can be
    // identified. Their names live in cooked Blueprint assets, so they cannot
    // be read out of the EXE.
    bool widget_dump = false;

    // Virtual-key code for the yaw-mode toggle. Ctrl+Shift+H does the same job
    // and is not configurable.
    int yaw_mode_key = 0x22;  // VK_NEXT (Page Down)
};

void config_load(const std::string& exe_dir, Config& out);
void config_write_default_if_missing(const std::string& exe_dir);

}  // namespace swtd_ht
