// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

namespace swtd_ht::builds
{
    extern const BuildProfile kSteamProfile_20240618;
    const BuildProfile kSteamProfile_20240618 = {
        /* Name        */ "steam-win64-20240618",
        /* Fingerprint */ { 0x5BF79C37u, 0x0A5E3000u, 0x0A18023Fu },
        /* Offsets     */ {
            /* kGetPlayerViewPointRva */ 0x03b12e30ULL,
            /* kKnownCallerRvas */ {{
                0x038d91ecULL,  // Render caller in ULocalPlayer::GetViewPoint.
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
            /* kShowMouseCursorOffset */ 0x544,
            /* kShowMouseCursorMask   */ 0x1u,
            /* kCutsceneModeOffset    */ 0x8cd,
            /* MinimalViewInfoLayout */ {
                /* kFovOffset      */ 0x30,
                /* kRotationStride */ 0x18,
            },
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
            /* kGetTargetRotationRva          */ 0x03725180ULL,
            /* kTorchArmTargetRotationRetRva  */ 0x04f6a008ULL,
            /* kSceneComponentAttachParentOffset */ 0xb0,
        },
    };
}
