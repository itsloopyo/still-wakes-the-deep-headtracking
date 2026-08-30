// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Pin the exact function that decides where Still Wakes the Deep's torch beam
// points, and the call site to gate on.
//
// FindTorchRig established the rig from AHabitatCharacterTorch's constructor
// (RVA 0x04f43d00): a HabitatTorchSpringArmComponent hangs off the torch's
// root, and PrimaryLightComponent - the beam - is attached to the SPRING ARM,
// with SecondaryLightComponent under that again. So whatever aims the arm aims
// the beam.
//
// It also turned up two functions of identical size (3309 bytes) referencing
// the SCENE_QUERY_STAT(SpringArm) trace tag: one in the engine's address range
// (0x0373d430) and one in Habitat's (0x04f69f70). That is the stock
// USpringArmComponent::UpdateDesiredArmLocation and the game's override of it.
// Both open with the same call, `FUN_143725180(this, &FRotator)`, which by
// position is USpringArmComponent::GetTargetRotation - the arm's aim, before
// any lag or wander is applied.
//
// This script settles four things:
//   1. That 0x143725180 really is GetTargetRotation (it should read
//      bUsePawnControlRotation and ask the owning pawn for its view rotation -
//      the mouse/pad aim the player reports the beam following).
//   2. The RETURN address of the call to it inside the Habitat override, which
//      is the gate: head rotation is added only for that caller, so every other
//      spring arm and every Blueprint GetTargetRotation call is untouched.
//   3. Which class vtable holds the Habitat override, confirming it belongs to
//      UHabitatTorchSpringArmComponent and not some other Habitat spring arm.
//   4. The class-registration name behind FUN_144eca100, the class getter the
//      torch constructor passes to CreateDefaultSubobject for the arm.
//
// Output: .lab/ghidra/torch_target.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.PrintWriter;
import java.util.*;

public class FindTorchTargetRotation extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;
    DecompInterface di;

    static final long GET_TARGET_ROTATION = 0x143725180L;
    static final long HABITAT_UPDATE_ARM  = 0x144f69f70L;
    static final long ENGINE_UPDATE_ARM   = 0x14373d430L;
    static final long ARM_CLASS_GETTER    = 0x144eca100L;
    static final long TORCH_CTOR          = 0x144f43d00L;

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
        if (f == null) { out.println("    <no function>"); return; }
        DecompileResults res = di.decompileFunction(f, 180, monitor);
        if (res == null || res.getDecompiledFunction() == null) {
            out.println("    <decompile failed>"); return;
        }
        String[] lines = res.getDecompiledFunction().getC().split("\n");
        for (int i = 0; i < Math.min(lines.length, maxLines); i++) out.println("    | " + lines[i]);
        if (lines.length > maxLines) out.println("    | ... (" + (lines.length - maxLines) + " more)");
    }

    // Every CALL in a function, with the RVA of the instruction AFTER it - the
    // return address a detour reads to identify its caller.
    void dumpCalls(long fnVa, int limit) {
        Function f = fm.getFunctionContaining(addr(fnVa));
        if (f == null) { out.println("    <no function>"); return; }
        InstructionIterator it = listing.getInstructions(f.getBody(), true);
        int n = 0;
        while (it.hasNext() && n < limit) {
            Instruction ins = it.next();
            if (!ins.getMnemonicString().equalsIgnoreCase("CALL")) continue;
            long site = ins.getAddress().getOffset();
            long ret = site + ins.getLength();
            Address[] flows = ins.getFlows();
            String tgt = "(indirect)";
            if (flows != null && flows.length > 0) {
                Function cf = fm.getFunctionContaining(flows[0]);
                tgt = String.format("0x%08x %s", flows[0].getOffset() - BASE,
                    cf == null ? "?" : cf.getName());
            }
            out.printf("    call@0x%08x  ret=0x%08x  -> %s   [%s]%n",
                site - BASE, ret - BASE, tgt, ins.toString());
            n++;
        }
    }

    // Slot pointers live in .rdata; walk back from one to the start of its
    // vtable (the first qword that is not code) to identify the table.
    void dumpVtablesContaining(long target) throws Exception {
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
                    if (q != target) continue;
                    long slotAddr = p + i;
                    long tableStart = slotAddr;
                    while (tableStart - 8 >= start) {
                        long prev = qword(addr(tableStart - 8));
                        if (!inText(prev)) break;
                        tableStart -= 8;
                    }
                    out.printf("    slot @RVA 0x%08x  in table @RVA 0x%08x  index %d%n",
                        slotAddr - BASE, tableStart - BASE, (slotAddr - tableStart) / 8);
                }
                if (monitor.isCancelled()) return;
            }
        }
    }

    void section(String t) { out.println(); out.println("================ " + t + " ================"); }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/torch_target.txt");
        di = new DecompInterface();
        di.openProgram(currentProgram);
        out.printf("image base 0x%x%n", BASE);

        section("GetTargetRotation candidate @RVA 0x03725180");
        decomp(GET_TARGET_ROTATION, 160);
        out.println("  its calls:");
        dumpCalls(GET_TARGET_ROTATION, 40);
        out.println("  vtable slots holding it (empty = non-virtual):");
        dumpVtablesContaining(GET_TARGET_ROTATION);
        out.flush();

        section("Habitat UpdateDesiredArmLocation @RVA 0x04f69f70 - first calls");
        dumpCalls(HABITAT_UPDATE_ARM, 25);
        out.println("  vtable slots holding it (identifies the owning class):");
        dumpVtablesContaining(HABITAT_UPDATE_ARM);
        out.flush();

        section("Engine UpdateDesiredArmLocation @RVA 0x0373d430 - first calls");
        dumpCalls(ENGINE_UPDATE_ARM, 25);
        out.println("  vtable slots holding it:");
        dumpVtablesContaining(ENGINE_UPDATE_ARM);
        out.flush();

        section("Spring-arm class getter @RVA 0x04eca100 (from the torch constructor)");
        decomp(ARM_CLASS_GETTER, 90);
        out.flush();

        section("Torch actor constructor @RVA 0x04f43d00 - calls");
        dumpCalls(TORCH_CTOR, 60);
        out.flush();

        di.dispose();
        out.close();
        println("FindTorchTargetRotation done");
    }
}
