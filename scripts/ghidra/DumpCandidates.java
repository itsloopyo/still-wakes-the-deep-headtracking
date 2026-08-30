// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Dump instruction context around each candidate virtual-GPV call site so the
// FMinimalViewInfo render-builder pattern (loc@+0, rot@+0x18, FOV@+0x30 stores
// around a CALL [reg+0x800]) can be spotted by eye. Output: .lab/ghidra/candidates.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.PrintWriter;

public class DumpCandidates extends GhidraScript {
    long BASE; Listing listing; FunctionManager fm; PrintWriter out;
    Address addr(long v){ return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v); }

    void window(long callRva){
        Function fn = fm.getFunctionContaining(addr(BASE+callRva));
        long fnRva = fn!=null? fn.getEntryPoint().getOffset()-BASE : 0;
        out.printf("%n=== call@0x%08x  (fn 0x%08x %s) ===%n", callRva, fnRva, fn!=null?fn.getName():"?");
        long start = (fn!=null && fn.getEntryPoint().getOffset()-BASE > callRva-0x70)
            ? fn.getEntryPoint().getOffset()-BASE : callRva-0x70;
        Address a = addr(BASE+start);
        InstructionIterator ii = listing.getInstructions(a, true);
        int n=0;
        while(ii.hasNext() && n<70){
            Instruction ins=ii.next();
            long irva=ins.getAddress().getOffset()-BASE;
            if(irva > callRva+0x30) break;
            String mark = (irva==callRva)? "  <== GPV call" : "";
            String ann="";
            for(ghidra.program.model.symbol.Reference r:ins.getReferencesFrom()){
                Address ta=r.getToAddress(); if(ta==null) continue; Data d=listing.getDataAt(ta);
                if(d!=null){try{Object v=d.getValue(); if(v instanceof String) ann=" ; \""+v+"\"";}catch(Exception e){}}
            }
            out.printf("   0x%08x  %-42s%s%s%n", irva, ins.toString(), ann, mark);
            n++;
        }
    }

    public void run() throws Exception {
        BASE=currentProgram.getImageBase().getOffset();
        listing=currentProgram.getListing(); fm=currentProgram.getFunctionManager();
        out=new PrintWriter(".lab/ghidra/candidates.txt");
        long[] calls = {
            0x035d9758L, 0x03569225L, 0x037260c4L, 0x03726b5eL, 0x03726daaL,
            0x03a7832bL, 0x03aa7af6L, 0x03afee3bL, 0x03b7b11aL,
            0x0448dd21L, 0x0454f416L, 0x049ab225L, 0x04a6693cL, 0x04a66dcbL,
            0x04a6c43cL, 0x04b71ab1L, 0x04f37a2bL, 0x04f3921aL, 0x04f42604L,
            0x04fdae33L, 0x05066267L
        };
        for(long c: calls) window(c);
        out.flush(); out.close();
        println("Wrote candidates.txt");
    }
}
