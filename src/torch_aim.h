// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace swtd_ht::TorchAim {

// Point the torch where the player is looking rather than where they are
// aiming. Hooks USpringArmComponent::GetTargetRotation and adds the head pose,
// scaled by `multiplier`, for the one caller that is the torch's own spring
// arm. Returns false, having said why, when the active build profile carries no
// RVAs for it - the beam then stays on the game's aim and nothing else changes.
bool Install(float multiplier);

// Publish the pose the camera hook applied this frame, from that hook. The
// beam is composed from the same numbers the view is, so the two cannot
// disagree about where the head is. `active` false puts the beam straight back
// on the game's own aim: menus, cutscenes, tracking toggled off.
void Publish(bool active, float yaw, float pitch, float roll, bool worldSpaceYaw);

}  // namespace swtd_ht::TorchAim
