// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Confirm the class behind the spring-arm override, and that the arm really is
// aimed by the pawn's control rotation.
//
// FindTorchTargetRotation left two things resting on the name alone: that the
// UpdateDesiredArmLocation override at RVA 0x04f69f70 belongs to
// UHabitatTorchSpringArmComponent, and that this arm actually has
// bUsePawnControlRotation set - the branch in GetTargetRotation that replaces
// the arm's own rotation with the owning pawn's view rotation. Without that
// flag the beam would not track the mouse at all and the whole diagnosis would
// be wrong.
//
// Both fall out of the class's own constructor. The class getter at 0x04eca100
// registers "HabitatTorchSpringArmComponent" (/Script/Habitat, size 0x3c0) and
// hands GetPrivateStaticClassBody the InternalConstructor at 0x04eca440, which
// Ghidra left as a bare label rather than a function - so it is disassembled
// here by hand and its callee followed. The constructor stores the class vtable
// into the fresh object: if that is the table holding the override, the
// override is this class's. The byte it ORs at this+0x270 carries
// bUsePawnControlRotation.
//
// Output: .lab/ghidra/torch_verify.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.PrintWriter;
import java.util.*;

public class VerifyTorchSpringArm extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;
    DecompInterface di;

    static final long INTERNAL_CTOR      = 0x144eca440L;
    static final long HABITAT_UPDATE_ARM = 0x144f69f70L;
    static final long ENGINE_UPDATE_ARM  = 0x14373d430L;

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
        out.printf("    fn %s @RVA 0x%08x%n", f.getName(), f.getEntryPoint().getOffset() - BASE);
        DecompileResults res = di.decompileFunction(f, 180, monitor);
        if (res == null || res.getDecompiledFunction() == null) {
            out.println("    <decompile failed>"); return;
        }
        String[] lines = res.getDecompiledFunction().getC().split("\n");
        for (int i = 0; i < Math.min(lines.length, maxLines); i++) out.println("    | " + lines[i]);
        if (lines.length > maxLines) out.println("    | ... (" + (lines.length - maxLines) + " more)");
    }

    // Raw disassembly at an address Ghidra never turned into a function, with
    // the flow targets it reaches, so the constructor behind a bare label can
    // still be followed.
    List<Long> disasm(long va, int count) throws Exception {
        List<Long> targets = new ArrayList<>();
        Address a = addr(va);
        if (listing.getInstructionAt(a) == null) {
            try { disassemble(a); } catch (Exception e) { }
        }
        Instruction ins = listing.getInstructionAt(a);
        for (int i = 0; i < count && ins != null; i++) {
            out.printf("    0x%08x  %s%n", ins.getAddress().getOffset() - BASE, ins.toString());
            Address[] flows = ins.getFlows();
            if (flows != null) for (Address f : flows) targets.add(f.getOffset());
            String m = ins.getMnemonicString();
            if (m.equalsIgnoreCase("RET") || m.equalsIgnoreCase("JMP")) break;
            ins = ins.getNext();
        }
        return targets;
    }

    long slotOf(long table, long target, int slots) throws Exception {
        for (int i = 0; i < slots; i++) {
            if (qword(addr(table + i * 8L)) == target) return i;
        }
        return -1;
    }

    // Walk back from a known slot to the first qword that is not code - the
    // RTTI/COL pointer that precedes an MSVC vtable, or the previous table's
    // padding. Gives the table start the slot index is relative to.
    long tableStart(long slotAddr) throws Exception {
        long t = slotAddr;
        while (true) {
            long prev;
            try { prev = qword(addr(t - 8)); } catch (Exception e) { break; }
            if (!inText(prev)) break;
            t -= 8;
        }
        return t;
    }

    void section(String t) { out.println(); out.println("================ " + t + " ================"); }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/torch_verify.txt");
        di = new DecompInterface();
        di.openProgram(currentProgram);
        out.printf("image base 0x%x%n", BASE);

        section("InternalConstructor @RVA 0x04eca440 - raw disassembly");
        List<Long> targets = disasm(INTERNAL_CTOR, 40);

        section("what it reaches");
        Set<Long> seen = new LinkedHashSet<>(targets);
        for (long t : seen) {
            out.printf("  -> 0x%08x%n", t - BASE);
            decomp(t, 140);
            out.println();
        }
        out.flush();

        section("vtable cross-check");
        // Locate each override's slot pointer by scanning the tables the first
        // pass named, then re-derive the table start from that slot so the two
        // indices are measured the same way.
        long[] approxTables = { 0x1485658f8L, 0x147d7d108L };
        for (long t : approxTables) {
            long hab = slotOf(t, HABITAT_UPDATE_ARM, 400);
            long eng = slotOf(t, ENGINE_UPDATE_ARM, 400);
            out.printf("  approx table @RVA 0x%08x: habitat slot=%d engine slot=%d%n",
                t - BASE, hab, eng);
            if (hab >= 0) {
                long start = tableStart(t + hab * 8L);
                out.printf("    habitat override: true table start @RVA 0x%08x, index %d%n",
                    start - BASE, (t + hab * 8L - start) / 8);
            }
            if (eng >= 0) {
                long start = tableStart(t + eng * 8L);
                out.printf("    engine override: true table start @RVA 0x%08x, index %d%n",
                    start - BASE, (t + eng * 8L - start) / 8);
            }
        }
        out.flush();

        di.dispose();
        out.close();
        println("VerifyTorchSpringArm done");
    }
}
