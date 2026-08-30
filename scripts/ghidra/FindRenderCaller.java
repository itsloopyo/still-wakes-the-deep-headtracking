// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Find GPV's vtable slot, then every CALL [reg + slot*8] site (the virtual
// GetPlayerViewPoint calls Ghidra can't see as direct refs), ranked by
// camera/view context. Output: .lab/ghidra/rendercaller.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import java.io.PrintWriter;
import java.util.*;

public class FindRenderCaller extends GhidraScript {
    long BASE, SIZE = 0x0a5e3000L;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;
    Address addr(long v){ return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v); }
    String blk(Address a){ MemoryBlock b=mem.getBlock(a); return b==null?"?":b.getName(); }
    boolean inText(long v){ if(v<BASE||v>=BASE+SIZE) return false; MemoryBlock b=mem.getBlock(addr(v)); return b!=null && b.getName().contains("text"); }

    List<String> stringsInFn(Function fn,int limit){
        LinkedHashSet<String> res=new LinkedHashSet<>(); int n=0;
        InstructionIterator ii=listing.getInstructions(fn.getBody(),true);
        while(ii.hasNext()){ if(++n>limit) break; Instruction ins=ii.next();
            for(ghidra.program.model.symbol.Reference r:ins.getReferencesFrom()){
                Address ta=r.getToAddress(); if(ta==null) continue; Data d=listing.getDataAt(ta);
                if(d!=null){ try{Object v=d.getValue(); if(v instanceof String) res.add((String)v);}catch(Exception e){} } } }
        return new ArrayList<>(res);
    }

    public void run() throws Exception {
        BASE=currentProgram.getImageBase().getOffset();
        listing=currentProgram.getListing(); fm=currentProgram.getFunctionManager(); mem=currentProgram.getMemory();
        out=new PrintWriter(".lab/ghidra/rendercaller.txt");
        long GPV=0x03b12e30L;

        // 1. Slot index from the vtable at .rdata 0x07d3c648: walk backward while
        //    entries are .text pointers; the vtable base is where that run starts.
        long hit = 0x07d3c648L;
        long base = hit;
        for (int s=1;s<=400;s++){
            long sa = BASE+hit - (long)s*8;
            byte[] q=new byte[8]; mem.getBytes(addr(sa),q);
            long qv=0; for(int k=0;k<8;k++) qv|=(q[k]&0xFFL)<<(8*k);
            if (inText(qv)) base = (hit - (long)s*8);
            else break;
        }
        long slot = (hit - base)/8;
        out.printf("vtable base RVA 0x%08x, GPV at slot %d (disp 0x%x)%n", base, slot, slot*8);
        long disp = slot*8;

        // 2. Scan every function for CALL with an indirect operand at +disp.
        out.printf("%n## CALL [reg + 0x%x] sites (virtual GPV calls), camera-ranked%n", disp);
        String dispHex = "0x"+Long.toHexString(disp);
        List<long[]> rows = new ArrayList<>();   // retRva, callRva, fnRva, score
        List<String> hints = new ArrayList<>();
        FunctionIterator fns = fm.getFunctions(true);
        int scanned=0;
        while (fns.hasNext()){
            Function fn = fns.next(); scanned++;
            boolean found=false; long callRva=0, retRva=0;
            InstructionIterator ii=listing.getInstructions(fn.getBody(),true);
            while(ii.hasNext()){
                Instruction ins=ii.next();
                if(!ins.getMnemonicString().equals("CALL")) continue;
                String t=ins.toString();
                // match "CALL qword ptr [REG + 0xDISP]"
                if(t.contains("[") && t.contains("+ "+dispHex+"]")){
                    found=true; callRva=ins.getAddress().getOffset()-BASE;
                    retRva=callRva+ins.getLength();
                    // score this fn by camera/view strings
                    int score=0; StringBuilder h=new StringBuilder();
                    for(String s: stringsInFn(fn,4000)){
                        if(s.contains("View")||s.contains("Camera")||s.contains("FOV")||s.contains("Fov")||
                           s.contains("Projection")||s.contains("Scene")||s.contains("Aspect")||
                           s.contains("MinimalView")||s.contains("Eye")){ score++; if(h.length()<160){h.append(s).append(" | ");} }
                    }
                    rows.add(new long[]{retRva, callRva, fn.getEntryPoint().getOffset()-BASE, score});
                    hints.add(h.toString());
                }
            }
        }
        out.printf("   scanned %d functions, %d call sites%n", scanned, rows.size());
        // sort by score desc, then retRva
        Integer[] idx = new Integer[rows.size()];
        for(int i=0;i<idx.length;i++) idx[i]=i;
        Arrays.sort(idx,(a,b)->{ long d=rows.get(b)[3]-rows.get(a)[3]; if(d!=0) return d<0?-1:1; return Long.compare(rows.get(a)[0],rows.get(b)[0]); });
        int shown=0;
        for(int j=0;j<idx.length;j++){
            int i=idx[j]; long[] r=rows.get(i);
            out.printf("   retRVA 0x%08x  call@0x%08x  fn 0x%08x  score=%d  %s%n",
                r[0], r[1], r[2], r[3], hints.get(i).length()>150? hints.get(i).substring(0,150):hints.get(i));
            if(++shown>=60) { out.printf("   ... (%d more)%n", rows.size()-shown); break; }
        }
        out.flush(); out.close();
        println("Wrote rendercaller.txt: slot="+slot+" sites="+rows.size());
    }
}
