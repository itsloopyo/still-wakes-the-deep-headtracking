// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Dump the reflected property layout of the player-controller class that owns
// GetCutsceneMode, to confirm the byte its exec thunk reads (this+0x8cd) really
// is a member of the APlayerController the GetPlayerViewPoint hook holds.
//
// Route: StaticRegisterNatives<Class> calls the class getter, the getter builds
// the UClass from an FClassParams that points at a PropPointers array, and each
// entry there is an FPropertyParams whose name is at +0x00 and whose byte
// offset within the class is the uint16 at +0x32. Printing the whole list puts
// the cutscene members next to their neighbours, which is the cross-check that
// the offsets belong to one class rather than three coincidences.
//
// Output: .lab/ghidra/controller_class.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.*;

public class DumpControllerClass extends GhidraScript {
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

    int u16(Address a) throws Exception {
        return (mem.getByte(a) & 0xff) | ((mem.getByte(a.add(1)) & 0xff) << 8);
    }

    boolean inData(long v) {
        if (v == 0) return false;
        try { MemoryBlock b = mem.getBlock(addr(v)); return b != null && b.isInitialized() && !b.isExecute(); }
        catch (Exception e) { return false; }
    }

    String cstr(long va, int max) {
        if (!inData(va)) return null;
        try {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < max; i++) {
                byte c = mem.getByte(addr(va + i));
                if (c == 0) break;
                if (c < 0x20 || c > 0x7e) return null;
                sb.append((char) c);
            }
            return sb.length() >= 2 ? sb.toString() : null;
        } catch (Exception e) { return null; }
    }

    String wstr(long va, int max) {
        if (!inData(va)) return null;
        try {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < max; i++) {
                int c = (mem.getByte(addr(va + i * 2L)) & 0xff)
                      | ((mem.getByte(addr(va + i * 2L + 1)) & 0xff) << 8);
                if (c == 0) break;
                if (c < 0x20 || c > 0x7e) return null;
                sb.append((char) c);
            }
            return sb.length() >= 3 ? sb.toString() : null;
        } catch (Exception e) { return null; }
    }

    // An FPropertyParams: name string at +0x00, ArrayDim at +0x30, byte offset
    // within the owning class at +0x32.
    boolean dumpPropParams(long p, int idx) throws Exception {
        String name = cstr(qword(addr(p)), 96);
        if (name == null) return false;
        int arrayDim = u16(addr(p + 0x30));
        int off = u16(addr(p + 0x32));
        int genFlags = (int) (qword(addr(p + 0x18)) & 0xffffffffL);
        out.printf("   [%3d] +0x%04x  %-46s (arrayDim=%d genFlags=0x%x) params@0x%08x%n",
            idx, off, name, arrayDim, genFlags, p - BASE);
        return true;
    }

    // Given any .rdata address, treat it as an array of pointers to
    // FPropertyParams and print every entry that parses.
    int tryDumpPropArray(long arr) throws Exception {
        int n = 0;
        for (int i = 0; i < 600; i++) {
            long p;
            try { p = qword(addr(arr + i * 8L)); } catch (Exception e) { break; }
            if (!inData(p)) break;
            if (!dumpPropParams(p, i)) break;
            n++;
        }
        return n;
    }

    void printFn(long va, int maxIns) {
        Address a = addr(va);
        Instruction ins = listing.getInstructionAt(a);
        if (ins == null) { try { disassemble(a); } catch (Exception e) { } ins = listing.getInstructionAt(a); }
        for (int i = 0; i < maxIns && ins != null; i++) {
            out.printf("   0x%08x  %s%n", ins.getAddress().getOffset() - BASE, ins.toString());
            ins = ins.getNext();
        }
    }

    Set<Long> dataRefsOf(long fnVa, int maxIns) {
        LinkedHashSet<Long> refs = new LinkedHashSet<>();
        Address a = addr(fnVa);
        Instruction ins = listing.getInstructionAt(a);
        if (ins == null) { try { disassemble(a); } catch (Exception e) { } ins = listing.getInstructionAt(a); }
        for (int i = 0; i < maxIns && ins != null; i++) {
            for (Reference r : ins.getReferencesFrom()) {
                Address t = r.getToAddress();
                if (t != null && inData(t.getOffset())) refs.add(t.getOffset());
            }
            ins = ins.getNext();
        }
        return refs;
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/controller_class.txt");
        di = new DecompInterface();
        di.openProgram(currentProgram);

        // The class getter called by StaticRegisterNatives for the class that
        // owns GetCutsceneMode (FUN_144e96b90 calls it, then registers the
        // 9-entry native pair array).
        long getterRva = 0x04e96a00L;
        out.printf("================ class getter 0x%08x ================%n", getterRva);
        printFn(BASE + getterRva, 40);
        out.println();

        // Everything in .rdata the getter and its neighbours reach, unpacked:
        // the ClassParams, and through it the property array.
        Set<Long> refs = new LinkedHashSet<>();
        refs.addAll(dataRefsOf(BASE + getterRva, 60));
        for (long r : new ArrayList<>(refs)) {
            out.printf("---- .rdata 0x%08x ----%n", r - BASE);
            String s = cstr(r, 120);
            if (s != null) out.println("   cstr: " + s);
            String w = wstr(r, 120);
            if (w != null) out.println("   wstr: " + w);
            for (int off = 0; off <= 0x80; off += 8) {
                long q;
                try { q = qword(addr(r + off)); } catch (Exception e) { continue; }
                if (!inData(q)) continue;
                String cs = cstr(q, 96);
                String ws = wstr(q, 96);
                out.printf("   +0x%02x -> 0x%08x%s%s%n", off, q - BASE,
                    cs == null ? "" : ("  cstr='" + cs + "'"),
                    ws == null ? "" : ("  wstr='" + ws + "'"));
                // Property pointer array?
                long first;
                try { first = qword(addr(q)); } catch (Exception e) { continue; }
                if (inData(first) && cstr(qword(addr(first)), 96) != null) {
                    out.println("     looks like a PropPointers array:");
                    int n = tryDumpPropArray(q);
                    out.println("     (" + n + " properties)");
                }
            }
            out.flush();
        }

        di.dispose();
        out.close();
        println("DumpControllerClass done");
    }
}
