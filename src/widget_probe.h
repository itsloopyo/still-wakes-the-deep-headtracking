// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace swtd_ht::WidgetProbe {

// Validate the UObject/FName globals in the active build profile by reading the
// object array's own header and resolving a handful of names. Logs what it
// actually read either way, so a wrong kObjObjects shows up as a bad element
// count rather than as silence. Returns false if the globals do not look sane,
// in which case nothing else here should run.
bool ValidateGlobals();

// Log the live UMG objects whose name or class looks like a reticle, crosshair
// or interaction prompt, with their outer chain. This is how the widgets to
// move get identified - their names live in cooked Blueprint assets, so they
// cannot be read out of the EXE. Called on a timer while [Dev] WidgetDump is
// set, so that one of the passes lands while a prompt is on screen.
void DumpCandidates();

}  // namespace swtd_ht::WidgetProbe
