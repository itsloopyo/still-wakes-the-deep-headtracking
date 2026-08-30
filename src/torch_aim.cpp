// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Aim the torch with the head instead of the mouse.
//
// The rig, read out of AHabitatCharacterTorch's constructor (RVA 0x04f43d00):
// the actor builds a HabitatTorchSpringArmComponent on its root, hangs
// PrimaryLightComponent off the ARM, and SecondaryLightComponent off that
// again. So whatever aims the arm aims the beam, and nothing else has to be
// touched - no light component to find in the object table, no transform to
// write and put back.
//
// A spring arm asks USpringArmComponent::GetTargetRotation (RVA 0x03725180)
// where to point, once per tick, before its own rotation lag and the game's
// wander are applied. That function is where the beam picks up the aim: it
// hands back the arm's world rotation, replaced by the owning pawn's view
// rotation when bUsePawnControlRotation is set, then overwrites whichever of
// pitch/yaw/roll the arm is not inheriting. Adding the head pose to whatever
// comes back is correct whichever of those branches produced it, because all of
// them return the same thing - the arm's aim in world space.
//
// The head pose goes in for exactly one caller: the call at RVA 0x04f6a003 -
// so the return address the gate compares is 0x04f6a008 - inside
// UHabitatTorchSpringArmComponent::UpdateDesiredArmLocation (RVA 0x04f69f70,
// slot 191 of the class vtable at 0x08565bb0). Every other spring
// arm in the game, and every Blueprint that asks an arm for its target
// rotation, reads the clean value - the same per-caller gate the camera hook
// uses on GetPlayerViewPoint.
//
// The multiplier scales the head pose the beam is given. It defaults to 1.5,
// matching resident-evil-requiem and prey, so the beam leads the view: turn your
// head and your eyes end up off the centre of the screen, so a beam aligned with
// the view lands short of what you are actually looking at. 1.0 moves the beam
// with the view instead.

#include "torch_aim.h"

#include <atomic>
#include <cstdint>

#include <windows.h>
#include <intrin.h>

#include "builds/build_registry.h"
#include "camera_boundary.h"
#include "logging.h"

#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/unreal/ue_math.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace swtd_ht::TorchAim {

namespace {

namespace ue = ::cameraunlock::unreal;

using ue::FRotator;

// FRotator is FRotator3d under UE5 LWC - 24 bytes - so it comes back through a
// caller-supplied buffer whose address is also returned in RAX.
using GetTargetRotation_t = FRotator*(__fastcall*)(void* self, FRotator* out);

GetTargetRotation_t g_orig = nullptr;

// Written once, before the hook is enabled.
float g_multiplier = 1.5f;

std::atomic<bool>  g_active{false};
std::atomic<float> g_yaw{0.0f};
std::atomic<float> g_pitch{0.0f};
std::atomic<float> g_roll{0.0f};
std::atomic<bool>  g_worldSpaceYaw{true};

std::atomic<std::uint64_t> g_gatedCalls{0};

FRotator* __fastcall GetTargetRotation_Hook(void* self, FRotator* out) {
    const void* retAddr = _ReturnAddress();
    FRotator* result = g_orig(self, out);

    if (!g_active.load(std::memory_order_relaxed))
        return result;

    const std::uintptr_t retRva =
        reinterpret_cast<std::uintptr_t>(retAddr) - ue::ModuleBase();
    if (retRva != Offsets().kTorchArmTargetRotationRetRva)
        return result;

    const double k = static_cast<double>(g_multiplier);
    const double yaw   = g_yaw.load(std::memory_order_relaxed) * k;
    const double pitch = g_pitch.load(std::memory_order_relaxed) * k;
    const double roll  = g_roll.load(std::memory_order_relaxed) * k;

    // The camera hook's composition, scaled - literally the same function, so
    // the beam and the view cannot end up disagreeing about which way the head
    // turned.
    const FRotator clean = *out;
    camera_boundary::ApplyHeadPose(*out, yaw, pitch, roll,
                                   g_worldSpaceYaw.load(std::memory_order_relaxed));

    // Bounded: enough to show in a session log that the gate is matching the
    // torch's caller and by how much the beam leads, then quiet. The arm ticks
    // at frame rate, so anything unbounded here would be the largest thing in
    // the file by far.
    static std::atomic<int> s_logsLeft{6};
    static std::atomic<std::uint64_t> s_lastLog{0};
    const std::uint64_t n = g_gatedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::uint64_t now = GetTickCount64();
    if (s_logsLeft.load(std::memory_order_relaxed) > 0 &&
        (n == 1 || now - s_lastLog.load(std::memory_order_relaxed) >= 3000)) {
        s_lastLog.store(now, std::memory_order_relaxed);
        s_logsLeft.fetch_sub(1, std::memory_order_relaxed);
        Log::Line("torch #%llu arm=%p clean=(Y=%.2f P=%.2f R=%.2f) head*%.2f=(Y=%.2f P=%.2f R=%.2f) "
                  "beam=(Y=%.2f P=%.2f R=%.2f) yawMode=%s",
            static_cast<unsigned long long>(n), self,
            clean.Yaw, clean.Pitch, clean.Roll, g_multiplier, yaw, pitch, roll,
            out->Yaw, out->Pitch, out->Roll,
            g_worldSpaceYaw.load(std::memory_order_relaxed) ? "world" : "local");
    }

    return result;
}

}  // namespace

void Publish(bool active, float yaw, float pitch, float roll, bool worldSpaceYaw) {
    g_yaw.store(yaw, std::memory_order_relaxed);
    g_pitch.store(pitch, std::memory_order_relaxed);
    g_roll.store(roll, std::memory_order_relaxed);
    g_worldSpaceYaw.store(worldSpaceYaw, std::memory_order_relaxed);
    g_active.store(active, std::memory_order_relaxed);
}

bool Install(float multiplier) {
    const std::uintptr_t rva    = Offsets().kGetTargetRotationRva;
    const std::uintptr_t retRva = Offsets().kTorchArmTargetRotationRetRva;
    if (rva == 0 || retRva == 0) {
        Log::Line("torch: this build profile carries no spring-arm RVAs - the beam stays "
                  "on the game's own aim");
        return false;
    }

    g_multiplier = multiplier;

    auto& hm = cameraunlock::hooks::HookManager::Instance();
    void* target = reinterpret_cast<void*>(ue::ModuleBase() + rva);
    if (auto s = hm.CreateHook(target, reinterpret_cast<void*>(&GetTargetRotation_Hook),
                               reinterpret_cast<void**>(&g_orig));
        s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("torch: CreateHook(GetTargetRotation) failed: %s - the beam stays on the "
                  "game's own aim", cameraunlock::hooks::HookStatusToString(s));
        return false;
    }
    if (auto s = hm.EnableHook(target); s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("torch: EnableHook(GetTargetRotation) failed: %s - the beam stays on the "
                  "game's own aim", cameraunlock::hooks::HookStatusToString(s));
        return false;
    }

    Log::Line("torch: GetTargetRotation hooked at RVA 0x%08llx, caller gate ret RVA 0x%08llx, "
              "multiplier %.2f",
        static_cast<unsigned long long>(rva),
        static_cast<unsigned long long>(retRva), multiplier);
    return true;
}

}  // namespace swtd_ht::TorchAim
