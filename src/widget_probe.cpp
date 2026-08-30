// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Find the UMG widgets that need unsticking.
//
// The reticle and the interaction prompt are Blueprint assets in the cooked
// pak, not native classes - the exec thunks behind SetReticleSize are generic
// Blueprint VM stubs, and the widget variable names exist only in the .uasset.
// So the names cannot be recovered from the EXE and have to be read off the
// live object table with the game running.

#include "widget_probe.h"

#include <cstdint>
#include <string>
#include <unordered_set>

#include "builds/build_registry.h"
#include "logging.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace swtd_ht::WidgetProbe {

namespace {

namespace ue = ::cameraunlock::unreal;

// Substrings worth looking at. Deliberately wide: this runs a handful of times
// in a dev session, and a name we did not anticipate is exactly what it exists
// to catch.
const char* kNeedles[] = {
    "reticle", "crosshair", "cursor", "prompt", "tooltip",
    "hint", "interact", "hud", "marker",
};

// How far up the outer chain a candidate is reported. Three links is enough to
// tell a live widget from the Blueprint template of the same name.
constexpr int kOuterChainDepth = 3;

// How many objects the class-name sample checks before deciding the FNamePool
// resolves, and the bounds a live UE5 object table has to fall inside: well
// over ten thousand objects and nothing like five million. Outside that,
// kObjObjects points at the wrong field and every later read would be wild.
constexpr int          kNameSampleSize = 40;
constexpr std::uint32_t kMinPlausibleObjects = 1000;
constexpr std::uint32_t kMaxPlausibleObjects = 5000000;

}  // namespace

bool ValidateGlobals() {
    const auto& g = Offsets().UObjectGlobals;
    if (g.kObjObjects == 0 || g.kFNamePool == 0) {
        Log::Line("widget-probe: profile carries no UObject globals - reticle move disabled");
        return false;
    }

    const std::uintptr_t arr = ue::ModuleBase() + g.kObjObjects;
    std::uintptr_t chunks = 0;
    std::uint32_t  num = 0;
    const bool okPtr = ue::SafeReadPtr(arr, chunks);
    const bool okNum = ue::SafeReadU32(arr + g.kObjObjects_Num, num);
    Log::Line("widget-probe: ObjObjects@RVA 0x%08llx chunks=0x%llx numElements=%u",
        static_cast<unsigned long long>(g.kObjObjects),
        static_cast<unsigned long long>(chunks), num);

    if (!okPtr || !okNum || !ue::LooksLikePointer(chunks)
        || num < kMinPlausibleObjects || num > kMaxPlausibleObjects) {
        Log::Line("widget-probe: object array header does not look sane - reticle move "
                  "disabled. Re-derive kObjObjects (expected GUObjectArray + 0x10).");
        return false;
    }

    // Names have to resolve too, or the FNamePool half is wrong even though the
    // array half looked fine.
    int sampled = 0, named = 0;
    ue::ForEachUObject([&](std::uintptr_t obj) -> bool {
        const std::string cn = ue::ClassName(obj);
        if (!cn.empty()) ++named;
        if (++sampled >= kNameSampleSize) return true;
        return false;
    });
    Log::Line("widget-probe: resolved %d/%d sampled class names", named, sampled);
    if (named < sampled / 2) {
        Log::Line("widget-probe: FNamePool is not resolving - reticle move disabled. "
                  "Re-derive kFNamePool from FName::ToString.");
        return false;
    }
    Log::Line("widget-probe: UObject globals validated");
    return true;
}

void DumpCandidates() {
    static std::unordered_set<std::uintptr_t> s_seen;
    int hits = 0, fresh = 0;
    ue::ForEachUObject([&](std::uintptr_t obj) -> bool {
        const std::string on = ue::ObjectName(obj);
        if (on.empty()) return false;
        const std::string cn = ue::ClassName(obj);

        bool match = false;
        for (const char* n : kNeedles) {
            if (ue::ContainsCI(on, n) || ue::ContainsCI(cn, n)) { match = true; break; }
        }
        if (!match) return false;
        ++hits;
        // Archetypes and class-default objects are not on screen; they are
        // noise here, but their names are the same as the instances', so they
        // are counted and not printed rather than dropped silently.
        if (ue::ContainsCI(on, "Default__")) return false;
        if (!s_seen.insert(obj).second) return false;
        ++fresh;
        Log::Line("  widget 0x%llx  class=%-40s name=%s%s",
            static_cast<unsigned long long>(obj), cn.c_str(), on.c_str(),
            ue_vm::OuterChain(obj, kOuterChainDepth, " < ").c_str());
        return false;
    });
    Log::Line("widget-probe: %d matching objects, %d newly listed", hits, fresh);
}

}  // namespace swtd_ht::WidgetProbe
