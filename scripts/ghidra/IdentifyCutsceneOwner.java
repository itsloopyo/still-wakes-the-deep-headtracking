// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Attribute the cutscene-state members found by FindCutsceneState.java to the
// class that owns them.
//
// GetCutsceneMode's exec thunk reads a byte at this+0x8cd, which is only usable
// if we know the object it is read from is the APlayerController our
// GetPlayerViewPoint hook already holds. Two ways to settle that, both done
// here:
//   1. Walk the FNameNativePtrPair array the thunk lives in and print every
//      { name, thunk } entry. A class's whole UFUNCTION set names it far more
//      reliably than any single symbol.
//   2. Follow code xrefs to that array to StaticRegisterNatives<Class> and
//      decompile it, plus any wide (TEXT()) class-name strings it reaches.
//
// Output: .lab/ghidra/cutscene_owner.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.PrintWriter;
import java.util.*;

public class IdentifyCutsceneOwner extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;
    DecompInterface di;

    Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    long qword(Address a) throws Exception {
        byte[] b = new byte[8];
        mem.getBytes(a, b);
        long q = 0;
        for (int k = 7; k >= 0; k--) q = (q << 8) | (b[k] & 0xffL);
        return q;
    }

    boolean inText(long v) {
        if (v == 0) return false;
        try { MemoryBlock b = mem.getBlock(addr(v)); return b != null && b.isExecute(); }
        catch (Exception e) { return false; }
    }

    String cstr(long va, int max) {
        if (va == 0) return null;
        try {
            MemoryBlock b = mem.getBlock(addr(va));
            if (b == null || !b.isInitialized() || b.isExecute()) return null;
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < max; i++) {
                byte c = mem.getByte(addr(va + i));
                if (c == 0) break;
                if (c < 0x20 || c > 0x7e) return null;
                sb.append((char) c);
            }
            return sb.length() >= 3 ? sb.toString() : null;
        } catch (Exception e) { return null; }
    }

    void decomp(long va, int maxLines) {
        Function f = fm.getFunctionContaining(addr(va));
        if (f == null) { out.println("    <no function>"); return; }
        DecompileResults res = di.decompileFunction(f, 90, monitor);
        if (res == null || res.getDecompiledFunction() == null) {
            out.println("    <decompile failed>"); return;
        }
        String[] lines = res.getDecompiledFunction().getC().split("\n");
        for (int i = 0; i < Math.min(lines.length, maxLines); i++) out.println("    | " + lines[i]);
    }

    // Print a FNameNativePtrPair array: pairs of { const char* name, void* thunk }.
    // Walks backwards to the array start first, since the located entry is
    // rarely the first one.
    void dumpPairArray(long entryVa) throws Exception {
        long start = entryVa;
        for (int i = 0; i < 512; i++) {
            long prev = start - 0x10;
            String nm = cstr(qword(addr(prev)), 96);
            if (nm == null || !inText(qword(addr(prev + 8)))) break;
            start = prev;
        }
        out.printf("  pair array starts @0x%08x%n", start - BASE);
        for (int i = 0; i < 512; i++) {
            long p = start + i * 0x10L;
            String nm = cstr(qword(addr(p)), 96);
            long thunk = qword(addr(p + 8));
            if (nm == null || !inText(thunk)) break;
            out.printf("   [%3d] %-44s thunk 0x%08x%s%n", i, nm, thunk - BASE,
                p == entryVa ? "   <== located here" : "");
        }
    }

    void xrefsTo(long va) {
        out.printf("  code xrefs to 0x%08x:%n", va - BASE);
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(addr(va));
        int n = 0;
        while (it.hasNext() && n < 8) {
            Reference r = it.next();
            Address from = r.getFromAddress();
            Function f = fm.getFunctionContaining(from);
            out.printf("   from 0x%08x (%s)%n", from.getOffset() - BASE,
                f == null ? "no fn" : f.getName());
            if (f != null) { decomp(f.getEntryPoint().getOffset(), 60); n++; }
        }
        if (n == 0) out.println("   <none>");
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/cutscene_owner.txt");
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // RVAs of the FNameNativePtrPair entries FindCutsceneState.java landed on.
        long[] pairEntries = { 0x084f23d0L, 0x080fcd98L };
        for (long rva : pairEntries) {
            out.printf("================ pair entry @0x%08x ================%n", rva);
            dumpPairArray(BASE + rva);
            out.println();
            xrefsTo(BASE + rva);
            out.println();
            out.flush();
        }

        // The exec thunks themselves, disassembled through their caller chain:
        // which member each reads, and who calls the underlying getter.
        long[] thunks = { 0x04e96e20L, 0x04e36d05L };
        for (long rva : thunks) {
            out.printf("================ thunk 0x%08x ================%n", rva);
            decomp(BASE + rva, 40);
            out.println();
            out.flush();
        }
        di.dispose();
        out.close();
        println("IdentifyCutsceneOwner done");
    }
}
