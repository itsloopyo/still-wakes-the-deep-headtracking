// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include <cameraunlock/memory/pe_fingerprint.h>
#include <cameraunlock/unreal/ue_runtime.h>

// One BuildProfile describes a single shipped build of Still Wakes the Deep:
// the PE-header fingerprint that uniquely identifies it, plus every per-build
// RVA / field offset the camera hook needs. The registry holds one profile per
// supported build; at startup the mod fingerprints the live module and selects
// the matching profile. No match leaves the mod fully dormant (no hooks
// installed, game runs vanilla) - see AGENTS.md "Maintain compatibility across
// new patches": never edit an existing profile's RVAs in place, ADD a new one.
//
// SWtD is UE 5.4. The lean offset set reflects that this is a narrative
// walking-sim: no weapons, no crosshair, no helmet overlay, so the mod only
// needs to inject the head pose into the render-path view and leave every other
// GetPlayerViewPoint caller clean (the aim/interaction-trace decoupling).

namespace swtd_ht
{
    // PE-header build fingerprint (TimeDateStamp + SizeOfImage + CheckSum);
    // the shared type keeps reading/matching/classification in core.
    using PeFingerprint = ::cameraunlock::memory::PeFingerprint;

    struct OffsetTable
    {
        // Hook target: APlayerController::GetPlayerViewPoint. RVA from the
        // module base. Zero = profile incomplete (mod stays dormant).
        std::uintptr_t kGetPlayerViewPointRva;

        // Return-address RVAs of the distinct GetPlayerViewPoint call sites.
        // Head tracking is injected ONLY for callers flagged here per the
        // active inject mode; every other caller reads the clean (mouse/pad)
        // rotation. That per-caller gate IS the look/aim decoupling.
        // 0-valued trailing entries are unused padding.
        std::array<std::uintptr_t, 16> kKnownCallerRvas;

        // Default inject mode at startup. 0 = all callers (diagnostic only),
        // 1..16 = inject only for kKnownCallerRvas[mode-1] (the render-path
        // caller / FMinimalViewInfo builder), 17 = none. Page Down / Page Up
        // cycle this live so the render caller can be re-confirmed in game
        // after a patch without a rebuild.
        int kDefaultInjectMode;

        // APlayerController::bShowMouseCursor, as a byte offset into the
        // controller plus the bit within the dword there. UE raises that flag
        // exactly when input belongs to a menu rather than the player, so it is
        // the gameplay gate: cursor up -> suppress tracking. The hook already
        // holds the controller pointer, so reading it costs one load.
        std::size_t   kShowMouseCursorOffset;
        std::uint32_t kShowMouseCursorMask;

        // The game's own cutscene flag, as a byte offset into the same
        // controller. Habitat carries a full cutscene layer of its own
        // (SetCutsceneMode pushes onto CutsceneModeContextStack and caches the
        // result in this bool), and it is the only state that distinguishes a
        // cutscene from ordinary gameplay - bShowMouseCursor stays clear
        // throughout one. Head tracking runs through a cutscene; the only thing
        // this gates is the FOV offset, which stands down so a cutscene is
        // framed at the focal length it was composed for. Zero = not derived
        // for this build, and the offset then stays applied throughout.
        std::size_t   kCutsceneModeOffset;

        // FMinimalViewInfo field offsets. The render caller is
        // ULocalPlayer::GetViewPoint, which hands GPV pointers to the Location
        // and Rotation fields of the FMinimalViewInfo it is filling in, so the
        // live FOV the frame will render with sits at outLocation + kFovOffset.
        // kRotationStride is the Location->Rotation gap, checked against the
        // actual out-param pair before the FOV is read, and before the
        // [Camera] FovOffset write goes anywhere near it - if the two pointers
        // are not that far apart they are not fields of one FMinimalViewInfo,
        // and what looks like the FOV is a local in some other caller's stack
        // frame.
        struct {
            std::size_t kFovOffset;
            std::size_t kRotationStride;
        } MinimalViewInfoLayout;

        // GUObjectArray / FNamePool, for finding the UMG reticle and prompt
        // widgets by name. The shared core type is what ue::SetRuntime consumes.
        ::cameraunlock::unreal::UObjectGlobalsLayout UObjectGlobals;

        // UObject::ProcessEvent, for calling SetRenderTranslation on those
        // widgets. Pinned by RVA rather than read from a vtable slot because
        // AActor overrides the slot with an RPC-aware variant, and the base
        // UObject one is the one that must be called here.
        std::uintptr_t kProcessEventRva;

        // USpringArmComponent::GetTargetRotation, which returns the rotation a
        // spring arm aims itself at before its own lag is applied. The torch's
        // beam lights hang off UHabitatTorchSpringArmComponent, and that arm
        // has bUsePawnControlRotation set, so this function hands it the pawn's
        // view rotation - the mouse/pad aim. Adding the head pose here is what
        // puts the beam where the player is looking.
        std::uintptr_t kGetTargetRotationRva;

        // Return-address RVA of the one call to it inside
        // UHabitatTorchSpringArmComponent::UpdateDesiredArmLocation. The head
        // pose is added only for this caller, so every other spring arm in the
        // game, and every Blueprint that asks an arm for its target rotation,
        // still reads the clean value. Same per-caller gate the camera hook
        // uses. Zero in either field leaves torch tracking off for the build.
        std::uintptr_t kTorchArmTargetRotationRetRva;

        // USceneComponent::AttachParent. The torch's flare card is a sibling of
        // the spring arm rather than a child of it, so aiming the arm with the
        // head leaves the glare behind; the card is re-parented onto the arm,
        // and this is what says whether that took. Engine-version-bound rather
        // than build-bound, like the UObject struct offsets above. Zero leaves
        // the glare where the game put it.
        std::size_t kSceneComponentAttachParentOffset;
    };

    struct BuildProfile
    {
        const char*   Name;
        PeFingerprint Fingerprint;
        OffsetTable   Offsets;
    };
}
