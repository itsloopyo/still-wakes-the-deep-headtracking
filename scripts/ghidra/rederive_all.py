# SWtD (UE5.4) string/structure-anchored RVA derivation. Does not depend on
# any previously-captured RVA. Emits the load-bearing values for the build
# profile:
#   1. GetPlayerViewPoint hook target (anchored on its checkf string) + prologue
#   2. every static call site to GPV (retRVA = _ReturnAddress) + containing fn,
#      annotated with View/Camera/FOV/Scene string refs so the render-path
#      caller (FMinimalViewInfo builder) stands out.
#   3. bShowMouseCursor bitfield offset (anchored on the property string) for
#      the InGameplay menu gate.
OUT = r".lab/ghidra/rederive.txt"

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()
BASE    = currentProgram.getImageBase().getOffset()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"
def fn_at(rva):
    return fm.getFunctionContaining(addr(BASE + rva))

def fns_for_string(needle, want_substr=True, maxhits=24):
    out = []
    for data in listing.getDefinedData(True):
        if not data.hasStringValue(): continue
        try: s = str(data.getValue())
        except: continue
        ok = (needle.lower() in s.lower()) if want_substr else (needle == s)
        if not ok: continue
        for r in ref_mgr.getReferencesTo(data.getAddress()):
            fn = fm.getFunctionContaining(r.getFromAddress())
            if fn:
                out.append((s, fn.getEntryPoint().getOffset() - BASE,
                            data.getAddress().getOffset() - BASE))
        if len(out) >= maxhits: break
    return out

def strings_in_fn(fn, limit=6000):
    res = []; n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit: break
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta is None: continue
            d = listing.getDataAt(ta)
            if d is not None and d.hasStringValue():
                try: res.append(str(d.getValue()))
                except: pass
    seen = set(); uniq = []
    for s in res:
        if s not in seen:
            seen.add(s); uniq.append(s)
    return uniq

with open(OUT, "w") as f:
    f.write("SWtD re-derivation. image base 0x%x\n" % BASE)
    f.write("=" * 78 + "\n\n")

    # 1. GetPlayerViewPoint - anchored on the checkf string.
    f.write("## 1. GetPlayerViewPoint (anchor: checkf string)\n")
    gpv_rva = None
    for s, frva, srva in fns_for_string("GetPlayerViewPoint"):
        if gpv_rva is None: gpv_rva = frva
        f.write("   fn RVA 0x%08x  (str 0x%x %r)\n" % (frva, srva, s[:70]))
    if gpv_rva is not None:
        f.write("   -> GPV entry RVA 0x%08x\n" % gpv_rva)
        bs = []
        a = addr(BASE + gpv_rva)
        for i in range(16):
            bs.append(mem.getByte(a.add(i)) & 0xFF)
        f.write("   prologue: %s\n" % " ".join("%02x" % b for b in bs))

    # 2. Static call sites to GPV.
    f.write("\n## 2. GPV call sites (retRVA = _ReturnAddress, with fn string hints)\n")
    if gpv_rva is not None:
        gpv = addr(BASE + gpv_rva)
        rows = []
        for r in ref_mgr.getReferencesTo(gpv):
            if not r.getReferenceType().isCall(): continue
            site = r.getFromAddress()
            ins = listing.getInstructionAt(site)
            if ins is None: continue
            ret = site.getOffset() + ins.getLength()
            fn = fm.getFunctionContaining(site)
            fnrva = (fn.getEntryPoint().getOffset()-BASE) if fn else 0
            rows.append((ret-BASE, site.getOffset()-BASE, fnrva, fn))
        rows.sort()
        f.write("   %d static call site(s):\n" % len(rows))
        for ret, cs, fnrva, fn in rows:
            hints = ""
            if fn is not None:
                ss = strings_in_fn(fn)
                key = [x for x in ss if any(k in x for k in
                       ("View", "Camera", "FOV", "Fov", "Scene", "Projection",
                        "Aspect", "MinimalView", "CalcCamera"))]
                hints = ("  hints=%s" % key[:5]) if key else ""
            f.write("     retRVA 0x%08x  call@0x%08x  fn 0x%08x%s\n" %
                    (ret, cs, fnrva, hints))
        if not rows:
            f.write("     (none found statically - GPV is called virtually; "
                    "use runtime inject-mode bisection.)\n")

    # 3. bShowMouseCursor bitfield (InGameplay gate).
    f.write("\n## 3. bShowMouseCursor (anchor: property string)\n")
    for s, frva, srva in fns_for_string("bShowMouseCursor", maxhits=8):
        f.write("   ref in fn RVA 0x%08x  (str 0x%x %r)\n" % (frva, srva, s[:50]))

print("Wrote %s" % OUT)
