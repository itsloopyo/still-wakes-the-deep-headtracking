// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ue_vm.h"

#include <windows.h>

#include "builds/build_registry.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace swtd_ht::ue_vm {

namespace {

namespace ue = ::cameraunlock::unreal;

using ProcessEvent_t = void(__fastcall*)(void* self, void* function, void* params);
ProcessEvent_t g_processEvent = nullptr;

// Non-unwinding, so __try is legal here while callers hold objects with
// destructors.
bool Invoke(void* self, void* function, void* params) {
    __try {
        g_processEvent(self, function, params);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool Ready() {
    if (g_processEvent) return true;
    const std::uintptr_t rva = Offsets().kProcessEventRva;
    if (rva == 0) return false;
    g_processEvent = reinterpret_cast<ProcessEvent_t>(ue::ModuleBase() + rva);
    return true;
}

std::uintptr_t ProcessEventRva() { return Offsets().kProcessEventRva; }

bool Dispatch(void* self, void* function, void* params) {
    if (!Ready()) return false;
    return Invoke(self, function, params);
}

std::string OuterChain(std::uintptr_t obj, int depth, const char* separator) {
    std::string out;
    std::uintptr_t cur = obj;
    for (int i = 0; i < depth; ++i) {
        std::uintptr_t outer = 0;
        if (!ue::SafeReadPtr(cur + Offsets().UObjectGlobals.kOuterPrivate, outer) || !outer)
            break;
        out += separator;
        out += ue::ObjectName(outer);
        cur = outer;
    }
    return out;
}

bool ReadObjectTable(ObjectTable& out) {
    const auto& g = Offsets().UObjectGlobals;
    // Both are divisors in ObjectAt().
    if (g.kObjObjects == 0 || g.kChunkNumElems == 0 || g.kFUObjectItemSize == 0) return false;
    const std::uintptr_t arr = ue::ModuleBase() + g.kObjObjects;
    if (!ue::SafeReadPtr(arr, out.Chunks) || !out.Chunks) return false;
    if (!ue::SafeReadU32(arr + g.kObjObjects_Num, out.Num)) return false;
    // Core's ForEachUObject bound: a count past it is not an object table.
    return out.Num != 0 && out.Num <= 0x4000000;
}

std::uintptr_t ObjectAt(const ObjectTable& table, std::uint32_t index) {
    const auto& g = Offsets().UObjectGlobals;
    std::uintptr_t chunk = 0;
    if (!ue::SafeReadPtr(table.Chunks + static_cast<std::uintptr_t>(index / g.kChunkNumElems) * 8,
                         chunk) || !chunk)
        return 0;
    std::uintptr_t obj = 0;
    if (!ue::SafeReadPtr(chunk + static_cast<std::uintptr_t>(index % g.kChunkNumElems)
                             * g.kFUObjectItemSize, obj))
        return 0;
    return obj;
}

bool IsRegistered(std::uintptr_t obj) {
    if (!obj) return false;
    ObjectTable table;
    if (!ReadObjectTable(table)) return false;
    // UObjectBase packs InternalIndex immediately before ClassPrivate.
    std::uint32_t index = 0;
    if (!ue::SafeReadU32(obj + Offsets().UObjectGlobals.kClassPrivate - 4, index)) return false;
    return index < table.Num && ObjectAt(table, index) == obj;
}

double NowMs() {
    static const double s_ticksPerMs = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / s_ticksPerMs;
}

}  // namespace swtd_ht::ue_vm
