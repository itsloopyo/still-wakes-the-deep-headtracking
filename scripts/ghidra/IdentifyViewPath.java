// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Identify the functions on the GetPlayerViewPoint view path by translation
// unit and by decompiled shape. The live GPV callers are all virtual, so they
// carry no static xrefs and no strings of their own; the way to name them is
// the __FILE__ strings of their NEIGHBOURS (the compiler emits a translation
// unit's functions contiguously, so a checkf in an adjacent function names the
// .cpp the target came from) plus the decompilation.
// Output: .lab/ghidra/view_path.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.*;

public class IdentifyViewPath extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; PrintWriter out;

    Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    // Every .cpp path string referenced from functions within +/- window bytes,
    // nearest first. Names the translation unit the target function lives in.
    void neighbourhoodFiles(long fnRva, long window) {
        Address lo = addr(BASE + Math.max(0, fnRva - window));
        Address hi = addr(BASE + fnRva + window);
        TreeMap<Long, String> found = new TreeMap<>();
        FunctionIterator fi = fm.getFunctions(new AddressSet(lo, hi), true);
        while (fi.hasNext()) {
            Function f = fi.next();
            long d = Math.abs(f.getEntryPoint().getOffset() - BASE - fnRva);
            InstructionIterator ii = listing.getInstructions(f.getBody(), true);
            int n = 0;
            while (ii.hasNext() && n++ < 4000) {
                Instruction ins = ii.next();
                for (Reference r : ins.getReferencesFrom()) {
                    Data dat = listing.getDataAt(r.getToAddress());
                    if (dat == null) continue;
                    try {
                        Object v = dat.getValue();
                        if (!(v instanceof String)) continue;
                        String s = ((String) v).trim();
                        if (s.endsWith(".cpp") || s.endsWith(".h")) {
                            String shortName = s.substring(s.lastIndexOf('\\') + 1);
                            if (!found.containsValue(shortName)) found.put(d, shortName);
                        }
                    } catch (Exception e) { }
                }
            }
            if (monitor.isCancelled()) return;
        }
        int shown = 0;
        for (Map.Entry<Long, String> e : found.entrySet()) {
            out.printf("   TU hint: %-44s (distance 0x%x)%n", e.getValue(), e.getKey());
            if (++shown >= 8) break;
        }
    }

    void decompile(long fnRva, DecompInterface di, int maxLines) {
        Function f = fm.getFunctionContaining(addr(BASE + fnRva));
        if (f == null) { out.println("   <no function>"); return; }
        DecompileResults res = di.decompileFunction(f, 120, monitor);
        if (res == null || res.getDecompiledFunction() == null) {
            out.println("   <decompile failed: " + (res == null ? "null" : res.getErrorMessage()) + ">");
            return;
        }
        String[] lines = res.getDecompiledFunction().getC().split("\n");
        for (int i = 0; i < Math.min(lines.length, maxLines); i++) out.println("   | " + lines[i]);
        if (lines.length > maxLines) out.println("   | ... (" + (lines.length - maxLines) + " more lines)");
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        out = new PrintWriter(".lab/ghidra/view_path.txt");

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        long[] targets = {
            0x038d8e60L,  // caller 1 - the confirmed render/view path
            0x03b102a0L,  // caller 2 - PlayerController.cpp neighbourhood
            0x03a7d260L,  // caller 4
            0x043e73e0L,  // caller 6
            0x03655830L,  // caller 8
            0x04fa28b0L,  // TraceInteractable (game interaction ray)
        };
        for (long t : targets) {
            out.printf("================ fn 0x%08x ================%n", t);
            neighbourhoodFiles(t, 0x20000);
            out.println("   ---- decompiled ----");
            decompile(t, di, 140);
            out.println();
            out.flush();
        }
        di.dispose();
        out.close();
        println("IdentifyViewPath done");
    }
}
