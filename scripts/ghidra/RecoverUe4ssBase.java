// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Recover the ASLR base of the UE4SS session that produced the game folder's
// UE4SS.log, so its runtime VAs convert into RVAs this mod can pin.
//
// GNatives was the obvious anchor and is a dead end: UE fills it at static-init,
// so in the on-disk image it is zeros, not a table of .text pointers.
//
// Brute force is better anyway. Windows loads images on a 64 KB grid, and the
// feasible range is small (every logged VA must fall inside a 0xa5e3000 image).
// For each candidate base, convert the five FUNCTION VAs and require each to
// land exactly on a function entry point, and the three DATA VAs to land in
// writable, non-executable memory. Chance agreement on five independent entry
// points is negligible, so a 5/5 base is the base.
// Output: .lab/ghidra/ue4ss_base.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.PrintWriter;

public class RecoverUe4ssBase extends GhidraScript {
    public void run() throws Exception {
        final long IMAGE = currentProgram.getImageBase().getOffset();
        final long SIZE  = 0x0a5e3000L;
        FunctionManager fm = currentProgram.getFunctionManager();
        Memory mem = currentProgram.getMemory();
        PrintWriter out = new PrintWriter(
            ".lab/ghidra/ue4ss_base.txt");

        String[] names = {
            "FName::ToString", "FName::FName(wchar_t*)", "StaticConstructObject_Internal",
            "GameEngineTick", "ProcessEvent",
            "GUObjectArray", "GMalloc", "GNatives",
        };
        long[] vas = {
            0x7ff794b1f9b0L, 0x7ff794b011f0L, 0x7ff794d1e3f0L,
            0x7ff79706b500L, 0x7ff794cf2100L,
            0x7ff79d2ab0d0L, 0x7ff79d1bc310L, 0x7ff79d2a9b70L,
        };
        final int kNumFuncs = 5;

        long vaMin = Long.MAX_VALUE, vaMax = Long.MIN_VALUE;
        for (long v : vas) { vaMin = Math.min(vaMin, v); vaMax = Math.max(vaMax, v); }
        long loBase = ((vaMax - SIZE) + 0xffffL) & ~0xffffL;
        long hiBase = (vaMin - 0x1000L) & ~0xffffL;
        out.printf("feasible base range 0x%x .. 0x%x (%d candidates)%n%n",
            loBase, hiBase, (hiBase - loBase) / 0x10000 + 1);

        for (long b = loBase; b <= hiBase; b += 0x10000L) {
            int funcHits = 0, dataHits = 0;
            boolean outOfRange = false;
            StringBuilder detail = new StringBuilder();
            for (int i = 0; i < vas.length; i++) {
                long rva = vas[i] - b;
                if (rva < 0x1000 || rva >= SIZE) { outOfRange = true; break; }
                Address a = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(IMAGE + rva);
                if (i < kNumFuncs) {
                    Function f = fm.getFunctionContaining(a);
                    if (f != null && f.getEntryPoint().getOffset() == IMAGE + rva) funcHits++;
                } else {
                    MemoryBlock blk = mem.getBlock(a);
                    if (blk != null && blk.isInitialized() && !blk.isExecute() && blk.isWrite()) dataHits++;
                }
                detail.append(String.format("   %-30s RVA 0x%08x%n", names[i], rva));
            }
            if (!outOfRange && funcHits >= 3) {
                out.printf("== base 0x%x  functions %d/%d  data %d/3%n", b, funcHits, kNumFuncs, dataHits);
                out.print(detail);
                out.println();
                out.flush();
            }
            if (monitor.isCancelled()) break;
        }
        out.close();
        println("RecoverUe4ssBase done");
    }
}
