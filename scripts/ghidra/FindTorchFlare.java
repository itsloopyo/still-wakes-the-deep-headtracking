// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Find what aims the torch's flare card.
//
// AHabitatCharacterTorch's constructor (RVA 0x04f43d00) creates a
// LightFlareStaticMeshComponent on the torch ROOT - not on the spring arm the
// beam lights hang off - and stores it at actor+0x2b8. The reflected fields
// LightFlareIntensity / LightFlareOpacity / LightFlareMaterialInstance belong
// with it. A player reports a glare that reads as fixed in world space where it
// should sit near the top of the screen, so the question is what sets that
// component's transform each frame and from which camera rotation.
//
// Two ways in, both used here:
//
//   1. The actor's vtable, at RVA 0x08458f10, which the constructor stores.
//      Every slot pointing into Habitat's address range is an override this
//      class wrote, and the per-frame one is among them. They are listed with
//      sizes so the tick-shaped one stands out, and the plausible ones are
//      decompiled.
//   2. The reflected field names, whose FProperty params carry the offset into
//      the actor - which then says which of those overrides touches the flare.
//
// What the answer has to distinguish: a card that is simply attached and
// carried by the torch mesh (in which case the mod left it behind when it moved
// the beam), against one that is re-aimed at the camera every tick from a
// rotation the mod does not touch (in which case it was already wrong before
// the beam moved, and needs the tracked rotation instead).
//
// Output: .lab/ghidra/torch_flare.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.util.*;

public class FindTorchFlare extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;
    DecompInterface di;

    static final long TORCH_VTABLE = 0x148458f10L;
    // Habitat's own code, as seen in every game-class RVA recovered so far
    // (0x044.. to 0x050..). Engine code sits below it.
    static final long HABITAT_LO = 0x144000000L;
    static final long HABITAT_HI = 0x145200000L;

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

    void decomp(long va, int maxLines) {
        Function f = fm.getFunctionContaining(addr(va));
        if (f == null) { out.printf("    <no function at 0x%08x>%n", va - BASE); return; }
        DecompileResults res = di.decompileFunction(f, 180, monitor);
        if (res == null || res.getDecompiledFunction() == null) {
            out.println("    <decompile failed>"); return;
        }
        String[] lines = res.getDecompiledFunction().getC().split("\n");
        for (int i = 0; i < Math.min(lines.length, maxLines); i++) out.println("    | " + lines[i]);
        if (lines.length > maxLines) out.println("    | ... (" + (lines.length - maxLines) + " more)");
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

    void hexdump(Address p, int bytes) {
        byte[] b = new byte[bytes];
        try { mem.getBytes(p, b); } catch (Exception e) { out.println("     <unreadable>"); return; }
        for (int i = 0; i < bytes; i += 16) {
            StringBuilder sb = new StringBuilder(String.format("     +0x%02x ", i));
            for (int k = 0; k < 16 && i + k < bytes; k++) sb.append(String.format("%02x ", b[i + k]));
            out.println(sb.toString());
        }
    }

    void section(String t) { out.println(); out.println("================ " + t + " ================"); }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/torch_flare.txt");
        di = new DecompInterface();
        di.openProgram(currentProgram);
        out.printf("image base 0x%x%n", BASE);

        section("AHabitatCharacterTorch vtable @RVA 0x08458f10 - Habitat overrides");
        List<long[]> overrides = new ArrayList<>();  // {slot, target, size}
        for (int i = 0; i < 400; i++) {
            long q;
            try { q = qword(addr(TORCH_VTABLE + i * 8L)); } catch (Exception e) { break; }
            if (!inText(q)) break;
            if (q < HABITAT_LO || q >= HABITAT_HI) continue;
            Function f = fm.getFunctionContaining(addr(q));
            long size = f == null ? 0 : f.getBody().getNumAddresses();
            overrides.add(new long[]{ i, q, size });
            out.printf("  [%3d] RVA 0x%08x size=%d %s%n", i, q - BASE, size,
                f == null ? "?" : f.getName());
        }
        out.flush();

        // Biggest first: a per-frame update that reads the camera and drives a
        // material is substantial, while the small overrides are getters and
        // lifecycle stubs.
        overrides.sort((a, b) -> Long.compare(b[2], a[2]));
        section("largest Habitat overrides, decompiled");
        int shown = 0;
        for (long[] o : overrides) {
            if (o[2] < 200) break;
            out.printf("  --- slot %d, RVA 0x%08x, size %d ---%n", o[0], o[1] - BASE, o[2]);
            decomp(o[1], 170);
            out.println();
            out.flush();
            if (++shown >= 8) break;
        }

        section("flare field reflection entries (offset into the actor)");
        String[] names = {
            "LightFlareStaticMeshComponent", "LightFlareIntensity",
            "LightFlareOpacity", "LightFlareMaterial", "LightFlareMaterialInstance",
        };
        Map<String, List<Address>> idx = stringIndex(new LinkedHashSet<>(Arrays.asList(names)));
        for (String n : names) {
            out.println("  ---- " + n + " ----");
            List<Address> sas = idx.get(n);
            if (sas == null) { out.println("    string NOT FOUND"); continue; }
            for (Address sa : sas) {
                out.printf("    string @RVA 0x%08x%n", sa.getOffset() - BASE);
                int p = 0;
                for (Address ptr : findPointersTo(sa.getOffset())) {
                    out.printf("     ptr @RVA 0x%08x%n", ptr.getOffset() - BASE);
                    hexdump(ptr, 0x40);
                    // Code that names the field directly - the setter stub, or
                    // whoever builds the material parameter by name.
                    for (Reference r : getReferencesTo(ptr)) {
                        Function f = fm.getFunctionContaining(r.getFromAddress());
                        if (f == null) continue;
                        out.printf("       referenced by %s @RVA 0x%08x size=%d%n", f.getName(),
                            f.getEntryPoint().getOffset() - BASE, f.getBody().getNumAddresses());
                    }
                    if (++p >= 3) { out.println("     ... more pointers omitted"); break; }
                }
                for (Reference r : getReferencesTo(sa)) {
                    Function f = fm.getFunctionContaining(r.getFromAddress());
                    if (f == null) continue;
                    out.printf("     code ref from %s @RVA 0x%08x%n", f.getName(),
                        f.getEntryPoint().getOffset() - BASE);
                }
            }
            out.flush();
        }

        di.dispose();
        out.close();
        println("FindTorchFlare done");
    }
}
