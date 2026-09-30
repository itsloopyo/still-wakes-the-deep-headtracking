// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_registry.h"
#include "runtime_discovery.h"
#include "logging.h"

#include <vector>

namespace swtd_ht::builds {
extern const BuildProfile kSteamProfile_20240618;
namespace {
const BuildProfile* g_active = nullptr;
BuildProfile g_discovered{};
std::uint32_t g_viewSlot = 0;
}

MatchResult SelectProfile(HMODULE host) {
    g_active = nullptr;
    g_viewSlot = 0;
    PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(host, running)) {
        Log::Line("build-check: failed to read PE header from host module");
        return MatchResult::ReadFailed;
    }
    Log::Line("build-check: running ts=0x%08x size=0x%08x csum=0x%08x",
              running.TimeDateStamp, running.SizeOfImage, running.CheckSum);
    const auto* known = running.Matches(kSteamProfile_20240618.Fingerprint)
        ? &kSteamProfile_20240618 : nullptr;
    std::vector<std::uint8_t> image(running.SizeOfImage);
    SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), host, image.data(), image.size(), &copied) ||
        copied != image.size()) {
        Log::Line("discovery: could not snapshot the executable: Win32 error %lu", GetLastError());
        return MatchResult::DiscoveryFailed;
    }
    std::string reason;
    g_discovered = {"runtime-discovered", running, {}};
    if (!DiscoverOffsets({image.data(), image.size(), reinterpret_cast<std::uintptr_t>(host)},
                         g_discovered.Offsets, reason, g_viewSlot)) {
        Log::Line("discovery: %s", reason.c_str());
        if (!known) return MatchResult::DiscoveryFailed;
        g_active = known;
        g_viewSlot = 0;
        Log::Line("build-check: using exact historical profile %s", known->Name);
        return MatchResult::Matched;
    }
    const auto& found = g_discovered.Offsets;
    if (known) {
        const auto& expected = known->Offsets;
        if (found.kGetPlayerViewPointRva != expected.kGetPlayerViewPointRva ||
            found.kKnownCallerRvas[0] != expected.kKnownCallerRvas[0] ||
            found.kProcessEventRva != expected.kProcessEventRva ||
            found.UObjectGlobals.kObjObjects != expected.UObjectGlobals.kObjObjects ||
            found.UObjectGlobals.kFNamePool != expected.UObjectGlobals.kFNamePool ||
            found.kDefaultInjectMode != expected.kDefaultInjectMode ||
            found.kShowMouseCursorOffset != expected.kShowMouseCursorOffset ||
            found.kShowMouseCursorMask != expected.kShowMouseCursorMask ||
            found.kCutsceneModeOffset != expected.kCutsceneModeOffset ||
            found.kGetTargetRotationRva != expected.kGetTargetRotationRva ||
            found.kTorchArmTargetRotationRetRva != expected.kTorchArmTargetRotationRetRva ||
            found.kSceneComponentAttachParentOffset != expected.kSceneComponentAttachParentOffset ||
            found.MinimalViewInfoLayout.kFovOffset != expected.MinimalViewInfoLayout.kFovOffset ||
            found.MinimalViewInfoLayout.kRotationStride != expected.MinimalViewInfoLayout.kRotationStride ||
            found.UObjectGlobals.kObjObjects_Num != expected.UObjectGlobals.kObjObjects_Num ||
            found.UObjectGlobals.kFUObjectItemSize != expected.UObjectGlobals.kFUObjectItemSize ||
            found.UObjectGlobals.kChunkNumElems != expected.UObjectGlobals.kChunkNumElems ||
            found.UObjectGlobals.kFNamePoolBlocks != expected.UObjectGlobals.kFNamePoolBlocks ||
            found.UObjectGlobals.kClassPrivate != expected.UObjectGlobals.kClassPrivate ||
            found.UObjectGlobals.kNamePrivate != expected.UObjectGlobals.kNamePrivate ||
            found.UObjectGlobals.kOuterPrivate != expected.UObjectGlobals.kOuterPrivate) {
            Log::Line("discovery: resolved addresses disagree with exact historical profile %s", known->Name);
            return MatchResult::DiscoveryFailed;
        }
    }
    g_active = &g_discovered;
    Log::Line("discovery: view=0x%08llx render=0x%08llx event=0x%08llx objects=0x%08llx "
              "names=0x%08llx slot=0x%x; awaiting live layout validation",
              static_cast<unsigned long long>(found.kGetPlayerViewPointRva),
              static_cast<unsigned long long>(found.kKnownCallerRvas[0]),
              static_cast<unsigned long long>(found.kProcessEventRva),
              static_cast<unsigned long long>(found.UObjectGlobals.kObjObjects),
              static_cast<unsigned long long>(found.UObjectGlobals.kFNamePool), g_viewSlot);
    return MatchResult::Matched;
}

const BuildProfile& ActiveProfile() { return *g_active; }
bool UsesRuntimeDiscovery() { return g_active == &g_discovered; }
std::uint32_t RuntimeViewSlot() { return g_viewSlot; }
}
