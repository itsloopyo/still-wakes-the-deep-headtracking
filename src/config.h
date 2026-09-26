// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

#include "cameraunlock/config/config_concepts.g.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace swtd_ht {

// The settings CameraUnlock.ini holds, at their defaults.
struct Config {
    int udp_port = 4242;
    bool enable_on_startup = true;

    // true = yaw turns about the world up-axis, so looking at the floor and
    // turning your head still pans across it. false = yaw turns about the
    // camera's own up-axis, which leans the horizon on a pitched turn.
    bool world_space_yaw = true;

    // The tracking mode at startup, the pair the mode hotkey saves.
    bool rotation_enabled = true;
    bool position_enabled = true;

    // Smoothing is picked per connection from the packet source address: a
    // tracker on this machine (loopback) uses local_smoothing, a remote network
    // device uses remote_smoothing. Both cover rotation and position.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    float position_limit_x = cameraunlock::PositionSettings{}.limit_x;
    float position_limit_y = cameraunlock::PositionSettings{}.limit_y;
    float position_limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    float position_limit_z = cameraunlock::PositionSettings{}.limit_z;
    float position_limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    std::string toggle_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::ToggleKey>::kCanonicalDefault;
    std::string cycle_tracking_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::CycleTrackingModeKey>::kCanonicalDefault;
    std::string yaw_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::YawModeKey>::kCanonicalDefault;

    // The torch points where the head is looking rather than where the mouse
    // is aiming, by light_multiplier times the head pose. The default leads the
    // view: turning your head puts your eyes off the centre of the screen, so a
    // beam aligned with the view lands short of what you are looking at.
    // 1.0 matches the view exactly, 0.0 leaves the beam where the game aimed it.
    // The number and the reasoning are the fleet's, not this game's - see
    // cameraunlock/effects/head_follow_light.h.
    bool light_follows_head = true;
    float light_multiplier = cameraunlock::effects::kDefaultLightMultiplier;

    // The torch's glare card is a sibling of the spring arm rather than a child
    // of it, so aiming the arm with the head leaves the glare behind on the
    // torch root and it reads as pinned to the world. Re-parent it onto the arm
    // so it travels with the beam. It picks up the arm's rotation lag and the
    // game's torch wander in the process, which on the root it did not have.
    bool flare_follows_beam = true;

    // Degrees added to the field of view the game renders with. Still Wakes
    // the Deep has no FOV control of its own - its settings object carries
    // ColourBlindMode, ReticleSize, HeadRollAmount and the rest, and nothing
    // for FOV - so the only route to a wider view is the mod's. 0.0 leaves the
    // game's own FOV alone. This is an offset rather than an absolute value so
    // that whatever the game does with its own FOV survives: the exe carries a
    // HabitatMovementCameraFOVData block with AdditionalFOV and velocity
    // thresholds in it, and pinning FOV to one number would flatten it.
    float fov_offset = 0.0f;

    // Dev only: keys that step which GetPlayerViewPoint caller gets the head
    // pose, forward and back. Only useful for re-confirming the render caller
    // after a game patch, so unbound unless asked for.
    std::string inject_next_key;
    std::string inject_previous_key;

    // Dev only: periodically list the live UMG objects whose name or class
    // looks like a reticle or interaction prompt, so the widgets to move can be
    // identified. Their names live in cooked Blueprint assets, so they cannot
    // be read out of the EXE.
    bool widget_dump = false;
};

}  // namespace swtd_ht

// CameraUnlock.ini, beside the game exe, in cameraunlock-core's canonical config
// format. One ConfigOwner reads and writes it; nothing else in the mod touches
// it. HeadTracking.ini, the file every earlier build read, is imported once
// while CameraUnlock.ini is absent and is never written.
namespace swtd_ht::config {

cameraunlock::config::ConfigTable<Config> Table();

cameraunlock::config::RenderHeader Header();

// HeadTracking.ini through the frozen reader in src/legacy_config/, mapped into
// Config.
cameraunlock::config::LegacyImport<Config> Import();

// The owner's options for CameraUnlock.ini in `exe_dir`, a full path, with
// HeadTracking.ini beside it as the legacy file and Defaults.ini where
// `defaults` says.
cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& exe_dir,
                                                              cameraunlock::config::DefaultsFile defaults);

// Reads, imports or creates CameraUnlock.ini in `exe_dir`, logs what the owner
// reports, and returns the settings the session runs on. Call once, from the
// bootstrap thread, with the log open. `defaults` is DefaultsFile::PerUser() in
// the mod.
Config Load(const std::wstring& exe_dir, cameraunlock::config::DefaultsFile defaults);

// The tracking mode the settings start in. The table never gives both rows
// false.
cameraunlock::TrackingMode StartupTrackingMode(const Config& config);

// Saves the value a hotkey has just applied. The session keeps it whether or
// not the save succeeds; a failed save is logged. Called on the hotkey thread.
void SaveWorldSpaceYaw(bool world_space_yaw);
void SaveTrackingMode(cameraunlock::TrackingMode mode);

}  // namespace swtd_ht::config
