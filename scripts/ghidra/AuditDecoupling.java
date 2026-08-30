// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Audit the decoupling story behind the GetPlayerViewPoint caller gate, and
// recover what the runtime gate still lacks:
//   A) what each live GPV caller actually is (owning UE source file / system),
//      two levels up, so "gameplay callers stay clean" is evidence not hope;
//   B) APlayerController::bShowMouseCursor's byte offset + bitmask, read out of
//      the generated reflection setter stub (the params struct only points at a
//      lambda that ORs the bit; that stub carries offset+mask as immediates);
//   C) the interaction / reticle surface: which functions touch screen
//      projection, crosshair or interaction strings, so the reticle question is
//      answered from the binary rather than guessed.
// Output: .lab/ghidra/audit_decoupling.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.*;

public class AuditDecoupling extends GhidraScript {
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

    void dumpFn(String tag, Function fn, int depth, int maxStr) {
        if (fn == null) { out.println(tag + " <no function>"); return; }
        long rva = fn.getEntryPoint().getOffset() - BASE;
        out.printf("%s fn=0x%08x '%s' size=%d%n", tag, rva, fn.getName(), fn.getBody().getNumAddresses());
        List<String> ss = stringsInFn(fn, 8000);
        int shown = 0;
        for (String s : ss) {
            out.println(tag + "   str: " + s.replace('\n', ' '));
            if (++shown >= maxStr) { out.println(tag + "   ... (" + (ss.size() - shown) + " more)"); break; }
        }
        if (depth > 0) {
            Set<Function> callers = fn.getCallingFunctions(monitor);
            out.println(tag + "   callers: " + callers.size());
            int i = 0;
            for (Function c : callers) {
                dumpFn(tag + "  >", c, depth - 1, 12);
                if (++i >= 5) { out.println(tag + "   ... more callers omitted"); break; }
            }
        }
    }

    // ---- part B helpers -------------------------------------------------
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

    void disasmAt(String tag, long va, int count) {
        Address a = addr(va);
        Instruction ins = listing.getInstructionAt(a);
        if (ins == null) {
            try { disassemble(a); } catch (Exception e) { }
            ins = listing.getInstructionAt(a);
        }
        for (int i = 0; i < count && ins != null; i++) {
            out.printf("%s   0x%08x  %s%n", tag, ins.getAddress().getOffset() - BASE, ins.toString());
            ins = ins.getNext();
        }
    }

    Address findStringAddr(String want) {
        DataIterator di = listing.getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next();
            try {
                Object v = d.getValue();
                if (v instanceof String && want.equals(((String) v).trim())) return d.getAddress();
            } catch (Exception e) { }
            if (monitor.isCancelled()) break;
        }
        return null;
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/audit_decoupling.txt");

        out.println("################ PART A: GPV caller identity (depth 2) ################");
        long[] rvas = {
            0x038d91ecL, 0x03b1047fL, 0x038b38b0L, 0x03a7d2a2L,
            0x04fa2983L, 0x043e74e6L, 0x0377a33cL, 0x03655908L,
        };
        for (long rva : rvas) {
            Address a = addr(BASE + rva);
            out.printf("==== ret RVA 0x%08x ====%n", rva);
            Function fn = fm.getFunctionContaining(a);
            dumpFn("  ", fn, 2, 30);
            out.println();
            out.flush();
        }

        out.println();
        out.println("################ PART B: bShowMouseCursor bitfield ################");
        String[] props = { "bShowMouseCursor", "bEnableClickEvents", "bCinematicMode" };
        for (String prop : props) {
            Address sa = findStringAddr(prop);
            out.println("-- property " + prop + " string @ " + (sa == null ? "NOT FOUND" :
                String.format("0x%08x", sa.getOffset() - BASE)));
            if (sa == null) continue;
            List<Address> ptrs = findPointersTo(sa.getOffset());
            out.println("   pointers to it: " + ptrs.size());
            for (Address p : ptrs) {
                out.printf("   params struct candidate @0x%08x%n", p.getOffset() - BASE);
                for (int off = 8; off <= 0x60; off += 8) {
                    long q;
                    try { q = qword(p.add(off)); } catch (Exception e) { continue; }
                    if (inText(q)) {
                        Function f = fm.getFunctionContaining(addr(q));
                        out.printf("     +0x%02x -> text 0x%08x  size=%s%n", off, q - BASE,
                            f == null ? "?" : String.valueOf(f.getBody().getNumAddresses()));
                        disasmAt("     ", q, 8);
                    }
                }
                out.flush();
            }
        }

        out.println();
        out.println("################ PART C: reticle / interaction / projection ################");
        String[] needles = {
            "Reticle", "Crosshair", "CrossHair", "Interact", "Deproject",
            "ProjectWorldToScreen", "ScreenPosition", "ViewportSize",
        };
        DataIterator di = listing.getDefinedData(true);
        int printed = 0;
        while (di.hasNext() && printed < 400) {
            Data d = di.next();
            String s;
            try {
                Object v = d.getValue();
                if (!(v instanceof String)) continue;
                s = ((String) v).trim();
            } catch (Exception e) { continue; }
            if (s.length() < 4 || s.length() > 120) continue;
            boolean hit = false;
            for (String nd : needles) if (s.contains(nd)) { hit = true; break; }
            if (!hit) continue;
            StringBuilder refs = new StringBuilder();
            int nref = 0;
            for (Reference r : getReferencesTo(d.getAddress())) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f != null) {
                    refs.append(String.format(" fn=0x%08x", f.getEntryPoint().getOffset() - BASE));
                    if (++nref >= 6) break;
                }
            }
            out.printf("str@0x%08x %s%s%n", d.getAddress().getOffset() - BASE, s.replace('\n', ' '), refs);
            printed++;
            if (monitor.isCancelled()) break;
        }
        out.flush();
        out.close();
        println("AuditDecoupling done");
    }
}
