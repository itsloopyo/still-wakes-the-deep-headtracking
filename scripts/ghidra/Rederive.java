// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// SWtD (UE5.4) static camera discovery. Java GhidraScript (Ghidra 12 dropped
// Jython; Python scripts need PyGhidra). Emits:
//   0. PE fingerprint (ts/size/csum)
//   1. UE camera-related string landscape (which anchors actually survived
//      into this shipping build) + referencing functions.
//   2. GetPlayerViewPoint entry (if stringed) + prologue.
//   3. Static call sites to GPV (retRVA + containing fn + View/Camera/FOV hints)
//      -> the render-path caller (FMinimalViewInfo builder) stands out.
//   4. bShowMouseCursor bitfield references.
// Output: .lab/ghidra/rederive.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceManager;
import java.io.PrintWriter;
import java.util.*;

public class Rederive extends GhidraScript {

    long BASE;
    Listing listing;
    FunctionManager fm;
    ReferenceManager refMgr;
    Memory mem;
    PrintWriter out;

    // string-value -> list of (functionRVA) referencing it, and the string addr
    static class Hit { String s; long fnRva; long strRva; Hit(String s,long f,long sr){this.s=s;this.fnRva=f;this.strRva=sr;} }

    Address addr(long v){ return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v); }
    String blk(Address a){ MemoryBlock b=mem.getBlock(a); return b==null?"?":b.getName(); }

    // Cache of all defined string data (value + address), built once.
    List<Object[]> strs = new ArrayList<>();  // [String value, Long strAddrOffset]

    void buildStringCache() {
        DataIterator it = listing.getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            if (d == null) continue;
            Object v;
            try { v = d.getValue(); } catch (Exception e) { continue; }
            if (v instanceof String) {
                strs.add(new Object[]{ (String)v, d.getAddress().getOffset() });
            }
        }
    }

    List<Hit> fnsForString(String needle, int maxhits) {
        List<Hit> res = new ArrayList<>();
        String nl = needle.toLowerCase();
        for (Object[] e : strs) {
            String s = (String)e[0];
            if (!s.toLowerCase().contains(nl)) continue;
            long strOff = (Long)e[1];
            for (Reference r : refMgr.getReferencesTo(addr(strOff))) {
                Function fn = fm.getFunctionContaining(r.getFromAddress());
                long frva = (fn!=null)? fn.getEntryPoint().getOffset()-BASE : 0;
                res.add(new Hit(s, frva, strOff-BASE));
            }
            if (res.size() >= maxhits) break;
        }
        return res;
    }

    List<String> stringsInFn(Function fn, int limit) {
        LinkedHashSet<String> res = new LinkedHashSet<>();
        int n=0;
        InstructionIterator ii = listing.getInstructions(fn.getBody(), true);
        while (ii.hasNext()) {
            if (++n > limit) break;
            Instruction ins = ii.next();
            for (Reference r : ins.getReferencesFrom()) {
                Address ta = r.getToAddress();
                if (ta == null) continue;
                Data d = listing.getDataAt(ta);
                if (d != null) {
                    try { Object v = d.getValue(); if (v instanceof String) res.add((String)v); }
                    catch (Exception ex) {}
                }
            }
        }
        return new ArrayList<>(res);
    }

    long readU32(long off){
        try {
            Address a = addr(BASE+off);
            byte[] b = new byte[4];
            mem.getBytes(a, b);
            return (b[0]&0xFFL)|((b[1]&0xFFL)<<8)|((b[2]&0xFFL)<<16)|((b[3]&0xFFL)<<24);
        } catch(Exception e){ return -1; }
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        refMgr = currentProgram.getReferenceManager();
        mem = currentProgram.getMemory();

        String path = ".lab/ghidra/rederive.txt";
        out = new PrintWriter(path);

        out.printf("SWtD re-derivation. image base 0x%x%n", BASE);
        out.println("=".substring(0,1).repeat(78));

        // 0. PE fingerprint
        long e_lfanew = readU32(0x3c);
        long ts = readU32(e_lfanew + 8);
        long opt = e_lfanew + 24;
        long size = readU32(opt + 0x38);
        long csum = readU32(opt + 0x40);
        out.printf("%n## 0. PE fingerprint%n");
        out.printf("   TimeDateStamp=0x%08x SizeOfImage=0x%08x CheckSum=0x%08x%n", ts, size, csum);

        println("building string cache...");
        buildStringCache();
        out.printf("%n   (string cache: %d defined strings)%n", strs.size());
        println("string cache: " + strs.size());

        // 1. UE camera string landscape
        out.printf("%n## 1. UE camera string landscape (which anchors survived)%n");
        String[] anchors = {
            "GetPlayerViewPoint", "GetPlayerViewpoint", "CalcCamera",
            "PlayerCameraManager", "UpdateViewTarget", "GetActorEyesViewPoint",
            "MinimalViewInfo", "bShowMouseCursor", "ViewTarget", "GetViewPoint",
            "FOV", "DefaultFOV"
        };
        for (String a : anchors) {
            List<Hit> h = fnsForString(a, 6);
            out.printf("   %-24s -> %d ref(s)%n", a, h.size());
            for (Hit x : h)
                out.printf("       fn 0x%08x  str 0x%08x  %s%n", x.fnRva, x.strRva,
                    x.s.length()>60? x.s.substring(0,60):x.s);
        }

        // 2. GetPlayerViewPoint entry candidate(s)
        out.printf("%n## 2. GetPlayerViewPoint (anchor: name string)%n");
        Long gpvRva = null;
        for (Hit x : fnsForString("GetPlayerViewPoint", 12)) {
            if (gpvRva == null && x.fnRva != 0) gpvRva = x.fnRva;
            out.printf("   fn 0x%08x (str 0x%08x %s)%n", x.fnRva, x.strRva,
                x.s.length()>55? x.s.substring(0,55):x.s);
        }
        if (gpvRva != null) {
            out.printf("   -> GPV entry RVA 0x%08x%n", gpvRva);
            StringBuilder sb = new StringBuilder();
            Address a = addr(BASE + gpvRva);
            for (int i=0;i<16;i++) sb.append(String.format("%02x ", mem.getByte(a.add(i))&0xFF));
            out.printf("   prologue: %s%n", sb.toString().trim());

            // 3. static call sites to GPV
            out.printf("%n## 3. GPV call sites (retRVA = _ReturnAddress, with hints)%n");
            List<long[]> rows = new ArrayList<>();   // [retRva, callSiteRva, fnRva]
            List<Function> fns = new ArrayList<>();
            for (Reference r : refMgr.getReferencesTo(addr(BASE+gpvRva))) {
                if (!r.getReferenceType().isCall()) continue;
                Address site = r.getFromAddress();
                Instruction ins = listing.getInstructionAt(site);
                if (ins == null) continue;
                long ret = site.getOffset() + ins.getLength();
                Function fn = fm.getFunctionContaining(site);
                long fnrva = (fn!=null)? fn.getEntryPoint().getOffset()-BASE : 0;
                rows.add(new long[]{ret-BASE, site.getOffset()-BASE, fnrva});
                fns.add(fn);
            }
            out.printf("   %d static call site(s)%n", rows.size());
            for (int i=0;i<rows.size();i++) {
                long[] row = rows.get(i);
                Function fn = fns.get(i);
                String hints = "";
                if (fn != null) {
                    List<String> ss = stringsInFn(fn, 6000);
                    List<String> key = new ArrayList<>();
                    for (String s : ss)
                        if (s.contains("View")||s.contains("Camera")||s.contains("FOV")||
                            s.contains("Fov")||s.contains("Scene")||s.contains("Projection")||
                            s.contains("Aspect")||s.contains("MinimalView")||s.contains("CalcCamera"))
                            { key.add(s); if (key.size()>=5) break; }
                    if (!key.isEmpty()) hints = "  hints="+key;
                }
                out.printf("     retRVA 0x%08x  call@0x%08x  fn 0x%08x%s%n",
                    row[0], row[1], row[2], hints);
            }
            if (rows.isEmpty())
                out.printf("     (no static callers - GPV called virtually; need vtable/runtime)%n");
        } else {
            out.printf("   NOT stringed in this build - use vtable/RTTI or CalcCamera path.%n");
        }

        out.flush(); out.close();
        println("Wrote " + path);
    }
}
