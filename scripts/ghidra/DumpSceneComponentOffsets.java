// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Read USceneComponent field offsets out of the reflection data.
//
// The flare card is a static mesh on the torch ROOT while the beam lights are
// on the spring ARM, so moving the arm with the head leaves the card behind.
// Putting the card on the arm needs one K2_AttachToComponent call, and checking
// afterwards that it took needs AttachParent's offset - otherwise a call that
// dispatches without doing anything is indistinguishable from a working one.
//
// Every one of these is a UPROPERTY, so its FProperty params struct carries the
// offset as a plain field. The hexdump below is the params struct; the offset
// is the 16-bit value at +0x32, following the ArrayDim word at +0x30 - the same
// place LightFlareStaticMeshComponent's 0x2b8 was read from.
//
// Output: .lab/ghidra/scenecomponent_offsets.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.PrintWriter;
import java.util.*;

public class DumpSceneComponentOffsets extends GhidraScript {
    long BASE;
    Listing listing; Memory mem; PrintWriter out;

    Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
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

    int u16(Address p, int off) throws Exception {
        byte[] b = new byte[2];
        mem.getBytes(p.add(off), b);
        return (b[0] & 0xff) | ((b[1] & 0xff) << 8);
    }

    void hexdump(Address p, int bytes) {
        byte[] b = new byte[bytes];
        try { mem.getBytes(p, b); } catch (Exception e) { out.println("     <unreadable>"); return; }
        for (int i = 0; i < bytes; i += 16) {
            StringBuilder sb = new StringBuilder(String.format("     +0x%02x ", i));
            for (int k = 0; k < 16 && i + k < bytes; k++) sb.append(String.format("%02x ", b[i + k]));
            out.println(sb.toString());
        }
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/scenecomponent_offsets.txt");
        out.printf("image base 0x%x%n", BASE);

        String[] names = {
            "AttachParent", "AttachSocketName", "AttachChildren",
            "RelativeLocation", "RelativeRotation", "RelativeScale3D",
            "bVisible", "TargetArmLength",
        };
        Map<String, List<Address>> idx = stringIndex(new LinkedHashSet<>(Arrays.asList(names)));

        for (String n : names) {
            out.println();
            out.println("================ " + n + " ================");
            List<Address> sas = idx.get(n);
            if (sas == null) { out.println("  string NOT FOUND"); continue; }
            for (Address sa : sas) {
                out.printf("  string @RVA 0x%08x%n", sa.getOffset() - BASE);
                int shown = 0;
                for (Address ptr : findPointersTo(sa.getOffset())) {
                    out.printf("   params struct @RVA 0x%08x", ptr.getOffset() - BASE);
                    try {
                        out.printf("   arrayDim=%d offset=0x%x%n", u16(ptr, 0x30), u16(ptr, 0x32));
                    } catch (Exception e) { out.println(); }
                    hexdump(ptr, 0x40);
                    if (++shown >= 4) { out.println("   ... more omitted"); break; }
                }
            }
            out.flush();
        }
        out.close();
        println("DumpSceneComponentOffsets done");
    }
}
