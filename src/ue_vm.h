// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>

// Calling into the game's script VM, and reading the object graph around it.
//
// Two features need this - the reticle/prompt movers and the torch flare
// re-parent - and both do it the same way: resolve UObject::ProcessEvent from
// the active build profile once, then dispatch a UFUNCTION through it behind a
// fault guard. Every caller must already be on the game thread.
namespace swtd_ht::ue_vm {

// Resolve UObject::ProcessEvent from the active build profile, once. False when
// the profile carries no RVA for it, in which case nothing else here can run.
bool Ready();

// The RVA Ready() resolved, for the caller that wants to say so in the log.
std::uintptr_t ProcessEventRva();

// Dispatch a UFUNCTION. Returns false if the call faulted - the object stopped
// being what the caller thought it was between its liveness test and here - so
// the caller can drop it and go looking for its replacement rather than pushing
// into a dead object forever.
bool Dispatch(void* self, void* function, void* params);

// The chain of outer names above `obj`, at most `depth` links, each one
// prefixed with `separator`. Stops early at the first outer that will not read.
std::string OuterChain(std::uintptr_t obj, int depth, const char* separator);

}  // namespace swtd_ht::ue_vm
