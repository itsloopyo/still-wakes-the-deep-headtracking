// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Find the native code behind the game's reticle / interaction-prompt UI.
//
// UE registers a UFUNCTION's native thunk as an { const char* Name, FNativeFuncPtr }
// pair, so a pointer to the name string in .rdata is immediately followed by the
// exec stub's address. Same shape that gave up bShowMouseCursor's bitfield. For
// each name below: locate the string, every .rdata pointer to it, and whatever
// the neighbouring qwords point at in .text, then decompile those.
//
// Also dumps the FProperty params structs that mention the UI class names, since
// those carry each property's byte offset - what a render-transform write needs.
// Output: .lab/ghidra/hud_natives.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.PrintWriter;
import java.util.*;

public class FindHudNatives extends GhidraScript {
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

    Map<String, Address> stringIndex(Set<String> wanted) {
        Map<String, Address> found = new LinkedHashMap<>();
        DataIterator it = listing.getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            try {
                Object v = d.getValue();
                if (!(v instanceof String)) continue;
                String s = ((String) v).trim();
                if (wanted.contains(s) && !found.containsKey(s)) found.put(s, d.getAddress());
            } catch (Exception e) { }
            if (found.size() == wanted.size() || monitor.isCancelled()) break;
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

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/hud_natives.txt");
        di = new DecompInterface();
        di.openProgram(currentProgram);

        String[] names = {
            "SetReticleSize", "GetReticleSize", "ReticleSize",
            "HabitatInteractionPrompt", "HabitatPromptStack", "HabitatSlotWidget",
            "HabitatHUDPreset", "HabitatFadeWidget",
            "ActivateWorldMarker", "DeactivateWorldMarker", "GetWorldMarkerComponent",
            "RenderTransform", "RenderTransformPivot", "bRenderTransformSet",
        };
        Set<String> wanted = new LinkedHashSet<>(Arrays.asList(names));
        Map<String, Address> idx = stringIndex(wanted);

        for (String n : names) {
            Address sa = idx.get(n);
            out.println("================ " + n + " ================");
            if (sa == null) { out.println("  string NOT FOUND"); out.flush(); continue; }
            out.printf("  string @0x%08x%n", sa.getOffset() - BASE);
            List<Address> ptrs = findPointersTo(sa.getOffset());
            out.println("  .rdata pointers: " + ptrs.size());
            int shown = 0;
            for (Address p : ptrs) {
                out.printf("   ptr @0x%08x%n", p.getOffset() - BASE);
                for (int off = 8; off <= 0x40; off += 8) {
                    long q;
                    try { q = qword(p.add(off)); } catch (Exception e) { continue; }
                    if (!inText(q)) continue;
                    Function f = fm.getFunctionContaining(addr(q));
                    out.printf("     +0x%02x -> text 0x%08x size=%s%n", off, q - BASE,
                        f == null ? "?" : String.valueOf(f.getBody().getNumAddresses()));
                    if (off <= 0x10) decomp(q, 60);
                }
                if (++shown >= 4) { out.println("   ... more pointers omitted"); break; }
            }
            out.println();
            out.flush();
        }
        di.dispose();
        out.close();
        println("FindHudNatives done");
    }
}
