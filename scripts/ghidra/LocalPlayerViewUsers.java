// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Who calls ULocalPlayer::GetViewPoint (fn 0x038d8e60)? That function is where
// the head pose is injected, so every caller of it sees the TRACKED view - which
// decides whether the game's world-space UI (SWorldMarkerWidgetLayer's markers)
// projects onto the head-tracked frame or onto the clean one.
//
// GetViewPoint is virtual, so the calls are CALL [reg+disp] on its vtable slot;
// scanned across the whole image that displacement matches hundreds of unrelated
// classes. Restricting the scan to LocalPlayer.cpp's own address neighbourhood
// removes the noise: CalcSceneView, GetProjectionData and GetViewPoint are
// emitted together, so a hit there is a real ULocalPlayer view user.
// Output: .lab/ghidra/localplayer_view_users.txt
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.*;

public class LocalPlayerViewUsers extends GhidraScript {
    long BASE;
    Listing listing; FunctionManager fm; PrintWriter out;

    Address addr(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    List<String> stringsInFn(Function fn) {
        LinkedHashSet<String> res = new LinkedHashSet<>();
        InstructionIterator ii = listing.getInstructions(fn.getBody(), true);
        int n = 0;
        while (ii.hasNext() && n++ < 8000) {
            for (Reference r : ii.next().getReferencesFrom()) {
                Data d = listing.getDataAt(r.getToAddress());
                if (d == null) continue;
                try {
                    Object v = d.getValue();
                    if (v instanceof String) {
                        String s = ((String) v).trim();
                        if (s.length() >= 4) res.add(s);
                    }
                } catch (Exception e) { }
            }
        }
        return new ArrayList<>(res);
    }

    public void run() throws Exception {
        BASE = currentProgram.getImageBase().getOffset();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        out = new PrintWriter(".lab/ghidra/localplayer_view_users.txt");

        final long GETVIEWPOINT = 0x038d8e60L;
        final long WINDOW = 0x60000L;
        String[] disps = { "0x2c8]", "0x2d0]" };

        Address lo = addr(BASE + GETVIEWPOINT - WINDOW);
        Address hi = addr(BASE + GETVIEWPOINT + WINDOW);
        out.printf("scanning 0x%08x .. 0x%08x for virtual GetViewPoint calls%n%n",
            GETVIEWPOINT - WINDOW, GETVIEWPOINT + WINDOW);

        FunctionIterator fi = fm.getFunctions(new AddressSet(lo, hi), true);
        while (fi.hasNext()) {
            Function fn = fi.next();
            List<String> sites = new ArrayList<>();
            InstructionIterator ii = listing.getInstructions(fn.getBody(), true);
            int n = 0;
            while (ii.hasNext() && n++ < 20000) {
                Instruction ins = ii.next();
                if (!ins.getMnemonicString().equals("CALL")) continue;
                String t = ins.toString();
                if (!t.contains("[")) continue;
                for (String d : disps) {
                    if (t.contains(d)) {
                        Instruction nx = ins.getNext();
                        sites.add(String.format("retRVA 0x%08x  %s",
                            nx == null ? 0 : nx.getAddress().getOffset() - BASE, t));
                    }
                }
            }
            if (sites.isEmpty()) continue;
            out.printf("== fn 0x%08x size=%d%n", fn.getEntryPoint().getOffset() - BASE,
                fn.getBody().getNumAddresses());
            for (String s : sites) out.println("     " + s);
            int shown = 0;
            for (String s : stringsInFn(fn)) {
                out.println("     str: " + s.replace('\n', ' '));
                if (++shown >= 14) break;
            }
            Set<Function> callers = fn.getCallingFunctions(monitor);
            out.println("     callers: " + callers.size());
            int i = 0;
            for (Function c : callers) {
                out.printf("      < 0x%08x size=%d%n", c.getEntryPoint().getOffset() - BASE,
                    c.getBody().getNumAddresses());
                int cs = 0;
                for (String s : stringsInFn(c)) {
                    out.println("          str: " + s.replace('\n', ' '));
                    if (++cs >= 8) break;
                }
                if (++i >= 6) break;
            }
            out.println();
            out.flush();
            if (monitor.isCancelled()) break;
        }
        out.close();
        println("LocalPlayerViewUsers done");
    }
}
