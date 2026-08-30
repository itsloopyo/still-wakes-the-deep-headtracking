// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Answer two questions the caller gate cannot answer on its own:
//
//  1. Which functions call ULocalPlayer::GetViewPoint (fn 0x038d8e60)? That is
//     the function the head pose is injected inside, so every caller of it
//     renders / projects through the TRACKED view. If the projection helper
//     that world-space UI uses is on this list, the game's world markers follow
//     their objects for free; if it is not, they drift by the head offset.
//     GetViewPoint is virtual, so the calls are CALL [reg+disp] on its vtable
//     slot, not direct xrefs.
//
//  2. What does the native world-marker layer
//     (Habitat/ui/WorldMarker/SWorldMarkerWidgetLayer.cpp) call? Its checkf
//     __FILE__ string names the translation unit; the functions referencing it
//     are the layer's, and their callees say which projection path it uses.
//
// Output: .lab/ghidra/marker_projection.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.*;

public class TraceMarkerProjection extends GhidraScript {
    long BASE, SIZE = 0x0a5e3000L;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;

    Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    boolean inText(long v) {
        if (v < BASE || v >= BASE + SIZE) return false;
        MemoryBlock b = mem.getBlock(addr(v));
        return b != null && b.getName().contains("text");
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

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/marker_projection.txt");

        final long GETVIEWPOINT = 0x038d8e60L;

        // ---- 1. locate GetViewPoint in the vtables, derive its slot ----------
        out.println("################ GetViewPoint vtable slots ################");
        long want = BASE + GETVIEWPOINT;
        List<Long> slots = new ArrayList<>();
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
                    if (q != want) continue;
                    long hitRva = p + i - BASE;
                    // walk back to the vtable base: contiguous .text pointers
                    long vbase = hitRva;
                    for (int s = 1; s <= 600; s++) {
                        long sa = BASE + hitRva - (long) s * 8;
                        byte[] qb = new byte[8];
                        try { mem.getBytes(addr(sa), qb); } catch (Exception e) { break; }
                        long qv = 0;
                        for (int k = 0; k < 8; k++) qv |= (qb[k] & 0xFFL) << (8 * k);
                        if (inText(qv)) vbase = hitRva - (long) s * 8; else break;
                    }
                    long slot = (hitRva - vbase) / 8;
                    out.printf("  vtable 0x%08x  entry 0x%08x  slot %d (disp 0x%x)%n",
                        vbase, hitRva, slot, slot * 8);
                    if (!slots.contains(slot)) slots.add(slot);
                }
                if (monitor.isCancelled()) break;
            }
        }
        out.flush();

        // ---- 2. every CALL [reg + disp] for those slots ----------------------
        for (long slot : slots) {
            long disp = slot * 8;
            String dispHex = "0x" + Long.toHexString(disp);
            out.printf("%n## CALL [reg + %s] sites (virtual GetViewPoint calls)%n", dispHex);
            FunctionIterator fns = fm.getFunctions(true);
            int found = 0;
            while (fns.hasNext() && found < 80) {
                Function fn = fns.next();
                InstructionIterator ii = listing.getInstructions(fn.getBody(), true);
                int n = 0;
                while (ii.hasNext() && n++ < 20000) {
                    Instruction ins = ii.next();
                    String m = ins.getMnemonicString();
                    if (!m.equals("CALL")) continue;
                    String t = ins.toString();
                    if (!t.contains("[") || !t.contains(dispHex + "]")) continue;
                    Instruction nx = ins.getNext();
                    long retRva = nx == null ? 0 : nx.getAddress().getOffset() - BASE;
                    out.printf("  retRVA 0x%08x  in fn 0x%08x size=%d  | %s%n",
                        retRva, fn.getEntryPoint().getOffset() - BASE,
                        fn.getBody().getNumAddresses(), t);
                    List<String> ss = stringsInFn(fn, 6000);
                    int shown = 0;
                    for (String s : ss) {
                        out.println("      str: " + s.replace('\n', ' '));
                        if (++shown >= 10) break;
                    }
                    found++;
                    break;
                }
                if (monitor.isCancelled()) break;
            }
            out.flush();
        }

        // ---- 3. the native world-marker layer --------------------------------
        out.println();
        out.println("################ SWorldMarkerWidgetLayer translation unit ################");
        Address fileStr = null;
        DataIterator di = listing.getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next();
            try {
                Object v = d.getValue();
                if (v instanceof String && ((String) v).contains("SWorldMarkerWidgetLayer.cpp")) {
                    fileStr = d.getAddress();
                    break;
                }
            } catch (Exception e) { }
        }
        if (fileStr == null) {
            out.println("  __FILE__ string not found");
        } else {
            out.printf("  __FILE__ @0x%08x%n", fileStr.getOffset() - BASE);
            Set<Function> owners = new LinkedHashSet<>();
            for (Reference r : getReferencesTo(fileStr)) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f != null) owners.add(f);
            }
            out.println("  referencing functions: " + owners.size());
            for (Function f : owners) {
                long rva = f.getEntryPoint().getOffset() - BASE;
                out.printf("  == fn 0x%08x size=%d%n", rva, f.getBody().getNumAddresses());
                for (String s : stringsInFn(f, 6000)) out.println("       str: " + s.replace('\n', ' '));
                Set<Function> callees = f.getCalledFunctions(monitor);
                out.println("       callees: " + callees.size());
                int i = 0;
                for (Function c : callees) {
                    out.printf("        -> 0x%08x %s%n", c.getEntryPoint().getOffset() - BASE, c.getName());
                    if (++i >= 40) break;
                }
                // Neighbouring functions share the translation unit; list them so
                // the whole layer can be walked.
                out.println("       (TU neighbours listed below)");
            }
            // Functions within +/- 0x6000 of the first owner, as TU neighbours.
            if (!owners.isEmpty()) {
                Function first = owners.iterator().next();
                long c = first.getEntryPoint().getOffset();
                FunctionIterator ni = fm.getFunctions(
                    new AddressSet(addr(c - 0x6000), addr(c + 0x6000)), true);
                while (ni.hasNext()) {
                    Function f = ni.next();
                    out.printf("   TU~ fn 0x%08x size=%d%n",
                        f.getEntryPoint().getOffset() - BASE, f.getBody().getNumAddresses());
                }
            }
        }
        out.flush();
        out.close();
        println("TraceMarkerProjection done");
    }
}
