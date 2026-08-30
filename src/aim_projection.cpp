// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Clean-aim -> screen-offset projection for the body-forward reticle.
//
// Ported from subnautica-2-headtracking's aim_projection.cpp, which solves the
// same problem on the same engine behind the same GetPlayerViewPoint hook.
// Diffed rather than re-derived on purpose: those numbers were verified in a
// running game once already.
//
// CPU-only, no swapchain involvement. The offset computed here drives
// SetRenderTranslation on the game's own UMG widgets. Subnautica 2 started with
// a kiero+ImGui DX12 overlay and had to abandon it: a third-party Present hook
// device-removes the GPU when Streamline owns presentation for DLSS Frame
// Generation. Still Wakes the Deep ships sl.interposer.dll, so an overlay would
// walk into the same fault here.
//
// Rotation only, deliberately. The reticle marks a POINT, and projecting a
// direction instead treats that point as infinitely far away, which leaves the
// parallax term lean/distance uncorrected once positional tracking moves the
// render eye off the shot eye. Correcting it needs the live distance to what
// the player is pointing at, and taking that from anything fixed, smoothed or
// stale is worse than leaving it out (see the place-the-reticle-under-the-shot
// doctrine). Bring parallax back only with a live per-frame aim distance read
// from the game's own interaction trace.

#include <cameraunlock/rendering/aim_quat_projection.h>

#include "aim_projection.h"
#include "logging.h"

#include <atomic>
#include <mutex>
#include <windows.h>

namespace swtd_ht::AimProjection
{
    namespace
    {
        // Horizontal FOV at the 16:9 reference aspect. The GPV hook reads the
        // engine's live FMinimalViewInfo.FOV each render-caller frame and pushes
        // it here, so the projection follows whatever the game is rendering.
        // The seed is only what stands in until the first live value lands.
        constexpr float kFovDefaultAt16x9 = 90.0f;
        constexpr float kRefAspect = 16.0f / 9.0f;
        std::atomic<float> g_fovHorizontalAt16x9{kFovDefaultAt16x9};

        std::mutex g_aimMutex;
        double g_qx = 0.0, g_qy = 0.0, g_qz = 0.0, g_qw = 1.0;
        bool   g_aimActive = false;

        std::atomic<float> g_offsetX{0.0f};
        std::atomic<float> g_offsetY{0.0f};
        std::atomic<bool>  g_offsetValid{false};

        // The game's main window, for the viewport dimensions. Largest visible
        // top-level window of this process, which skips the splash screen;
        // re-enumerated if the cached handle dies, since UE recreates the window
        // on a fullscreen-mode change.
        HWND g_gameWindow = nullptr;

        HWND GameWindow()
        {
            if (g_gameWindow && IsWindow(g_gameWindow)) return g_gameWindow;
            struct Ctx { DWORD pid; HWND best; LONG bestArea; };
            Ctx ctx{GetCurrentProcessId(), nullptr, 0};
            EnumWindows([](HWND wnd, LPARAM param) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(param);
                DWORD pid = 0;
                GetWindowThreadProcessId(wnd, &pid);
                if (pid != c->pid || !IsWindowVisible(wnd)) return TRUE;
                RECT rc{};
                if (!GetClientRect(wnd, &rc)) return TRUE;
                const LONG area = rc.right * rc.bottom;
                if (area > c->bestArea) { c->best = wnd; c->bestArea = area; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&ctx));
            if (ctx.best && ctx.best != g_gameWindow) {
                RECT rc{};
                GetClientRect(ctx.best, &rc);
                Log::Line("aim-projection: game window 0x%llx client=%ldx%ld",
                    reinterpret_cast<unsigned long long>(ctx.best), rc.right, rc.bottom);
            }
            g_gameWindow = ctx.best;
            return ctx.best;
        }

        // Caller holds g_aimMutex.
        void RecomputeOffsetLocked()
        {
            if (!g_aimActive) {
                g_offsetValid.store(false, std::memory_order_relaxed);
                return;
            }
            RECT rc{};
            const HWND wnd = GameWindow();
            if (!wnd || !GetClientRect(wnd, &rc) || rc.right <= 0 || rc.bottom <= 0) {
                g_offsetValid.store(false, std::memory_order_relaxed);
                return;
            }
            const float w = static_cast<float>(rc.right);
            const float h = static_cast<float>(rc.bottom);

            // Hor+ (MaintainYFOV) aspect scaling and viewport-edge NDC clamping
            // live in cameraunlock-core, which also documents why this is a
            // quaternion projection rather than per-axis yaw/pitch tangents:
            // the tangent form drifts as soon as roll joins pitch.
            const auto proj = cameraunlock::rendering::ProjectAimQuatHorPlus(
                g_qx, g_qy, g_qz, g_qw, w, h,
                g_fovHorizontalAt16x9.load(std::memory_order_relaxed), kRefAspect);
            if (!proj.inFront) {
                g_offsetValid.store(false, std::memory_order_relaxed);
                return;
            }
            g_offsetX.store(proj.screenX - w * 0.5f, std::memory_order_relaxed);
            g_offsetY.store(proj.screenY - h * 0.5f, std::memory_order_relaxed);
            g_offsetValid.store(true, std::memory_order_relaxed);
        }
    }

    void UpdateAim(double qx, double qy, double qz, double qw, bool active)
    {
        std::lock_guard<std::mutex> lk(g_aimMutex);
        g_qx = qx; g_qy = qy; g_qz = qz; g_qw = qw;
        g_aimActive = active;
        RecomputeOffsetLocked();
    }

    void SetActive(bool active)
    {
        std::lock_guard<std::mutex> lk(g_aimMutex);
        g_aimActive = active;
        RecomputeOffsetLocked();
    }

    void SetFovDegrees(float fovDegrees)
    {
        // Reject implausible reads and keep the last good value.
        if (fovDegrees >= kFovMinDegrees && fovDegrees <= kFovMaxDegrees)
            g_fovHorizontalAt16x9.store(fovDegrees, std::memory_order_relaxed);
    }

    bool GetScreenOffset(float& dx, float& dy)
    {
        if (!g_offsetValid.load(std::memory_order_relaxed)) return false;
        dx = g_offsetX.load(std::memory_order_relaxed);
        dy = g_offsetY.load(std::memory_order_relaxed);
        return true;
    }
}
