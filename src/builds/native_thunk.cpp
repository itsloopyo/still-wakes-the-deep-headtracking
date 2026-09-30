// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "native_thunk.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <hde64.h>

namespace swtd_ht::builds {
namespace {
enum class Origin { Unknown, Object, Frame, Output, Stack, Cursor, Boolean, Zero, Member, Result };
struct Value {
    Origin origin = Origin::Unknown;
    std::int64_t offset = 0;
    unsigned width = 0;
};
}

bool InspectNativeThunk(const std::uint8_t* bytes, std::size_t size, NativeThunk& out) {
    out = {};
    std::array<Value,16> registers{}, vectors{};
    registers[1] = {Origin::Object};
    registers[2] = {Origin::Frame};
    registers[8] = {Origin::Output};
    registers[4] = {Origin::Stack};
    std::map<std::int64_t,Value> stack;
    unsigned cursorWrites = 0, calls = 0;
    bool cursorTested = false;
    std::uint32_t outputMask = 0;
    NativeThunk result;
    auto load = [&](Value address, unsigned width) -> Value {
        if (address.origin == Origin::Stack) {
            const auto it = stack.find(address.offset);
            return it == stack.end() ? Value{} : it->second;
        }
        if (address.origin == Origin::Frame && address.offset == 0x20 && width == 8)
            return {Origin::Cursor,0,8};
        if (address.origin == Origin::Object && width == 1 && address.offset >= 0x28 && address.offset < 0x10000)
            return {Origin::Member,address.offset,1};
        if (address.origin == Origin::Result && address.offset >= 0 && address.offset + width <= 24)
            return {Origin::Result,address.offset,width};
        return {};
    };
    auto store = [&](Value address, Value value, unsigned width) {
        if (address.origin == Origin::Stack) { stack[address.offset] = value; return true; }
        if (address.origin == Origin::Frame && address.offset == 0x20 && width == 8 &&
            value.origin == Origin::Cursor && value.offset == 1) { ++cursorWrites; return true; }
        if (address.origin != Origin::Output || address.offset < 0 || address.offset + width > 24) return false;
        if (value.origin == Origin::Member && width == 1 && address.offset == 0 && !calls) {
            result.memberOffset = static_cast<std::uint32_t>(value.offset);
        } else if (value.origin != Origin::Result || !calls || value.offset != address.offset || value.width != width) {
            return false;
        }
        const auto mask = ((1u << width) - 1u) << address.offset;
        if (outputMask & mask) return false;
        outputMask |= mask;
        return true;
    };
    for (std::size_t at = 0; at < size && at < 128;) {
        std::uint8_t buffer[16]{};
        std::memcpy(buffer,bytes+at,std::min(sizeof(buffer),size-at));
        hde64s i{};
        const auto length = hde64_disasm(buffer,&i);
        if (!length || (i.flags & F_ERROR) || length > size-at) return false;
        if (i.p_seg || i.p_67 || i.p_lock || (i.p_66 && i.opcode != 0x0f)) return false;
        const auto next = at + length;
        const unsigned reg = i.modrm_reg + 8*i.rex_r;
        const unsigned rm = i.modrm_rm + 8*i.rex_b;
        const unsigned width = i.rex_w ? 8 : 4;
        const auto displacement = (i.flags & F_DISP8) ? static_cast<std::int32_t>(static_cast<std::int8_t>(i.disp.disp8)) : static_cast<std::int32_t>(i.disp.disp32);
        Value address;
        if ((i.flags & F_MODRM) && i.modrm_mod != 3) {
            unsigned base = rm;
            if (i.flags & F_SIB) {
                if (i.sib_index != 4 || i.rex_x || (i.modrm_mod == 0 && i.sib_base == 5)) return false;
                base = i.sib_base + 8*i.rex_b;
            } else if (i.modrm_mod == 0 && i.modrm_rm == 5) return false;
            address = registers[base];
            address.offset += displacement;
        }
        if (i.opcode == 0xc3) {
            if (registers[4].origin != Origin::Stack || registers[4].offset != 0 || cursorWrites != 1) return false;
            if (!calls && result.memberOffset && outputMask == 1) result.outputBytes = 1;
            else if (calls == 1 && outputMask == 0xffffff) result.outputBytes = 24;
            else return false;
            out = result;
            return true;
        } else if (i.opcode >= 0x50 && i.opcode <= 0x57) {
            registers[4].offset -= 8;
            stack[registers[4].offset] = registers[(i.opcode-0x50)+8*i.rex_b];
        } else if (i.opcode >= 0x58 && i.opcode <= 0x5f) {
            registers[(i.opcode-0x58)+8*i.rex_b] = load(registers[4],8);
            registers[4].offset += 8;
        } else if (i.opcode == 0x83 && i.modrm_mod == 3 && rm == 4 && i.rex_w && (i.modrm_reg == 0 || i.modrm_reg == 5)) {
            const auto immediate = static_cast<std::int8_t>(i.imm.imm8);
            registers[4].offset += i.modrm_reg == 0 ? immediate : -immediate;
        } else if (i.opcode == 0x8b || i.opcode == 0x8a) {
            registers[reg] = i.modrm_mod == 3 ? registers[rm] : load(address,i.opcode == 0x8a ? 1 : width);
            if (registers[reg].origin == Origin::Unknown) return false;
            if (i.modrm_mod == 3 && !i.rex_w && registers[reg].origin != Origin::Member) return false;
        } else if (i.opcode == 0x89 || i.opcode == 0x88) {
            if (i.modrm_mod == 3) {
                if (!i.rex_w && registers[reg].origin != Origin::Member) return false;
                registers[rm] = registers[reg];
            }
            else if (!store(address,registers[reg],i.opcode == 0x88 ? 1 : width)) return false;
        } else if (i.opcode == 0x8d && i.rex_w && i.modrm_mod != 3) {
            registers[reg] = address;
        } else if (i.opcode == 0x33 && i.modrm_mod == 3 && reg == rm) {
            registers[reg] = {Origin::Zero};
            cursorTested = false;
        } else if (i.opcode == 0x85 && i.modrm_mod == 3 && reg == rm && registers[reg].origin == Origin::Cursor) {
            cursorTested = true;
        } else if (i.opcode == 0x03 && i.modrm_mod == 3 && registers[reg].origin == Origin::Boolean && registers[rm].origin == Origin::Cursor) {
            registers[reg] = {Origin::Cursor,1,8};
        } else if (i.opcode == 0x0f && i.opcode2 == 0x95 && cursorTested && i.modrm_mod == 3 && registers[rm].origin == Origin::Zero) {
            registers[rm] = {Origin::Boolean,0,1};
        } else if (i.opcode == 0x0f && i.opcode2 == 0xb6 && i.modrm_mod != 3) {
            registers[reg] = load(address,1);
            if (registers[reg].origin != Origin::Member) return false;
        } else if (i.opcode == 0x0f && (i.opcode2 == 0x10 || i.opcode2 == 0x11) && i.modrm_mod != 3) {
            if (i.p_rep && i.p_rep != 0xf2) return false;
            const unsigned bytesCopied = i.p_rep == 0xf2 ? 8 : 16;
            if (i.opcode2 == 0x10) vectors[reg] = load(address,bytesCopied);
            else if (!store(address,vectors[reg],bytesCopied)) return false;
        } else if (i.opcode == 0xe8) {
            if (++calls != 1 || registers[1].origin != Origin::Object || registers[1].offset != 0 ||
                registers[2].origin != Origin::Stack || registers[2].offset < registers[4].offset ||
                registers[2].offset + 24 > 0 || cursorWrites != 1) return false;
            result.callOffset = static_cast<std::int64_t>(next) + static_cast<std::int32_t>(i.imm.imm32);
            for (unsigned r : {0u,1u,2u,8u,9u,10u,11u}) registers[r] = {};
            registers[0] = {Origin::Result};
        } else return false;
        at = next;
    }
    return false;
}
}
