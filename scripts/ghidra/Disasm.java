// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Targeted disassembly + GPV-pointer (vtable slot) scan for SWtD.
// Output: .lab/ghidra/disasm.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import java.io.PrintWriter;

public class Disasm extends GhidraScript {
    long BASE;
    Listing listing;
    FunctionManager fm;
    Memory mem;
    PrintWriter out;

    Address addr(long v){ return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v); }

    void dumpFn(long rva, int maxIns) {
        Address entry = addr(BASE+rva);
        Function fn = fm.getFunctionContaining(entry);
        out.printf("%n## disasm fn @ RVA 0x%08x  (%s)%n", rva,
            fn!=null? fn.getName()+" entry=0x"+Long.toHexString(fn.getEntryPoint().getOffset()-BASE) : "NO FUNCTION");
        InstructionIterator ii = listing.getInstructions(entry, true);
        int n=0;
        while (ii.hasNext() && n<maxIns) {
            Instruction ins = ii.next();
            if (fn!=null && !fn.getBody().contains(ins.getAddress())) break;
            long irva = ins.getAddress().getOffset()-BASE;
            // annotate referenced string/data
            String ann = "";
            for (ghidra.program.model.symbol.Reference r : ins.getReferencesFrom()) {
                Address ta = r.getToAddress();
                if (ta==null) continue;
                Data d = listing.getDataAt(ta);
                if (d!=null) { try { Object v=d.getValue(); if (v instanceof String) ann=" ; \""+v+"\""; } catch(Exception e){} }
            }
            out.printf("   0x%08x  %-40s%s%n", irva, ins.toString(), ann);
            n++;
        }
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/disasm.txt");

        long GPV = 0x03b12e30L;

        // 1. GPV body (what it reads/calls - confirms POV source).
        dumpFn(GPV, 120);

        // 2. MinimalViewInfo referencer.
        dumpFn(0x03276a80L, 160);

        // 3. Scan all readable memory for a qword == BASE+GPV (vtable slots).
        //    Report each hit RVA; consecutive hits sharing a slot offset pin
        //    the slot index. Also dump the 4 qwords around the first few hits.
        out.printf("%n## GPV pointer occurrences (vtable slots holding 0x%08x)%n", GPV);
        long target = BASE + GPV;
        int hits = 0;
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized()) continue;
            String bn = b.getName();
            if (!(bn.contains("rdata") || bn.contains("data") || bn.contains("RTTI") || bn.equals(".rdata"))) {
                // still scan everything initialized, but note block
            }
            Address start = b.getStart();
            long len = b.getSize();
            byte[] buf = new byte[(int)Math.min(len, 0x4000000)];
            // scan in chunks
            long off = 0;
            while (off < len) {
                int chunk = (int)Math.min(buf.length, len-off);
                try { mem.getBytes(start.add(off), buf, 0, chunk); }
                catch(Exception e){ off += chunk; continue; }
                for (int i=0;i+8<=chunk;i+=8) {
                    long val = 0;
                    for (int k=0;k<8;k++) val |= (buf[i+k]&0xFFL)<<(8*k);
                    if (val == target) {
                        long hitRva = (start.getOffset()+off+i)-BASE;
                        out.printf("   hit @ RVA 0x%08x  (block %s)%n", hitRva, bn);
                        hits++;
                        if (hits <= 4) {
                            // dump 6 slots before/after to reveal vtable shape
                            for (int s=-6;s<=6;s++) {
                                long sa = start.getOffset()+off+i + (long)s*8;
                                try {
                                    byte[] q=new byte[8]; mem.getBytes(addr(sa), q);
                                    long qv=0; for(int k=0;k<8;k++) qv|=(q[k]&0xFFL)<<(8*k);
                                    String tag = (qv>=BASE && qv<BASE+0x0a5e3000L)? String.format("RVA 0x%08x",qv-BASE) : "----";
                                    out.printf("       [%+d] 0x%08x = 0x%016x  %s%s%n",
                                        s, sa-BASE, qv, tag, s==0?"  <== GPV":"");
                                } catch(Exception e){}
                            }
                        }
                        if (hits >= 40) break;
                    }
                }
                off += chunk;
                if (hits >= 40) break;
            }
            if (hits >= 40) break;
        }
        out.printf("   total hits (capped 40): %d%n", hits);

        out.flush(); out.close();
        println("Wrote disasm.txt");
    }
}
