// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Inject the head pose into the render path, and nowhere else.
//
// GetPlayerViewPoint fires from many call sites per frame. Only the one the
// caller gate names - the FMinimalViewInfo builder inside
// ULocalPlayer::GetViewPoint - gets the pose written back. Every other caller
// (interaction line-traces, audio listener, AI perception, replication) reads
// the clean mouse/pad rotation, and that per-caller gate IS the look/aim
// decoupling: no separate save/restore sandwich is needed because the game
// simply never observes the delta.

#include "view_hook.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <tuple>
#include <unordered_map>

#include <windows.h>
#include <intrin.h>

#include "aim_projection.h"
#include "builds/build_registry.h"
#include "camera_boundary.h"
#include "inject_mode.h"
#include "logging.h"
#include "reticle_mover.h"
#include "torch_aim.h"
#include "torch_flare.h"
#include "widget_probe.h"

#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/unreal/ue_math.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace swtd_ht::view_hook {

namespace {

namespace ue = ::cameraunlock::unreal;

using ue::FQuat4d;
using ue::FRotator;
using ue::FVector;
using cameraunlock::time::FrameClock;

static_assert(std::tuple_size<decltype(OffsetTable::kKnownCallerRvas)>::value
                  == inject::kCallerSlots,
              "inject::kCallerSlots must match the profile's caller RVA table");

// APlayerController::GetPlayerViewPoint(self, &OutLocation, &OutRotation).
// FVector/FRotator are FVector3d/FRotator3d (24 bytes) under UE5 LWC.
using GetPlayerViewPoint_t = void(__fastcall*)(void* self, FVector* outLocation, FRotator* outRotation);

Dependencies g_deps{};

std::atomic<bool> g_trackingEnabled{true};
std::atomic<bool> g_worldSpaceYaw{true};
std::atomic<int>  g_injectMode{inject::kFirstCaller};

GetPlayerViewPoint_t g_origGetPlayerViewPoint = nullptr;
std::atomic<std::uint64_t> g_hookCallCount{0};

// Ticked only by the injected render-path caller, so the session sees one dt
// per rendered frame.
FrameClock g_frameClock;

// ---- diagnostics cadence -------------------------------------------------
constexpr std::uint64_t kHeartbeatMs = 30000;
// The pose detail is bounded on top of its interval: the composition evidence
// (clean vs tracker vs result) is all there in the first 40s, and left running
// it costs ~400 KB an hour and buries the startup chain a user is asked to
// read. The heartbeat carries liveness thereafter.
constexpr std::uint64_t kPoseDetailMs    = 2000;
constexpr int           kPoseDetailLines = 20;
constexpr std::uint64_t kCallerSummaryEvery = 1800;
constexpr std::uint64_t kWidgetProbeMs     = 10000;
constexpr int           kWidgetProbePasses = 8;

// ---- field of view -------------------------------------------------------
// The game ships no FOV control of its own: its settings object carries
// ColourBlindMode, ReticleSize, HeadRollAmount and the rest of the accessibility
// block, and nothing for FOV. The engine's value is still reachable from here.
//
// ULocalPlayer::GetViewPoint (fn 0x038d8e60, the render/projection caller this
// hook injects for) fills the FMinimalViewInfo it was handed like this:
//
//     OutViewInfo      = CameraManager->GetCameraCacheView();   // vfn +0x750
//     OutViewInfo.FOV  = CameraManager->GetFOVAngle();          // vfn +0x7c8
//     PC->GetPlayerViewPoint(&OutViewInfo.Location,             // vfn +0x7f8
//                            &OutViewInfo.Rotation);            //   <- this hook
//     ... scene view extensions ...
//     OutViewInfo.DesiredFOV = OutViewInfo.FOV;
//
// So the FOV this frame will be drawn with is already in the struct when the
// hook runs, one FVector and one FRotator past the Location pointer we were
// handed, and it is still ours to change: DesiredFOV is copied from it after we
// return, and the projection matrix is built from it later still. The engine
// re-reads GetFOVAngle() every frame, so an offset written here cannot
// accumulate - the base is always the game's own current FOV, including the
// AdditionalFOV Habitat pushes in on movement.

// What the game asked for and what the frame is being drawn with, for the
// heartbeat - that log line is how a user finds out what FovOffset is adding to.
std::atomic<float> g_gameFov{0.0f};
std::atomic<float> g_renderFov{0.0f};

// Returns the FOV the frame will actually render with, or 0 when the out-params
// are not fields of one FMinimalViewInfo, or the value there is not a
// believable angle.
float ApplyFovOffset(FVector* outLocation, FRotator* outRotation, bool allowOffset) {
    const auto& mvi = Offsets().MinimalViewInfoLayout;
    const auto locAddr = reinterpret_cast<std::uintptr_t>(outLocation);
    const auto rotAddr = reinterpret_cast<std::uintptr_t>(outRotation);
    // Unless the two pointers are exactly one FVector apart they are not fields
    // of one view info, and reading - let alone writing - past the first would
    // land in some other caller's stack frame.
    if (rotAddr - locAddr != mvi.kRotationStride) return 0.0f;

    float fov = 0.0f;
    if (!ue::SafeReadFloat(locAddr + mvi.kFovOffset, fov)) return 0.0f;
    // Phrased as a range test rather than its negation so a NaN, which fails
    // every comparison, is rejected instead of passed through.
    if (!(fov >= AimProjection::kFovMinDegrees && fov <= AimProjection::kFovMaxDegrees))
        return 0.0f;
    g_gameFov.store(fov, std::memory_order_relaxed);

    const float offset = g_deps.config->fov_offset;
    float render = fov;
    if (allowOffset && offset != 0.0f) {
        render = fov + offset;
        if (render < AimProjection::kFovMinDegrees) render = AimProjection::kFovMinDegrees;
        if (render > AimProjection::kFovMaxDegrees) render = AimProjection::kFovMaxDegrees;
        if (!ue::SafeWriteFloat(locAddr + mvi.kFovOffset, render))
            render = fov;
    }
    g_renderFov.store(render, std::memory_order_relaxed);

    static std::atomic<bool> s_announced{false};
    if (!s_announced.exchange(true, std::memory_order_relaxed)) {
        Log::Line("fov: the game renders at %.1f degrees, [Camera] FovOffset=%.1f -> %.1f "
                  "(the reticle projection follows the same value)",
                  fov, offset, render);
    }
    return render;
}

// ---- caller gate ---------------------------------------------------------
bool ShouldInjectForCaller(std::uintptr_t retRva, int mode) {
    return inject::ShouldInject(retRva, mode, Offsets().kKnownCallerRvas);
}

// Caller distribution accounting, only meaningful in the all-callers mode -
// that's how the render caller gets (re-)confirmed after a patch. Off the hot
// path otherwise.
std::mutex g_callerMutex;
std::unordered_map<std::uintptr_t, std::uint64_t> g_callerCounts;
std::atomic<std::uint64_t> g_callerLastSummary{0};

void DumpCallerSummary(std::uint64_t total) {
    std::lock_guard<std::mutex> lk(g_callerMutex);
    Log::Line("caller-summary @%llu calls: %zu unique return RVAs:",
        static_cast<unsigned long long>(total), g_callerCounts.size());
    for (const auto& kv : g_callerCounts)
        Log::Line("  ret RVA 0x%08llx  count=%llu",
            static_cast<unsigned long long>(kv.first),
            static_cast<unsigned long long>(kv.second));
}

void CountCaller(std::uintptr_t retRva, std::uint64_t call) {
    { std::lock_guard<std::mutex> lk(g_callerMutex); ++g_callerCounts[retRva]; }
    if (call - g_callerLastSummary.load(std::memory_order_relaxed) >= kCallerSummaryEvery) {
        g_callerLastSummary.store(call, std::memory_order_relaxed);
        DumpCallerSummary(call);
    }
}

// ---- gameplay / cutscene gates -------------------------------------------
// APlayerController::bShowMouseCursor. UE raises that flag when input belongs
// to a menu rather than the player, so a raised cursor means menu / pause /
// options and tracking is suppressed. `self` at this hook IS the controller, so
// the gate is one guarded load off a pointer we already hold - no sampling
// thread, and no confusion with a cursor some other window put up.
bool InGameplay(std::uintptr_t controller) {
    std::uint32_t flags = 0;
    if (!ue::SafeReadU32(controller + Offsets().kShowMouseCursorOffset, flags))
        return false;
    return (flags & Offsets().kShowMouseCursorMask) == 0;
}

// Cleared for the session if the flag ever reads as something that is not a
// bool, which means it is not where the build profile says it is.
std::atomic<bool> g_cutsceneGateOk{true};

// AHabitatPlayerController's own cutscene flag, the byte behind the game's
// GetCutsceneMode(). A cutscene leaves bShowMouseCursor clear, so the menu gate
// above never sees one; this is the state that separates the two. Same
// controller pointer, one more guarded load.
//
// Head tracking is NOT gated on this - looking around during a cutscene is the
// point of it. The only thing that stands down for a cutscene is the FOV
// offset, because a cutscene is composed at a chosen focal length.
bool InCutscene(std::uintptr_t controller) {
    const std::size_t off = Offsets().kCutsceneModeOffset;
    if (off == 0 || !g_cutsceneGateOk.load(std::memory_order_relaxed))
        return false;

    // Core has no byte-wide guarded read; the second byte this picks up is
    // still inside the controller (the class is 0x910 bytes) and is discarded.
    std::uint16_t raw = 0;
    if (!ue::SafeReadU16(controller + off, raw))
        return false;

    const unsigned flag = raw & 0xffu;
    if (flag > 1) {
        // A UE bool is 0 or 1. Anything else says this object is not the class
        // the offset was derived against, and every later read would be a coin
        // toss between "cutscene" and "not", i.e. head tracking dying at
        // random. Say so once and stop reading rather than act on it.
        if (g_cutsceneGateOk.exchange(false, std::memory_order_relaxed)) {
            Log::Line("cutscene gate: controller+0x%zx holds %u, which is not a bool - "
                      "the cutscene flag is not where this build profile says it is. "
                      "The FOV offset will stay applied through cutscenes for the rest "
                      "of this session.",
                      off, flag);
        }
        return false;
    }
    return flag != 0;
}

// ---- diagnostics ---------------------------------------------------------
// One line per transition, so a session log shows exactly which scenes the gate
// caught without the heartbeat having to land inside one. Worth keeping even
// though tracking no longer stands down for a cutscene: if the view misbehaves
// during one, this is what says the game called it a cutscene.
void LogCutsceneTransition(bool inCutscene) {
    static std::atomic<bool> s_wasInCutscene{false};
    if (inCutscene != s_wasInCutscene.exchange(inCutscene, std::memory_order_relaxed))
        Log::Line("cutscene %s - head tracking stays on, FOV offset %s",
                  inCutscene ? "started" : "ended",
                  inCutscene ? "suspended" : "resumed");
}

// Distinguishes "hook never fired" from "no tracker data".
void LogHeartbeat(std::uint64_t call, std::uintptr_t retRva,
                  bool inGameplay, bool inCutscene, int mode) {
    static std::atomic<std::uint64_t> s_lastTick{0};
    const std::uint64_t now = GetTickCount64();
    if (call != 1 && (now - s_lastTick.load(std::memory_order_relaxed)) < kHeartbeatMs)
        return;
    s_lastTick.store(now, std::memory_order_relaxed);

    auto* receiver = g_deps.receiver;
    float hy = 0, hp = 0, hr = 0;
    const bool data = receiver && receiver->GetRotation(hy, hp, hr);
    // Port state alongside the data flag, because "udpData=NO" alone cannot
    // tell a stalled tracker from a port another game still holds - and only
    // the second one resolves itself.
    const char* udpPort = !receiver              ? "none"
                        : receiver->IsRetrying() ? "waiting-for-port"
                        : receiver->IsRunning()  ? "listening"
                                                 : "down";
    Log::Line("heartbeat hook=%llu retRVA=0x%08llx enabled=%s gameplay=%s cutscene=%s udpPort=%s udpData=%s raw=(Y=%.2f P=%.2f R=%.2f) fov=%.1f->%.1f yawMode=%s injectMode=%d",
        static_cast<unsigned long long>(call),
        static_cast<unsigned long long>(retRva),
        g_trackingEnabled.load() ? "ON" : "OFF",
        inGameplay ? "YES" : "NO",
        inCutscene ? "YES" : "NO",
        udpPort,
        data ? "YES" : "NO", hy, hp, hr,
        g_gameFov.load(std::memory_order_relaxed),
        g_renderFov.load(std::memory_order_relaxed),
        g_worldSpaceYaw.load() ? "world" : "local", mode);
}

// Widget discovery. Every GetPlayerViewPoint caller is on the game thread,
// which is what walking the object table requires; it deliberately does not
// wait for tracker data, since finding the widgets has nothing to do with
// having a pose. Repeats on a timer rather than firing once, because the
// interaction prompt only exists as a live object while the player is looking
// at something a single early pass would always miss.
void MaybeRunWidgetProbe() {
    static std::atomic<bool> s_disabled{false};
    static std::atomic<std::uint64_t> s_lastPass{0};
    static std::atomic<int> s_passesRun{0};

    if (s_disabled.load(std::memory_order_relaxed)) return;
    if (s_passesRun.load(std::memory_order_relaxed) >= kWidgetProbePasses) return;
    const std::uint64_t now = GetTickCount64();
    if ((now - s_lastPass.load(std::memory_order_relaxed)) < kWidgetProbeMs) return;
    s_lastPass.store(now, std::memory_order_relaxed);

    const int pass = s_passesRun.fetch_add(1, std::memory_order_relaxed);
    // A profile whose UObject globals do not check out cannot be walked at all,
    // so the first failure ends the probe rather than repeating it.
    if (pass == 0 && !WidgetProbe::ValidateGlobals()) {
        s_disabled.store(true, std::memory_order_relaxed);
        return;
    }
    Log::Line("widget-probe: pass %d", pass);
    WidgetProbe::DumpCandidates();
}

struct PoseSample {
    std::uint64_t  Call;
    std::uintptr_t RetRva;
    FRotator       Clean;
    float          Yaw, Pitch, Roll;
    FRotator       Result;
    float          OffsetX, OffsetY, OffsetZ;
    FVector        PositionOffsetUE;
};

// Time-gated rather than call-gated because this fires on the render caller, so
// a call-count interval would log at the player's frame rate - four lines a
// second at 144Hz.
void LogPoseDetail(const PoseSample& s) {
    static std::atomic<std::uint64_t> s_lastLine{0};
    static std::atomic<int> s_linesWritten{0};
    if (s_linesWritten.load(std::memory_order_relaxed) >= kPoseDetailLines) return;
    const std::uint64_t now = GetTickCount64();
    if (s.Call != 1 && (now - s_lastLine.load(std::memory_order_relaxed)) < kPoseDetailMs)
        return;
    s_lastLine.store(now, std::memory_order_relaxed);
    s_linesWritten.fetch_add(1, std::memory_order_relaxed);

    Log::Line("hook #%llu retRVA=0x%08llx clean=(Y=%.2f P=%.2f R=%.2f) tracker=(Y=%.2f P=%.2f R=%.2f) result=(Y=%.2f P=%.2f R=%.2f) headOff_m=(x%.3f y%.3f z%.3f) posOff_ue=(%.1f,%.1f,%.1f)",
        static_cast<unsigned long long>(s.Call),
        static_cast<unsigned long long>(s.RetRva),
        s.Clean.Yaw, s.Clean.Pitch, s.Clean.Roll, s.Yaw, s.Pitch, s.Roll,
        s.Result.Yaw, s.Result.Pitch, s.Result.Roll,
        s.OffsetX, s.OffsetY, s.OffsetZ,
        s.PositionOffsetUE.X, s.PositionOffsetUE.Y, s.PositionOffsetUE.Z);
}

std::uintptr_t ReturnRva(const void* returnAddress) {
    const auto addr = reinterpret_cast<std::uintptr_t>(returnAddress);
    return ue::ModuleBase() != 0 ? addr - ue::ModuleBase() : addr;
}

void __fastcall GetPlayerViewPoint_Hook(void* self, FVector* outLocation, FRotator* outRotation) {
    const std::uintptr_t retRva = ReturnRva(_ReturnAddress());
    const auto controller = reinterpret_cast<std::uintptr_t>(self);

    const bool inGameplay = InGameplay(controller);
    const bool inCutscene = InCutscene(controller);
    LogCutsceneTransition(inCutscene);

    g_origGetPlayerViewPoint(self, outLocation, outRotation);
    const FRotator clean = *outRotation;

    const auto call = g_hookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const int mode = g_injectMode.load(std::memory_order_relaxed);
    if (mode == inject::kAllCallers)
        CountCaller(retRva, call);

    // Field of view, ahead of anything to do with head tracking: the offset is a
    // separate feature and stays applied while tracking is toggled off. Only the
    // render/projection caller is touched, and never in the all-callers mode -
    // the stride test alone is not licence enough to write four bytes into
    // whatever stack frame an unrecognised caller handed us. Menus and cutscenes
    // keep the framing the game chose: head tracking runs through a cutscene,
    // but a cutscene is composed at a chosen focal length and widening it moves
    // what the shot was framed around.
    const bool renderCaller = ShouldInjectForCaller(retRva, mode);
    if (renderCaller && mode != inject::kAllCallers) {
        const float renderFov = ApplyFovOffset(
            outLocation, outRotation, inGameplay && !inCutscene);
        if (renderFov > 0.0f)
            AimProjection::SetFovDegrees(renderFov);
    }

    LogHeartbeat(call, retRva, inGameplay, inCutscene, mode);

    const Config& config = *g_deps.config;
    if (config.widget_dump && inGameplay)
        MaybeRunWidgetProbe();

    // Re-parenting the glare card onto the beam's spring arm is a one-off
    // structural change with no pose in it, so it runs on the gameplay gate
    // rather than on tracking being enabled - and it self-heals after a level
    // change, which is why it is called every frame rather than once. The pass
    // time-gates itself down to one object-table walk every 15 seconds.
    if (config.torch_follows_head && config.torch_flare_follows_beam && inGameplay)
        TorchFlare::Tick();

    // Suppression tracked across calls so the reticle can be put back exactly
    // once on the way out. Without that the widgets keep whatever translation
    // the last tracked frame gave them for as long as the menu is up, since the
    // pass that moves them lives below this return.
    static std::atomic<bool> s_suppressed{false};

    if (!g_trackingEnabled.load(std::memory_order_relaxed) || !g_deps.session
            || !inGameplay) {
        AimProjection::SetActive(false);
        TorchAim::Publish(false, 0.0f, 0.0f, 0.0f, false);
        if (!s_suppressed.exchange(true, std::memory_order_relaxed))
            ReticleMover::Tick();
        return;
    }
    s_suppressed.store(false, std::memory_order_relaxed);

    // Decoupling: only the render-path caller(s) get the head pose written back.
    // Every other GetPlayerViewPoint caller (interaction traces, audio listener,
    // AI perception, replication) keeps the clean mouse/pad rotation.
    if (!renderCaller) {
        // Nothing below runs with the gate shut, so without this the torch would
        // hold whatever pose it was last handed while the view sits untracked.
        if (mode == inject::kNone)
            TorchAim::Publish(false, 0.0f, 0.0f, 0.0f, false);
        return;
    }

    Session& session = *g_deps.session;
    if (!session.Update(g_frameClock.Tick()))
        return;

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    if (!session.GetRotation(yaw, pitch, roll))
        return;

    const bool worldSpaceYaw = g_worldSpaceYaw.load(std::memory_order_relaxed);

    // The torch's spring arm ticks well before this hook runs, so it composes
    // the beam from the pose published on the previous frame. One frame behind
    // a view that itself carries the arm's own rotation lag is not something
    // the eye has any way to see.
    TorchAim::Publish(true, yaw, pitch, roll, worldSpaceYaw);

    const FQuat4d cleanQ = ue::QuatFromEulerDeg(clean.Pitch, clean.Yaw, clean.Roll);
    camera_boundary::ApplyHeadPose(*outRotation, yaw, pitch, roll, worldSpaceYaw);

    // Publish where the clean-aim ray now lands in the head-tracked view, so
    // the UMG reticle and prompt can be moved off screen centre to meet it.
    // qrel = tracked^-1 * clean, built from the same quaternions this hook just
    // composed rather than re-derived from the Euler angles - one derivation
    // used twice cannot disagree with itself.
    {
        const FQuat4d trackedQ = ue::QuatFromEulerDeg(
            outRotation->Pitch, outRotation->Yaw, outRotation->Roll);
        const FQuat4d relative = ue::QuatMul(ue::QuatInv(trackedQ), cleanQ);
        AimProjection::UpdateAim(relative.X, relative.Y, relative.Z, relative.W, true);
    }

    ReticleMover::Tick();

    FVector positionOffset{0.0, 0.0, 0.0};
    float offX = 0.0f, offY = 0.0f, offZ = 0.0f;
    if (session.GetPositionOffset(offX, offY, offZ)) {
        positionOffset = camera_boundary::PositionOffset(cleanQ, offX, offY, offZ);
        outLocation->X += positionOffset.X;
        outLocation->Y += positionOffset.Y;
        outLocation->Z += positionOffset.Z;
    }

    LogPoseDetail({call, retRva, clean, yaw, pitch, roll, *outRotation,
                   offX, offY, offZ, positionOffset});
}

}  // namespace

bool Install(const Dependencies& deps) {
    g_deps = deps;
    g_trackingEnabled.store(deps.config->enable_on_startup);
    g_worldSpaceYaw.store(deps.config->world_space_yaw);
    g_injectMode.store(Offsets().kDefaultInjectMode);

    auto& hm = cameraunlock::hooks::HookManager::Instance();
    void* target = reinterpret_cast<void*>(
        ue::ModuleBase() + Offsets().kGetPlayerViewPointRva);
    if (auto s = hm.CreateHook(target, reinterpret_cast<void*>(&GetPlayerViewPoint_Hook),
                               reinterpret_cast<void**>(&g_origGetPlayerViewPoint));
        s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: CreateHook(GetPlayerViewPoint) failed: %s", cameraunlock::hooks::HookStatusToString(s));
        return false;
    }
    if (auto s = hm.EnableHook(target); s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: EnableHook failed: %s", cameraunlock::hooks::HookStatusToString(s));
        return false;
    }
    Log::Line("GetPlayerViewPoint hooked at RVA 0x%08llx (default inject mode %d)",
        static_cast<unsigned long long>(Offsets().kGetPlayerViewPointRva),
        g_injectMode.load());
    return true;
}

bool TrackingEnabled() { return g_trackingEnabled.load(); }
void SetTrackingEnabled(bool enabled) { g_trackingEnabled.store(enabled); }

bool WorldSpaceYaw() { return g_worldSpaceYaw.load(); }
void SetWorldSpaceYaw(bool worldSpaceYaw) { g_worldSpaceYaw.store(worldSpaceYaw); }

int  InjectMode() { return g_injectMode.load(); }
void SetInjectMode(int mode) { g_injectMode.store(mode); }

}  // namespace swtd_ht::view_hook
