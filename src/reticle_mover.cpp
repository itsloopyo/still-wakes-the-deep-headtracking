// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Unstick the crosshair and the interaction prompt.
//
// The interaction ray keeps the clean mouse-driven rotation while the view
// follows the head, so the crosshair drawn at the centre of the picture stops
// marking the ray the moment the player looks off-centre. UMG anchors it there,
// so the fix is to translate the widget by the offset the aim projection
// computes.
//
// Ported from subnautica-2-headtracking, which does the same thing on the same
// engine. The widgets were found by walking the live object table (see
// widget_probe.cpp).
//
// They do not all live in the same place, which is the trap. The in-game HUD
// HBTT_HUD_InGameSimple_C owns the reticle (HBTT_Crosshair, wrapping the
// CrossHair and StealthCrosshair images) and the look-at tooltip
// (WBP_TooltipHUD) directly. The interaction prompt - the input glyph plus its
// verb, "OPEN" and the like - is NOT under the HUD: the HUD only has an empty
// PromptManagerSlot, and the widget that fills it, BP_PromptManager_C, is
// created under BP_HabitatGameInstance_C. An outer test written for the HUD
// tree therefore misses the prompt entirely, which is why it stayed pinned
// below screen centre while the reticle moved.
//
// Everything else on the HUD - objective, puppet-state, level-transition - is
// screen furniture that belongs where the game put it and is left alone. So is
// Skip_Prompt: it is a BP_PromptBase like the interaction prompts, but it hangs
// off CutsceneWidget rather than the prompt manager, so the outer test leaves
// the cutscene skip hint where it should be, in its corner.

#include "reticle_mover.h"

#include <cstdint>
#include <string>

#include <windows.h>

#include "aim_projection.h"
#include "builds/build_registry.h"
#include "logging.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace swtd_ht::ReticleMover {

namespace {

namespace ue = ::cameraunlock::unreal;

// The widgets that follow the aim, by object name plus a required outer.
//
// Each name exists twice while the game is running: once as the template
// inside the loaded widget Blueprint, and once as the widget the HUD actually
// built. Walking the live object table finds both, so the outer has to name
// something that only exists at runtime, or the walk can settle on a template
// that is never painted and the reticle sits dead at screen centre. Measured
// outer chains, HBTT_Crosshair (WBP_TooltipHUD is identical):
//
//   template  WidgetTree / HBTT_HUD_InGameSimple_C / /Game/Habitat/UI/HBTT_HUD_InGameSimple
//   live      WidgetTree / HBTT_HUD_InGameSimple_C / BP_HabitatHUDInGame_C / PersistentLevel / <map>
//
// so testing for the widget Blueprint's own name matches both, and the HUD
// actor, BP_HabitatHUDInGame, is what tells them apart. The prompt manager is
// the same story against BP_HabitatGameInstance, which also keeps Skip_Prompt
// out by construction: same class, different outer.
struct Target { const char* Name; const char* Outer; };
constexpr Target kTargets[] = {
    { "HBTT_Crosshair",     "BP_HabitatHUDInGame" },
    { "WBP_TooltipHUD",     "BP_HabitatHUDInGame" },
    { "BP_PromptManager_C", "BP_HabitatGameInstance" },
};
constexpr std::size_t kNumTargets = sizeof(kTargets) / sizeof(kTargets[0]);

// How often the object table is re-walked to re-find the targets: the short
// interval once a held widget has failed its liveness test, the long one as a
// backstop while everything still looks fine. The backstop is what bounds how
// long the reticle can sit dead at screen centre if a rebuilt HUD ever leaves
// the widget we hold alive but no longer painted, which no test on the pointer
// itself can see. A walk costs about 5ms, so 15s of it is not worth measuring;
// running it every couple of seconds regardless would be.
constexpr std::uint64_t kRetryWalkMs    = 2000;
constexpr std::uint64_t kBackstopWalkMs = 15000;

// How often a push happens anyway with the offset unchanged, so a game-side
// reset of RenderTransform cannot stick. Counted in calls, which arrive at
// frame rate.
constexpr std::uint64_t kReassertEveryCalls = 120;

// The offset line is evidence that the widgets follow the aim, and that reads
// off the first few lines; the heartbeat carries liveness for the rest of the
// session. Bounded on top of its interval because counting pushes tied the
// rate to the frame rate: 200 of the 275 lines in a seven-minute session were
// this one line, repeating that the reticle was still following.
constexpr std::uint64_t kOffsetLogMs    = 2000;
constexpr int           kOffsetLogLines = 20;

// A widget plus the class pointer it carried when collected. Loading a level
// frees and recreates the HUD, so a held pointer can dangle or be reused for a
// different object.
struct Widget { std::uintptr_t Obj = 0; std::uintptr_t Cls = 0; };

Widget g_widgets[kNumTargets];

// FName comparison ids for the target names, learned by the first walk that
// finds them. ObjectName() ignores the FName number, so comparing the id is
// the same test as the string compare that learned it, minus a name-pool
// lookup and a std::string build for every one of ~100k objects - which is
// what makes re-walking on a timer affordable.
std::uint32_t g_nameIds[kNumTargets] = {};

// How long the last walk took, reported in the throttled offset line so the
// cost of running it repeatedly stays visible rather than assumed.
float g_lastWalkMs = 0.0f;
std::uintptr_t g_setRenderTranslationFn = 0;
std::uintptr_t g_getViewportScaleFn = 0;
std::uintptr_t g_widgetLayoutLibCdo = 0;

// UMG paints a widget's render translation in slate units, which become
// units * DPI-scale real pixels, while the projected offset is real pixels. The
// movers divide by this. UE's default curve gives 1.0 at 1080p and 1.333 at
// 1440p-tall, so ignoring it overshoots by a third on a 1440p display.
float g_dpiScale = 1.0f;

// What a viewport scale can believably be. UE's default curve gives 1.0 at
// 1080p and 1.333 at 1440p-tall; anything outside this says the call did not
// return what we think it did.
constexpr float kMinPlausibleDpiScale = 0.05f;
constexpr float kMaxPlausibleDpiScale = 20.0f;

// GUObjectArray is the authority on whether an object still exists. Freed
// UObject memory keeps its old class pointer for as long as the allocator
// leaves it alone, so a class-pointer test on its own reports a destroyed
// widget as live indefinitely - the mod then pushes translations into a
// widget nothing paints, and the log says everything is fine. The array item
// at the object's own InternalIndex points back at it only while it is
// registered: destruction nulls that entry, and a reused index points at
// whatever took the slot.
bool RegisteredInObjectArray(std::uintptr_t obj) {
    const auto& g = Offsets().UObjectGlobals;
    if (g.kChunkNumElems == 0 || g.kFUObjectItemSize == 0) return false;
    // UObjectBase packs InternalIndex immediately before ClassPrivate.
    std::uint32_t index = 0;
    if (!ue::SafeReadU32(obj + g.kClassPrivate - 4, index)) return false;

    const std::uintptr_t objArr = ue::ModuleBase() + g.kObjObjects;
    std::uintptr_t chunks = 0;
    std::uint32_t num = 0;
    if (!ue::SafeReadPtr(objArr, chunks) || !chunks) return false;
    if (!ue::SafeReadU32(objArr + g.kObjObjects_Num, num) || index >= num) return false;

    std::uintptr_t chunk = 0;
    if (!ue::SafeReadPtr(chunks + (static_cast<std::uintptr_t>(index / g.kChunkNumElems) * 8),
                         chunk) || !chunk)
        return false;
    std::uintptr_t registered = 0;
    return ue::SafeReadPtr(chunk + static_cast<std::uintptr_t>(index % g.kChunkNumElems)
                               * g.kFUObjectItemSize, registered)
        && registered == obj;
}

bool Live(const Widget& w) {
    if (!w.Obj || !w.Cls) return false;
    std::uintptr_t cls = 0;
    if (!ue::SafeReadPtr(w.Obj + Offsets().UObjectGlobals.kClassPrivate, cls) || cls != w.Cls)
        return false;
    return RegisteredInObjectArray(w.Obj);
}

// How far up the outer chain the target test looks. Four links reach the map
// name, which is where the live widget and its Blueprint template part company.
constexpr int kOuterChainDepth = 4;

// The UFUNCTIONs the movers dispatch, plus the class-default object the
// viewport-scale call needs a `this` for. Each is looked up once and kept: they
// live for the process, unlike the widgets, which a level change replaces.
void ResolveScriptFunctions() {
    if (!g_setRenderTranslationFn)
        g_setRenderTranslationFn = ue::FindLiveObject("Function", "SetRenderTranslation", "Widget");
    if (!g_getViewportScaleFn)
        g_getViewportScaleFn = ue::FindLiveObject("Function", "GetViewportScale", "WidgetLayoutLibrary");
    if (!g_widgetLayoutLibCdo)
        g_widgetLayoutLibCdo = ue::FindLiveObject("WidgetLayoutLibrary", "Default__WidgetLayoutLibrary", nullptr);
}

// One table walk collects every target; doing it per widget would walk 100k
// objects once each. Only objects the array still holds are visited, so a
// widget that has been destroyed cannot come back out of this.
void Collect() {
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    Widget found[kNumTargets];
    int matches[kNumTargets] = {};
    const std::size_t nameOff = Offsets().UObjectGlobals.kNamePrivate;
    ue::ForEachUObject([&](std::uintptr_t obj) -> bool {
        std::uint32_t id = 0;
        if (!ue::SafeReadU32(obj + nameOff, id)) return false;
        std::string name;
        for (std::size_t i = 0; i < kNumTargets; ++i) {
            if (g_nameIds[i] != 0) {
                if (id != g_nameIds[i]) continue;
            } else {
                if (name.empty()) name = ue::ResolveFName(id);
                if (name != kTargets[i].Name) continue;
            }
            if (!ue::ContainsCI(ue_vm::OuterChain(obj, kOuterChainDepth, "/"),
                                kTargets[i].Outer)) continue;
            std::uintptr_t cls = 0;
            if (ue::SafeReadPtr(obj + Offsets().UObjectGlobals.kClassPrivate, cls) && cls) {
                found[i] = {obj, cls};
                g_nameIds[i] = id;
                ++matches[i];
            }
            break;
        }
        return false;
    });

    QueryPerformanceCounter(&t1);
    g_lastWalkMs = freq.QuadPart
        ? static_cast<float>((t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart))
        : 0.0f;

    bool changed = false;
    for (std::size_t i = 0; i < kNumTargets; ++i)
        if (found[i].Obj != g_widgets[i].Obj) changed = true;

    ResolveScriptFunctions();

    if (changed) {
        // Both addresses, so a session log shows the moment a rebuilt HUD moved
        // a widget and what it moved to. matches says whether more than one
        // object answered to the name and outer, which would make the choice
        // below arbitrary.
        for (std::size_t i = 0; i < kNumTargets; ++i) {
            Log::Line("reticle: target %-20s 0x%llx -> 0x%llx  matches=%d%s", kTargets[i].Name,
                static_cast<unsigned long long>(g_widgets[i].Obj),
                static_cast<unsigned long long>(found[i].Obj), matches[i],
                found[i].Obj ? "" : "  (NOT FOUND - stays screen-fixed)");
        }
        Log::Line("reticle: setRenderTranslation=0x%llx dpiScale=%.3f walk=%.1fms",
            static_cast<unsigned long long>(g_setRenderTranslationFn), g_dpiScale, g_lastWalkMs);
    }

    for (std::size_t i = 0; i < kNumTargets; ++i) g_widgets[i] = found[i];
}

void RefreshDpiScale() {
    if (!g_getViewportScaleFn || !g_widgetLayoutLibCdo) return;
    // GetViewportScale finds the viewport through its world-context object, so
    // there is nothing to ask while the walk has not found a widget to pass.
    if (!g_widgets[0].Obj) return;
    struct { void* WorldContext; float Ret; char pad[12]; } p{};
    p.WorldContext = reinterpret_cast<void*>(g_widgets[0].Obj);
    if (!ue_vm::Dispatch(reinterpret_cast<void*>(g_widgetLayoutLibCdo),
                         reinterpret_cast<void*>(g_getViewportScaleFn), &p))
        return;
    // A zero or absurd scale means the call did not do what we think; keeping
    // the previous value is better than dividing the offset by nonsense.
    if (p.Ret > kMinPlausibleDpiScale && p.Ret < kMaxPlausibleDpiScale
        && p.Ret != g_dpiScale) {
        Log::Line("reticle: viewport DPI scale %.3f -> %.3f", g_dpiScale, p.Ret);
        g_dpiScale = p.Ret;
    }
}

void Push(Widget& w, double x, double y) {
    if (!Live(w)) return;
    // UE5 LWC: FVector2D is two doubles. The tail padding covers any trailing
    // parameter the signature carries that we are not setting.
    struct { double X; double Y; char pad[16]; } tr{};
    tr.X = x;
    tr.Y = y;
    if (ue_vm::Dispatch(reinterpret_cast<void*>(w.Obj),
                        reinterpret_cast<void*>(g_setRenderTranslationFn), &tr))
        return;

    // A dispatch that faults means the object stopped being what it was between
    // the liveness test and the call. Dropping it sends the next walk looking
    // for whatever replaced it; swallowing the fault would leave the reticle
    // pinned to a dead object with nothing in the log to say so.
    Log::Line("reticle: SetRenderTranslation faulted on 0x%llx - dropped, re-locating",
        static_cast<unsigned long long>(w.Obj));
    w = Widget{};
}

// Resolve the script VM on the first tick that can, and say where it landed.
bool VmReady() {
    if (!ue_vm::Ready()) return false;
    static bool s_announced = false;
    if (!s_announced) {
        s_announced = true;
        Log::Line("reticle: ProcessEvent at RVA 0x%08llx",
            static_cast<unsigned long long>(ue_vm::ProcessEventRva()));
    }
    return true;
}

// A level change rebuilds the HUD, and the widget that replaces the one we hold
// is a different object. That is what the liveness test is for, and the short
// retry interval then re-points within a couple of seconds - it also covers a
// target that is genuinely absent, since the prompt manager does not exist until
// the HUD is up. The walk runs on the backstop interval even when every target
// looks fine, because a widget that has been orphaned rather than destroyed
// reads as perfectly alive. Comparing FName ids rather than resolving ~180k
// names is what makes repeating the walk affordable; its cost is printed in the
// offset line, so it can be checked rather than trusted.
void MaybeRefreshTargets() {
    bool allLive = true;
    for (const Widget& w : g_widgets) if (!Live(w)) { allLive = false; break; }

    static std::uint64_t s_lastWalk = 0;
    const std::uint64_t now = GetTickCount64();
    if (now - s_lastWalk < (allLive ? kBackstopWalkMs : kRetryWalkMs)) return;
    s_lastWalk = now;
    Collect();
    RefreshDpiScale();
}

// The hook fires several times per frame but the offset only changes on the
// render caller, and every push is a script-VM dispatch. Skip the pass when
// nothing moved, but re-assert every kReassertEveryCalls so a game-side reset of
// RenderTransform cannot stick while the offset is static.
bool NeedsPush(double x, double y) {
    static double s_lastX = 0.0, s_lastY = 0.0;
    static bool s_written = false;
    static std::uint64_t s_calls = 0;
    const bool reassert = (s_calls++ % kReassertEveryCalls) == 0;
    if (s_written && !reassert && x == s_lastX && y == s_lastY) return false;
    s_lastX = x; s_lastY = y; s_written = true;
    return true;
}

}  // namespace

void Tick() {
    if (Offsets().UObjectGlobals.kObjObjects == 0) return;
    if (!VmReady()) return;

    MaybeRefreshTargets();
    if (!g_setRenderTranslationFn || !Live(g_widgets[0])) return;

    float dx = 0.0f, dy = 0.0f;
    const bool haveOffset = AimProjection::GetScreenOffset(dx, dy);
    // No valid offset means tracking is off, suppressed, or the aim is behind
    // the view. Park the widgets back at the centre rather than leaving them
    // stuck wherever the last tracked frame put them.
    const double slateX = haveOffset ? dx / g_dpiScale : 0.0;
    const double slateY = haveOffset ? dy / g_dpiScale : 0.0;
    if (!NeedsPush(slateX, slateY)) return;

    for (Widget& w : g_widgets) Push(w, slateX, slateY);

    static std::uint64_t s_lastOffsetLog = 0;
    static int s_offsetLines = 0;
    if (s_offsetLines >= kOffsetLogLines) return;
    const std::uint64_t now = GetTickCount64();
    if (s_offsetLines != 0 && now - s_lastOffsetLog < kOffsetLogMs) return;
    s_lastOffsetLog = now;
    ++s_offsetLines;
    Log::Line("reticle: offset px=(%.1f,%.1f) slate=(%.1f,%.1f) dpi=%.3f valid=%s walk=%.1fms",
        dx, dy, slateX, slateY, g_dpiScale, haveOffset ? "yes" : "no", g_lastWalkMs);
}

}  // namespace swtd_ht::ReticleMover
