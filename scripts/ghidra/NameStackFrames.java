// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Name the frames of a crash's portable call stack.
//
// UE's crash context gives module + offset only, so the stack reads as a column
// of hex. For each RVA: the containing function, any strings it references, and
// the nearest __FILE__ strings in its address neighbourhood (the compiler emits
// a translation unit's functions contiguously, so an adjacent checkf names the
// .cpp). That is enough to say which engine subsystem faulted, which is what
// decides whether a mod is implicated or exonerated.
//
// RVAs come from the SWTD_STACK_RVAS environment variable, comma-separated hex.
// Output: .lab/ghidra/stack_frames.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.*;

public class NameStackFrames extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; PrintWriter out;

    Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    List<String> stringsInFn(Function fn, int limit) {
        LinkedHashSet<String> res = new LinkedHashSet<>();
        if (fn == null) return new ArrayList<>(res);
        int n = 0;
        InstructionIterator ii = listing.getInstructions(fn.getBody(), true);
        while (ii.hasNext() && n++ < limit) {
            for (Reference r : ii.next().getReferencesFrom()) {
                Data d = listing.getDataAt(r.getToAddress());
                if (d == null) continue;
                try {
                    Object v = d.getValue();
                    if (v instanceof String) {
                        String s = ((String) v).trim();
                        if (s.length() >= 4) res.add(s);
                    }
                } catch (Exception e) { }
            }
        }
        return new ArrayList<>(res);
    }

    void neighbourhoodFiles(long fnRva, long window, int max) {
        Address lo = addr(BASE + Math.max(0, fnRva - window));
        Address hi = addr(BASE + fnRva + window);
        TreeMap<Long, String> found = new TreeMap<>();
        FunctionIterator fi = fm.getFunctions(new AddressSet(lo, hi), true);
        while (fi.hasNext()) {
            Function f = fi.next();
            long d = Math.abs(f.getEntryPoint().getOffset() - BASE - fnRva);
            for (String s : stringsInFn(f, 3000)) {
                if (s.endsWith(".cpp") || s.endsWith(".h")) {
                    String shortName = s.substring(s.lastIndexOf('\\') + 1);
                    if (!found.containsValue(shortName)) found.put(d, shortName);
                }
            }
            if (monitor.isCancelled()) return;
        }
        int shown = 0;
        for (Map.Entry<Long, String> e : found.entrySet()) {
            out.printf("      TU hint: %-46s (distance 0x%x)%n", e.getValue(), e.getKey());
            if (++shown >= max) break;
        }
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        out = new PrintWriter(".lab/ghidra/stack_frames.txt");

        String spec = System.getenv("SWTD_STACK_RVAS");
        if (spec == null || spec.isEmpty()) { out.println("SWTD_STACK_RVAS unset"); out.close(); return; }

        int frame = 0;
        for (String part : spec.split(",")) {
            String p = part.trim().replace("0x", "");
            if (p.isEmpty()) continue;
            long rva = Long.parseLong(p, 16);
            out.printf("== frame %d  RVA 0x%08x%n", frame++, rva);
            Function f = fm.getFunctionContaining(addr(BASE + rva));
            if (f == null) { out.println("      <no function>"); out.println(); continue; }
            out.printf("      fn 0x%08x size=%d%n",
                f.getEntryPoint().getOffset() - BASE, f.getBody().getNumAddresses());
            int shown = 0;
            for (String s : stringsInFn(f, 6000)) {
                out.println("      str: " + s.replace('\n', ' '));
                if (++shown >= 10) break;
            }
            neighbourhoodFiles(rva, 0x14000, 4);
            out.println();
            out.flush();
        }
        out.close();
        println("NameStackFrames done");
    }
}
