// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Identify the functions containing the GetPlayerViewPoint call sites observed
// at runtime (inject-mode 0 caller summary). For each return-address RVA:
// containing function, its size, every string it references, the functions that
// call it, and the strings those callers reference. That context is what
// separates the render-path view builder from the gameplay-path callers
// (interaction traces, audio listener) - the distinction the caller gate relies
// on. Output: .lab/ghidra/identify_callers.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.*;

public class IdentifyCallers extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;

    Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    List<String> stringsInFn(Function fn, int limit) {
        LinkedHashSet<String> res = new LinkedHashSet<>();
        if (fn == null) return new ArrayList<>(res);
        int n = 0;
        InstructionIterator ii = listing.getInstructions(fn.getBody(), true);
        while (ii.hasNext()) {
            if (++n > limit) break;
            Instruction ins = ii.next();
            for (Reference r : ins.getReferencesFrom()) {
                Address ta = r.getToAddress();
                if (ta == null) continue;
                Data d = listing.getDataAt(ta);
                if (d != null) {
                    try {
                        Object v = d.getValue();
                        if (v instanceof String) {
                            String s = ((String) v).trim();
                            if (s.length() >= 4) res.add(s);
                        }
                    } catch (Exception e) { }
                }
            }
        }
        return new ArrayList<>(res);
    }

    void dumpFn(String tag, Function fn, int depth) {
        if (fn == null) { out.println(tag + " <no function>"); return; }
        long rva = fn.getEntryPoint().getOffset() - BASE;
        out.printf("%s fn=0x%08x '%s' size=%d%n", tag, rva, fn.getName(), fn.getBody().getNumAddresses());
        List<String> ss = stringsInFn(fn, 6000);
        int shown = 0;
        for (String s : ss) {
            out.println(tag + "   str: " + s.replace('\n', ' '));
            if (++shown >= 25) { out.println(tag + "   ... (" + (ss.size() - shown) + " more)"); break; }
        }
        if (depth > 0) {
            Set<Function> callers = fn.getCallingFunctions(monitor);
            out.println(tag + "   callers: " + callers.size());
            int i = 0;
            for (Function c : callers) {
                dumpFn(tag + "  >", c, depth - 1);
                if (++i >= 6) { out.println(tag + "   ... more callers omitted"); break; }
            }
        }
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/identify_callers.txt");

        long[] rvas = {
            0x038d91ecL, 0x03b1047fL, 0x038b38b0L, 0x03a7d2a2L,
            0x04fa2983L, 0x043e74e6L, 0x0377a33cL, 0x03655908L,
        };

        for (long rva : rvas) {
            Address a = addr(BASE + rva);
            out.printf("==== ret RVA 0x%08x ====%n", rva);
            Instruction ins = listing.getInstructionAt(a);
            Instruction prev = listing.getInstructionBefore(a);
            if (prev != null) out.println("  call insn: " + prev.toString() + "  @0x" + Long.toHexString(prev.getAddress().getOffset() - BASE));
            if (ins != null) out.println("  next insn: " + ins.toString());
            Function fn = fm.getFunctionContaining(a);
            dumpFn("  ", fn, 1);
            out.println();
            out.flush();
        }
        out.close();
        println("IdentifyCallers done");
    }
}
