#!/usr/bin/env python3
"""Dump GGUF metadata keys from a model file."""
import struct, sys

path = sys.argv[1] if len(sys.argv) > 1 else "models/gemma-4-E2B-it-Q4_K_M.gguf"
f = open(path, "rb")
magic = f.read(4)
ver, nt, nk = struct.unpack("<IQQ", f.read(20))
print(f"magic={magic} ver={ver} n_tensors={nt} n_kv={nk}")
print()

for i in range(nk):
    kl = struct.unpack("<Q", f.read(8))[0]
    key = f.read(kl).decode()
    vt = struct.unpack("<I", f.read(4))[0]

    if vt == 8:  # bool
        val = struct.unpack("<B", f.read(1))[0]
        print(f"  [{i:3d}] {key} = {bool(val)}")
    elif vt == 5:  # u32
        val = struct.unpack("<I", f.read(4))[0]
        print(f"  [{i:3d}] {key} = {val}")
    elif vt == 6:  # i32
        val = struct.unpack("<i", f.read(4))[0]
        print(f"  [{i:3d}] {key} = {val}")
    elif vt == 7:  # f32
        val = struct.unpack("<f", f.read(4))[0]
        print(f"  [{i:3d}] {key} = {val:.6f}")
    elif vt == 10:  # u64
        val = struct.unpack("<Q", f.read(8))[0]
        print(f"  [{i:3d}] {key} = {val}")
    elif vt == 4:  # string
        sl = struct.unpack("<Q", f.read(8))[0]
        val = f.read(sl).decode()
        print(f'  [{i:3d}] {key} = "{val}"')
    elif vt == 9:  # array
        at, ac = struct.unpack("<IQ", f.read(12))
        type_names = {4: "string", 5: "u32", 6: "i32", 7: "f32", 8: "bool", 10: "u64"}
        tn = type_names.get(at, f"type{at}")
        print(f"  [{i:3d}] {key} = array[{tn}, count={ac}]")
        # Print first few elements for small arrays
        if ac <= 10:
            for j in range(ac):
                if at == 5:
                    v = struct.unpack("<I", f.read(4))[0]
                    print(f"         [{j}] = {v}")
                elif at == 6:
                    v = struct.unpack("<i", f.read(4))[0]
                    print(f"         [{j}] = {v}")
                elif at == 7:
                    v = struct.unpack("<f", f.read(4))[0]
                    print(f"         [{j}] = {v:.6f}")
                elif at == 8:
                    v = struct.unpack("<B", f.read(1))[0]
                    print(f"         [{j}] = {bool(v)}")
                elif at == 4:
                    sl2 = struct.unpack("<Q", f.read(8))[0]
                    v = f.read(sl2).decode()
                    print(f'         [{j}] = "{v}"')
                else:
                    print(f"         (skipping type {at})")
                    break
        else:
            # Skip large arrays
            if at == 5:
                f.read(4 * ac)
            elif at == 6:
                f.read(4 * ac)
            elif at == 7:
                f.read(4 * ac)
            elif at == 8:
                f.read(1 * ac)
            elif at == 4:
                for _ in range(ac):
                    sl2 = struct.unpack("<Q", f.read(8))[0]
                    f.read(sl2)
            else:
                print(f"         (can't skip type {at}, stopping)")
                break
    else:
        print(f"  [{i:3d}] {key} = (unknown type {vt})")
        break

f.close()
print(f"\nDone: {nk} keys dumped")
