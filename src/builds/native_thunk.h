// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cstddef>
#include <cstdint>

namespace swtd_ht::builds {
struct NativeThunk {
    std::uint32_t memberOffset = 0;
    std::int64_t callOffset = 0;
    std::uint32_t outputBytes = 0;
};
bool InspectNativeThunk(const std::uint8_t* bytes, std::size_t size, NativeThunk& out);
}
