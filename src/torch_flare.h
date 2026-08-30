// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace swtd_ht::TorchFlare {

// Put the torch's flare card on the same spring arm the beam lights hang off,
// so the glare travels with the beam instead of being left behind in the world
// when the view turns. Called from the GetPlayerViewPoint hook, which is a
// game-thread context - walking the object table and dispatching into the
// script VM both require that.
void Tick();

}  // namespace swtd_ht::TorchFlare
