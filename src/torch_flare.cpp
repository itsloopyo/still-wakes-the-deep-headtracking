// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Keep the torch's glare with the beam.
//
// AHabitatCharacterTorch builds two things off its root: the spring arm, which
// carries PrimaryLightComponent and SecondaryLightComponent, and
// LightFlareStaticMeshComponent - the card that draws the glare. The card is a
// SIBLING of the arm, not a child of it. In the unmodified game that costs
// nothing, because the arm aims itself at the view and the root is carried in
// front of the view too, so beam and glare agree. Aim the arm with the head and
// they stop agreeing: the beam goes where the player looks and the glare stays
// on the root, which reads as a glare pinned to the world.
//
// The fix is structural rather than arithmetic. Re-parent the card onto the
// arm, keeping its world transform at the moment of the attach, and the engine
// carries it with the socket from then on - the same socket the beam lights
// already ride, updated at the same point in the frame, with no per-frame
// writes and no transform to put back. A spring arm returns its endpoint
// transform from GetSocketTransform whatever socket name a child asks for, so
// the card needs no socket of its own; that is also why the beam lights, which
// the constructor attaches with NAME_None, follow the arm at all.
//
// What this changes, and it is visible: the card picks up the arm's rotation
// lag and the game's own torch wander, which on the root it did not have. The
// glare now sways with the beam. That is the trade for having the two agree.
//
// The walk repeats rather than running once. Loading a level destroys the torch
// and builds a new one, whose card is a different object with the game's own
// attachment again, so the attach has to be re-asserted; checking
// AttachParent - rather than remembering that we called the attach - is what
// makes a call that dispatched but did nothing visible in the log.

#include "torch_flare.h"

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "builds/build_registry.h"
#include "logging.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace swtd_ht::TorchFlare {

namespace {

namespace ue = ::cameraunlock::unreal;

// The subobject names AHabitatCharacterTorch's constructor gives the two
// components, which are the FNames the live objects carry.
constexpr const char* kCardName = "LightFlareStaticMeshComponent";
constexpr const char* kArmName  = "HabitatTorchSpringArmComponent";

// EAttachmentRule. KeepWorld leaves the card exactly where it is at the moment
// it changes parent, so nothing jumps; SnapToTarget would drop it onto the
// spring arm's endpoint and KeepRelative would reinterpret its offset from the
// root as an offset from the socket.
constexpr std::uint8_t kKeepWorld = 1;

// Short interval while the card is not on the arm, long one as a backstop once
// it is. The backstop is what re-attaches after a level change; the walk is the
// same shape and cost as the reticle's, a handful of milliseconds.
constexpr std::uint64_t kRetryMs    = 2000;
constexpr std::uint64_t kBackstopMs = 15000;

std::uintptr_t g_attachFn = 0;

// Only so the log says something when the pair changes, rather than repeating
// itself every backstop.
std::uintptr_t g_lastCard = 0;
std::uintptr_t g_lastArm  = 0;

std::uintptr_t OuterOf(std::uintptr_t obj) {
    std::uintptr_t outer = 0;
    if (!ue::SafeReadPtr(obj + Offsets().UObjectGlobals.kOuterPrivate, outer)) return 0;
    return outer;
}

std::uintptr_t AttachParentOf(std::uintptr_t comp) {
    const std::size_t off = Offsets().kSceneComponentAttachParentOffset;
    std::uintptr_t parent = 0;
    if (off == 0 || !ue::SafeReadPtr(comp + off, parent)) return 0;
    return parent;
}

struct Component { std::uintptr_t Obj = 0; std::uintptr_t Torch = 0; };

// The card and the arm belonging to one torch. Class-default objects carry the
// same subobject names as the live actor, so they are dropped by their outer's
// name - an archetype has nothing to render and attaching to it would be a
// write into the template every later torch is built from.
struct Pair { std::uintptr_t Card = 0; std::uintptr_t Arm = 0; std::uintptr_t Torch = 0; };

Pair Collect(int& pairCount) {
    std::vector<Component> cards, arms;
    const std::size_t nameOff = Offsets().UObjectGlobals.kNamePrivate;

    // The two FName comparison ids, learned by the walks that resolve them, so
    // later walks compare integers instead of building a string for every one of
    // ~180k objects. BOTH have to be known before the integer path can be taken:
    // with only one learned it would reject every object that is not that one
    // name, and the other name could then never be resolved at all.
    static std::uint32_t s_cardId = 0, s_armId = 0;

    ue::ForEachUObject([&](std::uintptr_t obj) -> bool {
        std::uint32_t id = 0;
        if (!ue::SafeReadU32(obj + nameOff, id)) return false;

        bool isCard = false, isArm = false;
        if (s_cardId != 0 && s_armId != 0) {
            isCard = (id == s_cardId);
            isArm  = (id == s_armId);
            if (!isCard && !isArm) return false;
        } else {
            const std::string name = ue::ResolveFName(id);
            if (name == kCardName) { isCard = true; s_cardId = id; }
            else if (name == kArmName) { isArm = true; s_armId = id; }
            else return false;
        }

        const std::uintptr_t outer = OuterOf(obj);
        if (!outer) return false;
        if (ue::ContainsCI(ue::ObjectName(outer), "Default__")) return false;

        (isCard ? cards : arms).push_back({obj, outer});
        return false;
    });

    Pair first;
    pairCount = 0;
    for (const Component& c : cards) {
        for (const Component& a : arms) {
            if (c.Torch != a.Torch) continue;
            if (pairCount++ == 0) first = {c.Obj, a.Obj, c.Torch};
            break;
        }
    }
    return first;
}

// USceneComponent::K2_AttachToComponent, the one call that moves the card. The
// lookup is not retried on the short interval: hunting a name that has already
// failed against a fully built object table every two seconds only fills the log.
bool ResolveAttachFn() {
    if (g_attachFn) return true;
    g_attachFn = ue::FindLiveObject("Function", "K2_AttachToComponent", "SceneComponent");
    if (!g_attachFn) {
        static bool s_warned = false;
        if (!s_warned) {
            s_warned = true;
            Log::Line("torch-flare: SceneComponent.K2_AttachToComponent not found in the "
                      "object table - the glare stays on the torch root");
        }
        return false;
    }
    Log::Line("torch-flare: K2_AttachToComponent at 0x%llx",
        static_cast<unsigned long long>(g_attachFn));
    return true;
}

// Put the card on the arm. Returns whether it ended up there.
bool Attach(const Pair& p) {
    // USceneComponent::K2_AttachToComponent(Parent, SocketName, LocationRule,
    // RotationRule, ScaleRule, bWeldSimulatedBodies) -> bool. SocketName is
    // NAME_None; a spring arm hands every child its endpoint transform whatever
    // name is asked for.
    struct AttachParams {
        void*         Parent;
        std::uint32_t SocketComparisonIndex;
        std::uint32_t SocketNumber;
        std::uint8_t  LocationRule;
        std::uint8_t  RotationRule;
        std::uint8_t  ScaleRule;
        std::uint8_t  bWeldSimulatedBodies;
        std::uint8_t  ReturnValue;
        char          pad[15];
    } params{};
    params.Parent       = reinterpret_cast<void*>(p.Arm);
    params.LocationRule = kKeepWorld;
    params.RotationRule = kKeepWorld;
    params.ScaleRule    = kKeepWorld;

    const bool dispatched = ue_vm::Dispatch(reinterpret_cast<void*>(p.Card),
                                            reinterpret_cast<void*>(g_attachFn), &params);
    // Read the field back rather than trusting either the dispatch or the
    // function's own return: the parent is the thing that actually decides
    // where the card is drawn.
    const bool attached = AttachParentOf(p.Card) == p.Arm;
    Log::Line("torch-flare: attach card->arm dispatched=%s returned=%u AttachParent=%s",
        dispatched ? "yes" : "FAULTED", params.ReturnValue,
        attached ? "arm (glare now follows the beam)" : "UNCHANGED - glare stays on the root");
    return attached;
}

}  // namespace

void Tick() {
    if (Offsets().UObjectGlobals.kObjObjects == 0) return;
    if (Offsets().kSceneComponentAttachParentOffset == 0) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            Log::Line("torch-flare: this build profile carries no AttachParent offset - "
                      "the glare stays on the torch root");
        }
        return;
    }

    if (!ue_vm::Ready()) return;

    // Once the card is on the arm nothing needs doing, so the steady state is
    // one table walk every kBackstopMs.
    static std::uint64_t s_next = 0;
    const std::uint64_t now = GetTickCount64();
    if (now < s_next) return;

    int pairCount = 0;
    const Pair p = Collect(pairCount);

    if (!p.Card || !p.Arm) {
        s_next = now + kRetryMs;
        if (g_lastCard != 0) {
            Log::Line("torch-flare: torch gone (card and arm not both present) - waiting");
            g_lastCard = g_lastArm = 0;
        }
        return;
    }

    if (!ResolveAttachFn()) {
        s_next = now + kBackstopMs;
        return;
    }

    if (p.Card != g_lastCard || p.Arm != g_lastArm) {
        Log::Line("torch-flare: card 0x%llx arm 0x%llx torch %s  pairs=%d",
            static_cast<unsigned long long>(p.Card),
            static_cast<unsigned long long>(p.Arm),
            ue::ObjectName(p.Torch).c_str(), pairCount);
        g_lastCard = p.Card;
        g_lastArm  = p.Arm;
    }

    if (AttachParentOf(p.Card) == p.Arm) {
        s_next = now + kBackstopMs;
        return;
    }

    s_next = now + (Attach(p) ? kBackstopMs : kRetryMs);
}

}  // namespace swtd_ht::TorchFlare
