// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Locate the rig that aims Still Wakes the Deep's torch.
//
// The torch is native Habitat code, not a Blueprint: the EXE carries
// AHabitatCharacterTorch, UHabitatTorchSpringArmComponent and the reflected
// fields WanderMaxRotation / WanderNoiseSpeed / WanderRotationInterpSpeed /
// SpringArmTargetLength / bApplyTorchWanderingRotation, alongside stock
// USpringArmComponent's bUsePawnControlRotation and CameraRotationLagSpeed.
// A spring arm with bUsePawnControlRotation reads the PAWN's view rotation,
// which is the mouse/pad aim - exactly what the player reports the beam
// following.
//
// Three things are needed to put the beam on the head instead:
//
//   1. USpringArmComponent::UpdateDesiredArmLocation, anchored on the
//      SCENE_QUERY_STAT(SpringArm) trace-tag string it builds its collision
//      query with. It is the function that turns the target rotation into the
//      socket transform, so the virtual call it makes to GetTargetRotation is
//      visible in its disassembly as `call qword ptr [reg + slot]`.
//   2. That slot index, read off the indirect call above.
//   3. UHabitatTorchSpringArmComponent's vtable, so the slot can be resolved to
//      the torch's own override. IMPLEMENT_CLASS hands GetPrivateStaticClassBody
//      an InternalConstructor<T>, and that constructor is where the vtable
//      pointer is stored into the fresh object - so decompiling it names the
//      vtable.
//
// Output: .lab/ghidra/torch_rig.txt
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.scalar.Scalar;
import java.io.PrintWriter;
import java.util.*;

public class FindTorchRig extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; Memory mem; PrintWriter out;
    DecompInterface di;

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

    Set<Function> refFunctions(Address stringAddr) throws Exception {
        Set<Function> fns = new LinkedHashSet<>();
        for (Reference r : getReferencesTo(stringAddr)) {
            Function f = fm.getFunctionContaining(r.getFromAddress());
            if (f != null) fns.add(f);
        }
        // Data pointers into .rdata (the reflection tables) are followed one
        // level: their consumers are what matter, not the table itself.
        for (Address p : findPointersTo(stringAddr.getOffset())) {
            for (Reference r : getReferencesTo(p)) {
                Function f = fm.getFunctionContaining(r.getFromAddress());
                if (f != null) fns.add(f);
            }
        }
        return fns;
    }

    void decomp(Function f, int maxLines) {
        if (f == null) { out.println("      <no function>"); return; }
        DecompileResults res = di.decompileFunction(f, 120, monitor);
        if (res == null || res.getDecompiledFunction() == null) {
            out.println("      <decompile failed>");
            return;
        }
        String[] lines = res.getDecompiledFunction().getC().split("\n");
        for (int i = 0; i < Math.min(lines.length, maxLines); i++) out.println("      | " + lines[i]);
        if (lines.length > maxLines) out.println("      | ... (" + (lines.length - maxLines) + " more)");
    }

    // Every `call qword ptr [reg + N]` in a function, with the instruction
    // before it, which is where the vtable pointer was loaded from.
    void dumpIndirectCalls(Function f) {
        if (f == null) return;
        out.println("      indirect calls (vtable slots):");
        InstructionIterator it = listing.getInstructions(f.getBody(), true);
        Instruction prev = null, prev2 = null;
        while (it.hasNext()) {
            Instruction ins = it.next();
            String m = ins.getMnemonicString();
            if (m.equalsIgnoreCase("CALL")) {
                String rep = ins.toString();
                if (rep.contains("[")) {
                    out.printf("        0x%08x  %s%n", ins.getAddress().getOffset() - BASE, rep);
                    if (prev2 != null)
                        out.printf("            prev2: %s%n", prev2.toString());
                    if (prev != null)
                        out.printf("            prev:  %s%n", prev.toString());
                }
            }
            prev2 = prev; prev = ins;
        }
    }

    // IMPLEMENT_CLASS's InternalConstructor<T> placement-news the object, and
    // the constructor writes the vtable pointer into it. Find `LEA reg,[addr]`
    // where addr is .rdata whose first qwords are code - that is the vtable.
    void dumpVtableCandidates(Function f, int depth) {
        if (f == null || depth < 0) return;
        InstructionIterator it = listing.getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            if (!ins.getMnemonicString().equalsIgnoreCase("LEA")) continue;
            for (int op = 0; op < ins.getNumOperands(); op++) {
                for (Object o : ins.getOpObjects(op)) {
                    if (!(o instanceof Scalar) && !(o instanceof Address)) continue;
                    long v = (o instanceof Address) ? ((Address) o).getOffset()
                                                    : ((Scalar) o).getUnsignedValue();
                    if (v < BASE) continue;
                    if (inText(v)) continue;
                    try {
                        long s0 = qword(addr(v)), s1 = qword(addr(v + 8));
                        if (!inText(s0) || !inText(s1)) continue;
                        out.printf("      VTABLE CANDIDATE @RVA 0x%08x  (from %s @0x%08x)%n",
                            v - BASE, ins.toString(), ins.getAddress().getOffset() - BASE);
                        dumpVtable(v, 24);
                    } catch (Exception e) { }
                }
            }
        }
        // One level of callees, since the vtable store often sits in the
        // constructor the InternalConstructor thunk tail-calls.
        for (Function callee : f.getCalledFunctions(monitor)) {
            if (callee.getBody().getNumAddresses() > 4000) continue;
            dumpVtableCandidates(callee, depth - 1);
        }
    }

    void dumpVtable(long va, int slots) {
        for (int i = 0; i < slots; i++) {
            long q;
            try { q = qword(addr(va + i * 8L)); } catch (Exception e) { break; }
            if (!inText(q)) { out.printf("        [%3d] 0x%016x  <not code>%n", i, q); break; }
            Function f = fm.getFunctionContaining(addr(q));
            out.printf("        [%3d] RVA 0x%08x  %s%n", i, q - BASE,
                f == null ? "?" : f.getName());
        }
    }

    void section(String title) {
        out.println();
        out.println("================ " + title + " ================");
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        mem = currentProgram.getMemory();
        out = new PrintWriter(".lab/ghidra/torch_rig.txt");
        di = new DecompInterface();
        di.openProgram(currentProgram);
        out.printf("image base 0x%x%n", BASE);

        // The class names IMPLEMENT_CLASS registers (wide, with the U/A prefix
        // the generated code skips), plus the trace tag that anchors the stock
        // spring-arm update, plus the reflected field names.
        String[] names = {
            "UHabitatTorchSpringArmComponent",
            "HabitatTorchSpringArmComponent",
            "AHabitatCharacterTorch",
            "HabitatCharacterTorch",
            "AHabitatDLCCharacterTorch",
            "USpringArmComponent",
            "SpringArm",
            "SpringArmTargetLength",
            "TargetArmLength",
            "bUsePawnControlRotation",
            "bApplyTorchWanderingRotation",
            "WanderMaxRotation",
            "WanderNoiseSpeed",
            "WanderRotationInterpSpeed",
            "CameraRotationLagSpeed",
            "SocketOffset",
            "bFollowTorch",
            "TorchSpringArmComponent",
        };
        Set<String> wanted = new LinkedHashSet<>(Arrays.asList(names));
        Map<String, List<Address>> idx = stringIndex(wanted);

        for (String n : names) {
            section(n);
            List<Address> sas = idx.get(n);
            if (sas == null) { out.println("  string NOT FOUND"); out.flush(); continue; }
            for (Address sa : sas) {
                out.printf("  string @RVA 0x%08x%n", sa.getOffset() - BASE);
                Set<Function> fns = refFunctions(sa);
                out.println("  referencing functions: " + fns.size());
                int shown = 0;
                for (Function f : fns) {
                    out.printf("   fn %s @RVA 0x%08x size=%d%n", f.getName(),
                        f.getEntryPoint().getOffset() - BASE, f.getBody().getNumAddresses());
                    // The stock spring-arm update is the big one; the class
                    // registration stubs are tiny. Both are worth reading, but
                    // only the update needs its virtual calls enumerated.
                    if (f.getBody().getNumAddresses() > 300) dumpIndirectCalls(f);
                    decomp(f, 130);
                    if (f.getBody().getNumAddresses() < 400) dumpVtableCandidates(f, 1);
                    if (++shown >= 6) { out.println("   ... more referencing functions omitted"); break; }
                }
                out.flush();
            }
        }
        di.dispose();
        out.close();
        println("FindTorchRig done");
    }
}
