#!/usr/bin/env python3
"""Dump GGUF tensor names, shapes, and types."""
import struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else "models/gemma-4-E2B-it-Q4_K_M.gguf"
f = open(path, "rb")
magic = f.read(4)
ver, nt, nk = struct.unpack("<IQQ", f.read(20))
print(f"magic={magic} ver={ver} n_tensors={nt} n_kv={nk}")

TYPE_NAMES = {0:"F32",1:"F16",2:"Q4_0",3:"Q4_1",6:"Q5_0",7:"Q5_1",8:"Q8_0",9:"Q8_1",
              10:"Q2_K",11:"Q3_K",12:"Q4_K",13:"Q5_K",14:"Q6_K",15:"Q8_K"}

# Skip all KV pairs (to reach tensor info)
TYPE_SIZE = {0:1,1:1,2:2,3:2,4:4,5:4,6:4,7:1,10:8,11:8,12:8}
def skip_value(vt):
    if vt == 8:
        sl = struct.unpack("<Q", f.read(8))[0]
        f.read(sl)
    elif vt == 9:
        at, ac = struct.unpack("<IQ", f.read(12))
        for _ in range(ac):
            skip_value(at)
    elif vt in TYPE_SIZE:
        f.read(TYPE_SIZE[vt])

for i in range(nk):
    kl = struct.unpack("<Q", f.read(8))[0]
    f.read(kl)
    vt = struct.unpack("<I", f.read(4))[0]
    skip_value(vt)

# Now read tensor info
print(f"\n=== Tensors ({nt} total) ===")
tensors = []
for i in range(nt):
    nl = struct.unpack("<Q", f.read(8))[0]
    name = f.read(nl).decode(errors="replace")
    n_dims = struct.unpack("<I", f.read(4))[0]
    dims = struct.unpack(f"<{n_dims}Q", f.read(8 * n_dims))
    tt = struct.unpack("<I", f.read(4))[0]
    offset = struct.unpack("<Q", f.read(8))[0]
    tensors.append((name, dims, tt))

# Print first 30 and anything norm-related
print("\nFirst 30 tensors:")
for i, (name, dims, tt) in enumerate(tensors[:30]):
    print(f"  [{i:3d}] {name}  dims={dims}  type={TYPE_NAMES.get(tt,str(tt))}")

print("\nAll tensors matching 'norm':")
for i, (name, dims, tt) in enumerate(tensors):
    if "norm" in name.lower():
        print(f"  [{i:3d}] {name}  dims={dims}  type={TYPE_NAMES.get(tt,str(tt))}")

print("\nLayer 0 tensors (blk.0):")
for i, (name, dims, tt) in enumerate(tensors):
    if name.startswith("blk.0."):
        print(f"  [{i:3d}] {name}  dims={dims}  type={TYPE_NAMES.get(tt,str(tt))}")

f.close()
