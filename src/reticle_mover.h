// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace swtd_ht::ReticleMover {

// Push the projected clean-aim offset onto the game's own crosshair and
// interaction-prompt widgets, so they sit where the interaction ray points
// instead of at the centre of the head-tracked picture. Called from the
// GetPlayerViewPoint hook on the render caller, which is a game-thread context -
// the UObject table and the script VM both require that.
void Tick();

}  // namespace swtd_ht::ReticleMover
