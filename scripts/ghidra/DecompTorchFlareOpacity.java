// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Read the torch flare card's opacity driver.
//
// FindTorchFlare established what the native code does with
// LightFlareStaticMeshComponent (actor+0x2b8): AHabitatCharacterTorch's slot-114
// override builds a dynamic material instance from LightFlareMaterial
// (actor+0x2c0) into LightFlareMaterialInstance (actor+0x3f0), and calls
// FUN_144f6b180, which is the only function naming the LightFlareOpacity
// parameter. Nothing in the class's nine vtable overrides re-aims the card.
//
// What is still open is whether the card is meant to read as a view-relative
// flare or as a glow that belongs to the torch. If the opacity driver computes
// an angle between the card and a camera - and above all WHICH camera rotation
// it reads - that settles it, and names a second thing head tracking would have
// left on the clean view.
//
// Output: .lab/ghidra/torch_flare_opacity.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.PrintWriter;
import java.util.*;

public class DecompTorchFlareOpacity extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;
    DecompInterface di;

    // The opacity driver, the helper slot 114 calls just before it, and the
    // torch's remaining Habitat vtable overrides, which are the only other
    // native code the class runs.
    static final long[] TARGETS = {
        0x144f6b180L,  // names LightFlareOpacity
        0x144f5a740L,  // called from the slot-114 override
        0x144f66710L,  // vtable slot 158
        0x144f5d620L,  // slot 231
        0x144f6be50L,  // slot 232
        0x144f4c980L,  // slot 233
        0x144f6b800L,  // slot 234
        0x144e56890L,  // slot 0
    };

    Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    void decomp(long va, int maxLines) {
        Function f = fm.getFunctionContaining(addr(va));
        if (f == null) { out.printf("  <no function at 0x%08x>%n", va - BASE); return; }
        out.printf("  fn %s @RVA 0x%08x size=%d%n", f.getName(),
            f.getEntryPoint().getOffset() - BASE, f.getBody().getNumAddresses());
        DecompileResults res = di.decompileFunction(f, 200, monitor);
        if (res == null || res.getDecompiledFunction() == null) {
            out.println("  <decompile failed>"); return;
        }
        String[] lines = res.getDecompiledFunction().getC().split("\n");
        for (int i = 0; i < Math.min(lines.length, maxLines); i++) out.println("  | " + lines[i]);
        if (lines.length > maxLines) out.println("  | ... (" + (lines.length - maxLines) + " more)");
    }

    // Direct calls out of a function, so a camera lookup shows up by name even
    // when the arithmetic around it decompiles badly.
    void dumpCallees(long va) {
        Function f = fm.getFunctionContaining(addr(va));
        if (f == null) return;
        out.println("  callees:");
        InstructionIterator it = listing.getInstructions(f.getBody(), true);
        Set<Long> seen = new LinkedHashSet<>();
        while (it.hasNext()) {
            Instruction ins = it.next();
            if (!ins.getMnemonicString().equalsIgnoreCase("CALL")) continue;
            Address[] flows = ins.getFlows();
            if (flows == null || flows.length == 0) {
                out.printf("    0x%08x  %s%n", ins.getAddress().getOffset() - BASE, ins.toString());
                continue;
            }
            if (!seen.add(flows[0].getOffset())) continue;
            Function cf = fm.getFunctionContaining(flows[0]);
            out.printf("    -> 0x%08x %s (size %s)%n", flows[0].getOffset() - BASE,
                cf == null ? "?" : cf.getName(),
                cf == null ? "?" : String.valueOf(cf.getBody().getNumAddresses()));
        }
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/torch_flare_opacity.txt");
        di = new DecompInterface();
        di.openProgram(currentProgram);
        out.printf("image base 0x%x%n", BASE);

        for (long t : TARGETS) {
            out.println();
            out.printf("================ RVA 0x%08x ================%n", t - BASE);
            decomp(t, 200);
            dumpCallees(t);
            out.flush();
        }

        di.dispose();
        out.close();
        println("DecompTorchFlareOpacity done");
    }
}
