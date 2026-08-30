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

}  // namespace swtd_ht::ue_vm
