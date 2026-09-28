// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Bootstrap: everything that has to happen once, in order, before the hook can
// run - log, crash handler, config, build-profile fingerprint, module range,
// UDP receiver, tracking session, hooks, hotkeys. It runs on its own thread so
// none of it happens under the loader lock.

#include "headtracking_mod.h"

#include <memory>
#include <string>

#include <windows.h>
#include <psapi.h>

#include "builds/build_registry.h"
#include "config.h"
#include "logging.h"
#include "mod_hotkeys.h"
#include "session.h"
#include "torch_aim.h"
#include "view_hook.h"

#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/os/module_paths.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace swtd_ht {

namespace {

namespace ue = ::cameraunlock::unreal;

HANDLE g_bootstrapThread = nullptr;

Config g_config;
std::unique_ptr<cameraunlock::UdpReceiver> g_receiver;
std::unique_ptr<Session> g_session;

// The processors keep their identity sensitivity and inversion: the pose is
// applied as the tracker sends it.
void ApplyConfigToSession() {
    auto& ps = g_session->GetPositionProcessor().GetSettings();
    ps.limit_x       = g_config.position_limit_x;
    ps.limit_y       = g_config.position_limit_y;
    ps.limit_y_down  = g_config.position_limit_y_down;
    ps.limit_z       = g_config.position_limit_z;
    ps.limit_z_back  = g_config.position_limit_z_back;

    // The session feeds both the rotation and the position processor - there is
    // no separate position smoothing setting - and picks between the two values
    // per connection from the receiver's IsRemoteConnection(), re-read on every
    // Update().
    static_assert(Session::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() or smoothing silently stays local");
    g_session->SetLocalSmoothing(g_config.local_smoothing);
    g_session->SetRemoteSmoothing(g_config.remote_smoothing);

    g_session->SetMode(config::StartupTrackingMode(g_config));
}

std::wstring ExeDir() {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    const auto slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

void OpenLog() {
    // Core opens with CREATE_ALWAYS, so the log holds this session only, and
    // rotates the outgoing one to HeadTracking.prev.log first - the session
    // worth reading is usually the one that just crashed, and the user
    // relaunches the game before sending the file. It warns by itself when the
    // rotation fails, so there is nothing to do here but name the file.
    Log::Open(ExeDir() + L"\\HeadTracking.log");
    Log::Line("=== Still Wakes the Deep Head Tracking v" SWTD_HT_VERSION " (UE5.4) ===");
}

// CameraUnlock.ini beside the game exe, read, imported or created by the config
// owner.
void LoadConfig() {
    // Empty only when the exe's own path cannot be read. The owner needs a full
    // path, and a relative one would put the file wherever the game was started
    // from, so the settings stay at their defaults and nothing is saved.
    const std::wstring exeDir = cameraunlock::os::HostExeDirectory();
    if (exeDir.empty()) {
        Log::Line("config: the game directory could not be read, so CameraUnlock.ini "
                  "cannot be read or written - using defaults, and nothing is saved");
        g_config = config::Table().defaults();
        return;
    }
    g_config = config::Load(exeDir, cameraunlock::config::DefaultsFile::PerUser());
}

// Fingerprint the host EXE against the build registry. False leaves the mod
// fully dormant: no hooks installed, game runs vanilla.
bool SelectBuildProfile(HMODULE host) {
    const auto match = builds::SelectProfile(host);
    switch (match) {
        case builds::MatchResult::Matched:
            return true;
        case builds::MatchResult::HostNewer:
            Log::Line("build-check: this game build is NEWER than any profile this "
                      "mod knows about - check the releases page for an update. "
                      "Staying dormant; game runs vanilla.");
            return false;
        case builds::MatchResult::HostOlder:
            Log::Line("build-check: this game build is OLDER than the profile - let "
                      "Steam finish updating. Staying dormant; game runs vanilla.");
            return false;
        default:
            Log::Line("build-check: no matching/complete profile - staying dormant; "
                      "game runs vanilla.");
            return false;
    }
}

// Hand the shared UE runtime the module range every RVA is resolved against.
bool PublishModuleRange(HMODULE host) {
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), host, &mi, sizeof(mi))) {
        Log::Line("FATAL: GetModuleInformation failed - cannot resolve RVAs");
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll);
    ue::SetRuntime(base, base + mi.SizeOfImage, Offsets().UObjectGlobals);
    Log::Line("module base=0x%llx size=0x%x",
        static_cast<unsigned long long>(base), mi.SizeOfImage);
    return true;
}

void StartTracking() {
    g_receiver = std::make_unique<cameraunlock::UdpReceiver>();
    g_receiver->SetLog([](const std::string& m) { Log::Line("udp: %s", m.c_str()); });
    g_receiver->Start(static_cast<uint16_t>(g_config.udp_port));

    g_session = std::make_unique<Session>(*g_receiver);
    ApplyConfigToSession();
}

DWORD WINAPI BootstrapThread(LPVOID) {
    OpenLog();
    cameraunlock::diagnostics::InstallCrashHandler();
    LoadConfig();

    HMODULE host = GetModuleHandleW(nullptr);
    if (!SelectBuildProfile(host)) return 0;
    if (!PublishModuleRange(host)) return 0;

    StartTracking();

    auto& hm = cameraunlock::hooks::HookManager::Instance();
    if (auto s = hm.Initialize(); s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: MinHook init failed: %s", cameraunlock::hooks::HookStatusToString(s));
        return 0;
    }

    if (!view_hook::Install({&g_config, g_session.get(), g_receiver.get()}))
        return 0;

    // Always installed: at LightMultiplier=0 the turn is identity, so the beam sits on the
    // game's own aim without a second switch deciding whether the hook exists at all.
    TorchAim::Install(g_config.light_multiplier);

    hotkeys::Register(g_config, *g_session);
    Log::Line("init complete. Yaw mode %s. Waiting for OpenTrack on UDP %d.",
        g_config.world_space_yaw ? "world" : "local", g_config.udp_port);
    return 0;
}

}  // namespace

void Initialize(HMODULE) {
    g_bootstrapThread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
}

void Shutdown() {
    hotkeys::Stop();
    if (g_receiver) g_receiver->Stop();
    cameraunlock::hooks::HookManager::Instance().Shutdown();
    Log::Line("shutdown");
    Log::Close();
    if (g_bootstrapThread) { CloseHandle(g_bootstrapThread); g_bootstrapThread = nullptr; }
}

}  // namespace swtd_ht
