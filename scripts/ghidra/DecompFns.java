// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Decompile an arbitrary list of functions by RVA. Driven by the
// SWTD_DECOMP_RVAS environment variable (comma-separated hex, no 0x needed) so
// one compiled script serves every follow-up question without an edit.
// Output: .lab/ghidra/decomp.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;

public class DecompFns extends GhidraScript {
    public void run() throws Exception {
        long BASE = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();
        PrintWriter out = new PrintWriter(
            ".lab/ghidra/decomp.txt");

        String spec = System.getenv("SWTD_DECOMP_RVAS");
        if (spec == null || spec.isEmpty()) { out.println("SWTD_DECOMP_RVAS unset"); out.close(); return; }

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (String part : spec.split(",")) {
            String p = part.trim().replace("0x", "");
            if (p.isEmpty()) continue;
            long rva = Long.parseLong(p, 16);
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(BASE + rva);
            Function f = fm.getFunctionContaining(a);
            out.printf("================ fn 0x%08x ================%n", rva);
            if (f == null) { out.println("  <no function>"); continue; }
            DecompileResults res = di.decompileFunction(f, 180, monitor);
            if (res == null || res.getDecompiledFunction() == null) {
                out.println("  <decompile failed>");
            } else {
                out.println(res.getDecompiledFunction().getC());
            }
            out.println();
            out.flush();
        }
        di.dispose();
        out.close();
        println("DecompFns done");
    }
}
