// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Recover the UObject/FName globals the UMG widget mover needs.
//
// The game folder still carries a UE4SS log from an earlier session, which
// printed these as RUNTIME VAs under an unknown ASLR base:
//   GUObjectArray 0x7ff79d2ab0d0   GNatives  0x7ff79d2a9b70
//   FName::ToString 0x7ff794b1f9b0 ProcessEvent 0x7ff794cf2100
//   GMalloc 0x7ff79d1bc310         FName::FName 0x7ff794b011f0
//   StaticConstructObject 0x7ff794d1e3f0  GameEngineTick 0x7ff79706b500
// One anchor recovers the base and therefore all eight.
//
// GNatives is the anchor because it is structurally unmistakable: the Blueprint
// VM's dispatch table, 256 consecutive qwords that all point into .text. Find
// that run, and base = 0x7ff79d2a9b70 - itsRva. Every other logged VA is then
// checked against that base - a function VA must land on a .text function start
// and a data VA in initialised non-executable memory - so eight independent
// values agreeing is what makes the base trustworthy rather than a guess.
// Output: .lab/ghidra/uobject_globals.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.PrintWriter;
import java.util.*;

public class FindUObjectGlobals extends GhidraScript {
    long BASE, SIZE = 0x0a5e3000L;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;

    Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    boolean inText(long v) {
        if (v < BASE || v >= BASE + SIZE) return false;
        MemoryBlock b = mem.getBlock(addr(v));
        return b != null && b.isExecute();
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/uobject_globals.txt");

        final int kMinRun = 200;

        out.println("## candidate GNatives runs (>= " + kMinRun + " consecutive .text pointers)");
        List<long[]> runs = new ArrayList<>();   // startRva, length
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isExecute()) continue;
            long start = b.getStart().getOffset(), end = b.getEnd().getOffset();
            long runStart = -1;
            int runLen = 0;
            for (long p = start; p + 8 <= end; p += 8) {
                long q;
                try {
                    byte[] qb = new byte[8];
                    mem.getBytes(addr(p), qb);
                    q = 0;
                    for (int k = 7; k >= 0; k--) q = (q << 8) | (qb[k] & 0xffL);
                } catch (Exception e) { q = 0; }
                if (inText(q)) {
                    if (runStart < 0) { runStart = p; runLen = 0; }
                    runLen++;
                } else {
                    if (runStart >= 0 && runLen >= kMinRun) {
                        runs.add(new long[]{ runStart - BASE, runLen });
                        out.printf("  run @RVA 0x%08x  length %d  (block %s)%n",
                            runStart - BASE, runLen, b.getName());
                    }
                    runStart = -1; runLen = 0;
                }
                if (monitor.isCancelled()) break;
            }
            if (runStart >= 0 && runLen >= kMinRun) {
                runs.add(new long[]{ runStart - BASE, runLen });
                out.printf("  run @RVA 0x%08x  length %d  (block %s)%n",
                    runStart - BASE, runLen, b.getName());
            }
        }
        out.flush();

        // Verify each candidate base against all eight logged VAs.
        final long VA_GNATIVES = 0x7ff79d2a9b70L;
        String[] names = {
            "GUObjectArray", "GMalloc", "FName::ToString", "FName::FName",
            "StaticConstructObject", "GNatives", "GameEngineTick", "ProcessEvent",
        };
        long[] vas = {
            0x7ff79d2ab0d0L, 0x7ff79d1bc310L, 0x7ff794b1f9b0L, 0x7ff794b011f0L,
            0x7ff794d1e3f0L, 0x7ff79d2a9b70L, 0x7ff79706b500L, 0x7ff794cf2100L,
        };
        boolean[] isFunc = { false, false, true, true, true, false, true, true };

        out.println();
        out.println("## base candidates scored against the eight UE4SS VAs");
        for (long[] r : runs) {
            long cand = VA_GNATIVES - (BASE + r[0]) + BASE;   // implied image base in Ghidra terms
            long implied = VA_GNATIVES - r[0];                // real runtime base
            out.printf("%n-- if GNatives is RVA 0x%08x -> runtime base 0x%x%n", r[0], implied);
            int good = 0;
            for (int i = 0; i < vas.length; i++) {
                long rva = vas[i] - implied;
                String verdict;
                if (rva < 0x1000 || rva >= SIZE) {
                    verdict = "OUT OF IMAGE";
                } else if (isFunc[i]) {
                    Function f = fm.getFunctionContaining(addr(BASE + rva));
                    boolean entry = f != null && f.getEntryPoint().getOffset() == BASE + rva;
                    verdict = entry ? "function ENTRY  ok"
                        : (f != null ? "inside fn (not entry)" : "not a function");
                    if (entry) good++;
                } else {
                    MemoryBlock b = mem.getBlock(addr(BASE + rva));
                    boolean dataOk = b != null && b.isInitialized() && !b.isExecute();
                    verdict = dataOk ? ("data in " + b.getName() + "  ok")
                        : (b == null ? "unmapped" : "in " + b.getName());
                    if (dataOk) good++;
                }
                out.printf("   %-24s RVA 0x%08x  %s%n", names[i], rva, verdict);
            }
            out.printf("   SCORE %d/8%n", good);
            out.flush();
        }
        out.close();
        println("FindUObjectGlobals done");
    }
}
