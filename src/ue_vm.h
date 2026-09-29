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

// GUObjectArray's chunk table and element count, read fresh.
struct ObjectTable {
    std::uintptr_t Chunks = 0;
    std::uint32_t  Num = 0;
};

// False when the profile carries no object-table layout or the header does not
// read as one.
bool ReadObjectTable(ObjectTable& out);

// The object in slot `index`, or 0 for an empty or unreadable slot.
std::uintptr_t ObjectAt(const ObjectTable& table, std::uint32_t index);

// Whether `obj` is still registered in GUObjectArray. Freed UObject memory
// keeps its old class pointer for as long as the allocator leaves it alone, so
// a class-pointer test on its own reports a destroyed object as live
// indefinitely. The array slot at the object's own InternalIndex points back
// at it only while it is registered: destruction nulls that slot, and a reused
// index points at whatever took it.
bool IsRegistered(std::uintptr_t obj);

// How many table slots one Step() visits. A full pass over the ~190k objects a
// level holds measured 3-8ms comparing FName ids and 21-35ms resolving names,
// all of it on the game thread inside the camera hook, so a pass run in one go
// lands on one frame as a hitch. At this size a slice costs a few tenths of a
// millisecond (a couple of ms while names are still being resolved) and a pass
// completes in about twelve frames.
inline constexpr std::uint32_t kWalkSlice = 16384;

double NowMs();

// One pass over the object table, spread across calls.
class SlicedObjectWalk {
public:
    // Visits up to `budget` slots, handing each object to visit(obj), and
    // returns true on the call that completes the pass. The call after that
    // starts a new pass from slot 0. The table is re-read on every call, so
    // objects created or destroyed mid-pass are simply seen or not: anything a
    // pass collected must be checked with IsRegistered() before it is used.
    template <typename Visit>
    bool Step(std::uint32_t budget, Visit&& visit) {
        const double start = NowMs();
        ObjectTable table;
        bool done = true;
        if (ReadObjectTable(table) && next_ < table.Num) {
            const std::uint32_t end =
                table.Num - next_ > budget ? next_ + budget : table.Num;
            for (std::uint32_t i = next_; i < end; ++i) {
                if (const std::uintptr_t obj = ObjectAt(table, i)) visit(obj);
            }
            next_ = end;
            done = end == table.Num;
        }
        passMs_ += NowMs() - start;
        ++passSlices_;
        if (done) {
            lastPassMs_ = passMs_;
            lastPassSlices_ = passSlices_;
            passMs_ = 0.0;
            passSlices_ = 0;
            next_ = 0;
        }
        return done;
    }

    bool InProgress() const { return next_ != 0; }

    // Time spent inside Step() over the last completed pass, and the number of
    // calls it was spread across.
    float LastPassMs() const { return static_cast<float>(lastPassMs_); }
    int LastPassSlices() const { return lastPassSlices_; }

private:
    std::uint32_t next_ = 0;
    double passMs_ = 0.0;
    int passSlices_ = 0;
    double lastPassMs_ = 0.0;
    int lastPassSlices_ = 0;
};

}  // namespace swtd_ht::ue_vm
