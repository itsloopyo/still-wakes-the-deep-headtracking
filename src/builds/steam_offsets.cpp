// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Steam Win64 build of Still Wakes the Deep (StillWakesTheDeep.exe, UE 5.4),
// the Habitat/Binaries/Win64 shipping exe. RVAs derived in Ghidra via
// scripts/ghidra/*.java against the matching binary (.lab/ghidra project).
//
// To add support for a new Steam build: do NOT edit kSteamProfile_<date> in
// place. Append a new `extern const BuildProfile kSteamProfile_YYYYMMDD = {...}`
// below, register it at the top of kKnownProfiles in build_registry.cpp, and
// keep older profiles forever (the PE fingerprint routes each user to theirs).
//
// Discovery summary (the scripts named below reproduce every number here from
// the binary; their output is session scratch and is not committed):
//   - GetPlayerViewPoint @ RVA 0x03b12e30, identified by the checkf strings
//     naming its own out-parameters, and by a body that reads the cached POV
//     at controller+0x3c0 and the camera manager at +0x348 before writing
//     both out-parameters.
//   - GPV is virtually dispatched (vtable slot 256, disp 0x800), so it has no
//     static callers. kKnownCallerRvas is the set of return-address RVAs seen
//     live (inject-mode 0 caller summary). Which one is the renderer was then
//     settled in-game by injecting a fixed 25 deg yaw one caller at a time and
//     measuring the rendered frame: only caller 1 moves the view, callers 2-8
//     leave it at the noise floor. That is what makes the caller gate the
//     look/aim decoupling - the gameplay callers below never see the head pose.

namespace swtd_ht::builds
{
    extern const BuildProfile kSteamProfile_20240618;

    // ---- Steam Win64 build (PE TimeDateStamp 0x5BF79C37) ----
    const BuildProfile kSteamProfile_20240618 = {
        /* Name        */ "steam-win64-20240618",
        /* Fingerprint */ { 0x5BF79C37u, 0x0A5E3000u, 0x0A18023Fu },
        /* Offsets     */ {
            /* kGetPlayerViewPointRva */ 0x03b12e30ULL,
            // Observed in-game (inject-mode 0 caller summary, gameplay on the
            // Rig chapter), ordered by calls per frame. The earlier static
            // Ghidra candidate list matched none of these: GPV is dispatched
            // through the vtable, so the static "calls +0x7f8 then +0x800"
            // shape found decoys rather than the live callers.
            /* kKnownCallerRvas */ {{
                0x038d91ecULL,  // 1: fn 0x038d8e60 - RENDER PATH (the only
                                //    caller whose injection moves the frame)
                0x03b1047fULL,  // 2: fn 0x03b102a0
                0x038b38b0ULL,  // 3: fn 0x038b35d0 - LevelTick.cpp
                0x03a7d2a2ULL,  // 4: fn 0x03a7d260
                0x04fa2983ULL,  // 5: fn 0x04fa28b0 - "TraceInteractable"
                0x043e74e6ULL,  // 6: fn 0x043e73e0
                0x0377a33cULL,  // 7: fn 0x03778630 - CanvasObject/emulatestereo
                0x03655908ULL,  // 8: fn 0x03655830
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            /* kDefaultInjectMode     */ 1,
            // Read out of the generated reflection setter stub rather than
            // guessed: the FBoolPropertyParams for "bShowMouseCursor" points at
            // a one-instruction lambda that sets bit 0x1 of the dword at
            // object+0x544, so the stub carries both the offset and the mask as
            // immediates. The neighbouring stub for bEnableClickEvents sets bit
            // 0x2 of the same dword, so the two flags pack together in
            // APlayerController's declaration order - the cross-check that the
            // offset is the bitfield and not a coincidence. See
            // scripts/ghidra/AuditDecoupling.java.
            /* kShowMouseCursorOffset */ 0x544,
            /* kShowMouseCursorMask   */ 0x1u,
            // AHabitatPlayerController::GetCutsceneMode is a UFUNCTION, and its
            // generated exec thunk (RVA 0x04e96e20) inlines the whole body: it
            // widens one byte at object+0x8cd and returns it through the
            // out-parameter, nothing else. The object is whatever the function
            // was called on, so the flag is a plain byte on the controller.
            // That the controller is the one this
            // hook holds is settled by the native-registration array the thunk
            // sits in (RVA 0x084f23d0): its other eight entries are
            // GetHabitatInputComponent, GetIsUsingGamepad, ReportButtonPressed,
            // SetCutsceneMode, SetCutsceneSkippable, ShowLocationName,
            // ShowNotification, GetInputConfig - a player controller's function
            // set and nothing else's. Its class getter registers the class as
            // "HabitatPlayerController" in /Script/Habitat with size 0x910, so
            // the offset is inside the object, and the reflected
            // CutsceneModeContextStack lands at 0x8d0, immediately after this
            // flag. See scripts/ghidra/FindCutsceneState.java and
            // IdentifyCutsceneOwner.java.
            /* kCutsceneModeOffset    */ 0x8cd,
            /* MinimalViewInfoLayout */ {
                /* kFovOffset      */ 0x30,
                /* kRotationStride */ 0x18,
            },
            // Recovered from the UE4SS log this game folder still carries. It
            // printed GUObjectArray / FNamePool users as runtime VAs under an
            // unknown ASLR base; the base falls out by brute force over the
            // 64 KB load grid, demanding that all five logged FUNCTION VAs land
            // exactly on function entry points (base 0x7ff793930000, the only
            // candidate of 484 scoring 5/5 - scripts/ghidra/RecoverUe4ssBase.java).
            // Cross-check: the implied FName::ToString RVA 0x011ef9b0 is the
            // very function GetPlayerViewPoint calls to stringify its ViewTarget
            // name in the checkf path, which is independent of the log.
            //
            // kObjObjects is GUObjectArray (0x0997b0d0) + 0x10, the ObjObjects
            // member within FUObjectArray. kFNamePool was read out of
            // FName::ToString directly, which indexes
            // pool + (id >> 16) * 8 + 0x10 then (id & 0xffff) * 2 - the stock
            // UE5 FNamePool block layout core already expects. The struct
            // offsets below are engine-version-bound rather than build-bound.
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0997b0e0ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x098c4380ULL,
                /* kFNamePoolBlocks  */ 0x10,
                /* kClassPrivate     */ 0x10,
                /* kNamePrivate      */ 0x18,
                /* kOuterPrivate     */ 0x20,
            },
            /* kProcessEventRva */ 0x013c2100ULL,
            // The torch beam. AHabitatCharacterTorch's constructor (RVA
            // 0x04f43d00) creates a HabitatTorchSpringArmComponent on the
            // torch root and attaches PrimaryLightComponent to that ARM, with
            // SecondaryLightComponent under it - so the arm's aim is the
            // beam's aim.
            //
            // 0x03725180 is USpringArmComponent::GetTargetRotation, identified
            // by its body rather than a symbol: it reads the component
            // rotation, replaces it with the owning pawn's view rotation when
            // bUsePawnControlRotation is set, then puts back whichever of
            // pitch/yaw/roll the arm does not inherit. That is stock UE 5.4
            // line for line, and it is non-virtual (no vtable holds it), so
            // one hook covers every arm.
            //
            // 0x04f6a008 is the return address of the single call to it inside
            // UHabitatTorchSpringArmComponent::UpdateDesiredArmLocation (RVA
            // 0x04f69f70). That function is the class's override of the engine
            // one - byte-identical in size to it, in Habitat's address range,
            // and slot 191 of the vtable at 0x08565bb0 that the class
            // constructor (0x04f46ee0) stores. The chain to the class name is
            // closed: the getter at 0x04eca100 registers
            // "HabitatTorchSpringArmComponent" in /Script/Habitat, size 0x3c0,
            // and reaches that constructor through its InternalConstructor at
            // 0x04eca440. See scripts/ghidra/FindTorchRig.java,
            // FindTorchTargetRotation.java and VerifyTorchSpringArm.java.
            /* kGetTargetRotationRva          */ 0x03725180ULL,
            /* kTorchArmTargetRotationRetRva  */ 0x04f6a008ULL,
            // USceneComponent::AttachParent, read off its FProperty params
            // struct. The neighbouring entries in the same table are
            // AttachSocketName 0xb8, AttachChildren 0xc8, RelativeLocation
            // 0x128, RelativeRotation 0x140, RelativeScale3D 0x158 - and
            // GetTargetRotation reads RelativeRotation at exactly 0x140, which
            // is what says this block is USceneComponent's and not some other
            // class that happens to declare a field of the same name. See
            // scripts/ghidra/DumpSceneComponentOffsets.java.
            /* kSceneComponentAttachParentOffset */ 0xb0,
        },
    };
}
