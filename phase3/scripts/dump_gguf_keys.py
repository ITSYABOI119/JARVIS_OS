#!/usr/bin/env python3
"""Dump GGUF metadata keys from a model file.

GGUF type IDs (from gguf spec):
  0=UINT8, 1=INT8, 2=UINT16, 3=INT16, 4=UINT32, 5=INT32,
  6=FLOAT32, 7=BOOL, 8=STRING, 9=ARRAY, 10=UINT64, 11=INT64, 12=FLOAT64
"""
import struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else "models/gemma-4-E2B-it-Q4_K_M.gguf"
f = open(path, "rb")
magic = f.read(4)
ver, nt, nk = struct.unpack("<IQQ", f.read(20))
print(f"magic={magic} ver={ver} n_tensors={nt} n_kv={nk}")
print()

# Size in bytes for each scalar type
TYPE_SIZE = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4, 5: 4, 6: 4, 7: 1, 10: 8, 11: 8, 12: 8}
TYPE_FMT  = {0: "<B", 1: "<b", 2: "<H", 3: "<h", 4: "<I", 5: "<i", 6: "<f", 7: "<B", 10: "<Q", 11: "<q", 12: "<d"}
TYPE_NAME = {0:"u8", 1:"i8", 2:"u16", 3:"i16", 4:"u32", 5:"i32", 6:"f32", 7:"bool", 8:"string", 9:"array", 10:"u64", 11:"i64", 12:"f64"}

def read_value(vt):
    """Read a single GGUF value of the given type. Returns (value, display_string)."""
    if vt == 8:  # STRING
        sl = struct.unpack("<Q", f.read(8))[0]
        val = f.read(sl).decode(errors="replace")
        return val, f'"{val}"'
    elif vt == 7:  # BOOL
        val = struct.unpack("<B", f.read(1))[0]
        return val, str(bool(val))
    elif vt == 6:  # FLOAT32
        val = struct.unpack("<f", f.read(4))[0]
        return val, f"{val:.6f}"
    elif vt in TYPE_FMT:
        sz = TYPE_SIZE[vt]
        val = struct.unpack(TYPE_FMT[vt], f.read(sz))[0]
        return val, str(val)
    else:
        return None, f"(unknown type {vt})"

def skip_value(vt):
    """Skip a single GGUF value."""
    if vt == 8:  # STRING
        sl = struct.unpack("<Q", f.read(8))[0]
        f.read(sl)
    elif vt == 9:  # ARRAY
        at, ac = struct.unpack("<IQ", f.read(12))
        for _ in range(ac):
            skip_value(at)
    elif vt in TYPE_SIZE:
        f.read(TYPE_SIZE[vt])

for i in range(nk):
    kl = struct.unpack("<Q", f.read(8))[0]
    key = f.read(kl).decode(errors="replace")
    vt = struct.unpack("<I", f.read(4))[0]

    if vt == 9:  # ARRAY
        at, ac = struct.unpack("<IQ", f.read(12))
        tn = TYPE_NAME.get(at, f"type{at}")
        print(f"  [{i:3d}] {key} = array[{tn}, count={ac}]")
        if ac <= 10:
            for j in range(ac):
                val, disp = read_value(at)
                print(f"         [{j}] = {disp}")
        else:
            # Show first 3 and last 1
            for j in range(3):
                val, disp = read_value(at)
                print(f"         [{j}] = {disp}")
            # Skip middle
            for j in range(3, ac - 1):
                skip_value(at)
            val, disp = read_value(at)
            print(f"         ... ({ac - 4} more)")
            print(f"         [{ac-1}] = {disp}")
    else:
        val, disp = read_value(vt)
        print(f"  [{i:3d}] {key} = {disp}")

f.close()
print(f"\nDone: {nk} keys dumped")
