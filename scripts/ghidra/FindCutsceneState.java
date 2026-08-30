// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Locate the game's own "am I in a cutscene" state so the head-tracking hook
// can suppress while one plays.
//
// Still Wakes the Deep's Habitat code carries a whole cutscene layer in its
// reflection data (GetCutsceneMode / SetCutsceneMode / CutsceneModeContextStack
// / HabitatControllerCutsceneModeChanged), so the flag is a member of a game
// class rather than APlayerController::bCinematicMode. Two routes in, both
// already proven on this binary:
//
//   1. UFUNCTION thunks register as { const char* Name, FNativeFuncPtr } pairs,
//      so a .rdata pointer to the name string is followed by the exec stub.
//      Decompiling execGetCutsceneMode spells out which member it reads.
//   2. FProperty params structs name the property and carry either a SetBit
//      stub (bools - the OR immediate is offset+mask) or a plain Offset field
//      (everything else, e.g. the TArray context stack).
//
// Output: .lab/ghidra/cutscene_state.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.PrintWriter;
import java.util.*;

public class FindCutsceneState extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;
    DecompInterface di;

    Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    boolean inText(long v) {
        if (v == 0) return false;
        try {
            MemoryBlock b = mem.getBlock(addr(v));
            return b != null && b.isExecute();
        } catch (Exception e) { return false; }
    }

    long qword(Address a) throws Exception {
        byte[] b = new byte[8];
        mem.getBytes(a, b);
        long q = 0;
        for (int k = 7; k >= 0; k--) q = (q << 8) | (b[k] & 0xffL);
        return q;
    }

    List<Address> findPointersTo(long v) throws Exception {
        List<Address> hits = new ArrayList<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            long start = b.getStart().getOffset(), end = b.getEnd().getOffset();
            int chunk = 1 << 20;
            byte[] buf = new byte[chunk + 8];
            for (long p = start; p <= end; p += chunk) {
                int len = (int) Math.min(chunk + 8L, end - p + 1);
                if (len <= 8) break;
                try { mem.getBytes(addr(p), buf, 0, len); } catch (Exception e) { continue; }
                for (int i = 0; i + 8 <= len; i += 8) {
                    long q = 0;
                    for (int k = 7; k >= 0; k--) q = (q << 8) | (buf[i + k] & 0xffL);
                    if (q == v) hits.add(addr(p + i));
                }
                if (monitor.isCancelled()) return hits;
            }
        }
        return hits;
    }

    Map<String, List<Address>> stringIndex(Set<String> wanted) {
        Map<String, List<Address>> found = new LinkedHashMap<>();
        DataIterator it = listing.getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            try {
                Object v = d.getValue();
                if (!(v instanceof String)) continue;
                String s = ((String) v).trim();
                if (!wanted.contains(s)) continue;
                found.computeIfAbsent(s, k -> new ArrayList<>()).add(d.getAddress());
            } catch (Exception e) { }
            if (monitor.isCancelled()) break;
        }
        return found;
    }

    void decomp(long va, int maxLines) {
        Function f = fm.getFunctionContaining(addr(va));
        if (f == null) { out.println("        <no function>"); return; }
        DecompileResults res = di.decompileFunction(f, 90, monitor);
        if (res == null || res.getDecompiledFunction() == null) {
            out.println("        <decompile failed>");
            return;
        }
        String[] lines = res.getDecompiledFunction().getC().split("\n");
        for (int i = 0; i < Math.min(lines.length, maxLines); i++) out.println("        | " + lines[i]);
        if (lines.length > maxLines) out.println("        | ... (" + (lines.length - maxLines) + " more)");
    }

    void disasmAt(long va, int count) {
        Address a = addr(va);
        Instruction ins = listing.getInstructionAt(a);
        if (ins == null) {
            try { disassemble(a); } catch (Exception e) { }
            ins = listing.getInstructionAt(a);
        }
        for (int i = 0; i < count && ins != null; i++) {
            out.printf("        0x%08x  %s%n", ins.getAddress().getOffset() - BASE, ins.toString());
            ins = ins.getNext();
        }
    }

    void hexdump(Address p, int bytes) {
        byte[] b = new byte[bytes];
        try { mem.getBytes(p, b); } catch (Exception e) { out.println("     <unreadable>"); return; }
        for (int i = 0; i < bytes; i += 16) {
            StringBuilder sb = new StringBuilder();
            sb.append(String.format("     +0x%02x ", i));
            for (int k = 0; k < 16 && i + k < bytes; k++) sb.append(String.format("%02x ", b[i + k]));
            out.println(sb.toString());
        }
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/cutscene_state.txt");
        di = new DecompInterface();
        di.openProgram(currentProgram);

        String[] names = {
            "GetCutsceneMode", "SetCutsceneMode", "InCutsceneMode",
            "CutsceneModeContextStack", "OnCutsceneModeChanged",
            "GetIsInCutscene", "SetIsInCutscene", "bIsInCutscene", "bInCutscene",
            "IsCutsceneSkippable", "ShouldShowCutsceneBars",
            "HabitatPlayerControllerInGame", "HabitatControllerInGame",
        };
        Set<String> wanted = new LinkedHashSet<>(Arrays.asList(names));
        Map<String, List<Address>> idx = stringIndex(wanted);

        for (String n : names) {
            out.println("================ " + n + " ================");
            List<Address> sas = idx.get(n);
            if (sas == null) { out.println("  string NOT FOUND"); out.flush(); continue; }
            for (Address sa : sas) {
                out.printf("  string @0x%08x%n", sa.getOffset() - BASE);
                List<Address> ptrs = findPointersTo(sa.getOffset());
                out.println("  .rdata pointers: " + ptrs.size());
                int shown = 0;
                for (Address p : ptrs) {
                    out.printf("   ptr @0x%08x%n", p.getOffset() - BASE);
                    hexdump(p, 0x50);
                    for (int off = 8; off <= 0x48; off += 8) {
                        long q;
                        try { q = qword(p.add(off)); } catch (Exception e) { continue; }
                        if (!inText(q)) continue;
                        Function f = fm.getFunctionContaining(addr(q));
                        out.printf("     +0x%02x -> text 0x%08x size=%s%n", off, q - BASE,
                            f == null ? "?" : String.valueOf(f.getBody().getNumAddresses()));
                        disasmAt(q, 10);
                        if (off <= 0x10 || off == 0x38) decomp(q, 70);
                    }
                    if (++shown >= 4) { out.println("   ... more pointers omitted"); break; }
                }
                out.flush();
            }
            out.println();
        }
        di.dispose();
        out.close();
        println("FindCutsceneState done");
    }
}
