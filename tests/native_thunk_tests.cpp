// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "builds/native_thunk.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
unsigned checks=0, failures=0;
void Check(bool result,const char* name) { ++checks; if(!result){++failures; std::printf("FAIL %s\n",name);} }
void Emit(Bytes& code,std::initializer_list<std::uint8_t> instruction) {code.insert(code.end(),instruction);}
void Word(Bytes& code,std::uint32_t value) {for(unsigned n=0;n<4;++n)code.push_back(static_cast<std::uint8_t>(value>>(8*n)));}
void AdvanceFrame(Bytes& code) {
    Emit(code,{0x48,0x8b,0x42,0x20});
    Emit(code,{0x45,0x33,0xd2});
    Emit(code,{0x48,0x85,0xc0});
    Emit(code,{0x41,0x0f,0x95,0xc2});
    Emit(code,{0x4c,0x03,0xd0});
    Emit(code,{0x4c,0x89,0x52,0x20});
}
Bytes ByteGetter(std::uint32_t offset) {
    Bytes code; AdvanceFrame(code);
    Emit(code,{0x0f,0xb6,0x89}); Word(code,offset);
    Emit(code,{0x41,0x88,0x08}); Emit(code,{0xc3}); return code;
}
Bytes StructGetter() {
    Bytes code;
    Emit(code,{0x56}); Emit(code,{0x48,0x83,0xec,0x40});
    Emit(code,{0x49,0x8b,0xf0}); AdvanceFrame(code);
    Emit(code,{0x48,0x8d,0x54,0x24,0x20});
    Emit(code,{0xe8}); Word(code,0x1000-static_cast<std::uint32_t>(code.size()+4));
    Emit(code,{0x0f,0x10,0x00}); Emit(code,{0x0f,0x11,0x06});
    Emit(code,{0xf2,0x0f,0x10,0x40,0x10}); Emit(code,{0xf2,0x0f,0x11,0x46,0x10});
    Emit(code,{0x48,0x83,0xc4,0x40}); Emit(code,{0x5e}); Emit(code,{0xc3}); return code;
}
bool Inspect(const Bytes& code,swtd_ht::builds::NativeThunk& out) {
    return swtd_ht::builds::InspectNativeThunk(code.data(),code.size(),out);
}
}
int main() {
    using swtd_ht::builds::NativeThunk;
    NativeThunk out;
    for(auto offset : {0x30u,0x1f0u,0xfffeu}) {
        auto code=ByteGetter(offset);
        Check(Inspect(code,out)&&out.memberOffset==offset&&out.outputBytes==1,"derive byte member with alternate scratch registers");
        for(std::size_t size=0;size<code.size();++size)
            Check(!swtd_ht::builds::InspectNativeThunk(code.data(),size,out)&&!out.outputBytes,"reject every truncated byte getter");
    }
    auto code=StructGetter();
    Check(Inspect(code,out)&&out.outputBytes==24&&out.callOffset==0x1000,"derive call and full 24-byte return through RSI");
    for(std::size_t size=0;size<code.size();++size)
        Check(!swtd_ht::builds::InspectNativeThunk(code.data(),size,out)&&!out.outputBytes,"reject every truncated struct getter");
    code=ByteGetter(0x20); Check(!Inspect(code,out),"reject UObject header field");
    code=ByteGetter(0x10000); Check(!Inspect(code,out),"reject out-of-range member");
    code=ByteGetter(0x200); code[3]=0x28; Check(!Inspect(code,out),"reject different frame field");
    code=ByteGetter(0x200); code[7]=0x90; Check(!Inspect(code,out),"reject invalid cursor predicate");
    code=ByteGetter(0x200); code[code.size()-2]=0x09; Check(!Inspect(code,out),"reject store to object instead of result");
    code=StructGetter(); code[code.size()-4]=0x38; Check(!Inspect(code,out),"reject unbalanced stack");
    code=StructGetter(); code[code.size()-8]=8; Check(!Inspect(code,out),"reject overlapping result writes");
    code={0xe9,0,0,0,0,0xc3}; Check(!Inspect(code,out),"reject branch outside supported straight-line thunk");
    std::printf("%u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
