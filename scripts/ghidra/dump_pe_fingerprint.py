# PE TimeDateStamp / SizeOfImage / CheckSum from the analyzed program.
# These three uniquely identify the shipped build and route the runtime to
# the matching BuildProfile. Re-run after every game patch.
import struct

OUT = r".lab/ghidra/pe_fingerprint.txt"

mem  = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

base = currentProgram.getImageBase().getOffset()

def rd(off, n):
    a = addr(base + off)
    bs = bytearray(n)
    for i in range(n):
        bs[i] = mem.getByte(a.add(i)) & 0xFF
    return bytes(bs)

e_lfanew = struct.unpack_from("<I", rd(0x3c, 4))[0]
ts   = struct.unpack_from("<I", rd(e_lfanew + 4 + 4, 4))[0]
opt  = e_lfanew + 4 + 20
size = struct.unpack_from("<I", rd(opt + 0x38, 4))[0]
csum = struct.unpack_from("<I", rd(opt + 0x40, 4))[0]

line = "TimeDateStamp=0x%08x SizeOfImage=0x%08x CheckSum=0x%08x imagebase=0x%x" % (
    ts, size, csum, base)
with open(OUT, "w") as f:
    f.write(line + "\n")
print(line)
print("Wrote %s" % OUT)
