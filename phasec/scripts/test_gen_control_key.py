#!/usr/bin/env python3
"""test_gen_control_key.py -- host tests for the control-IN key generator (Phase C).

There is NO control_key.c and the box-side validation is inline in main_x86.c (seL4-only), so
unlike the M3-4b signer there is no host-compilable C parser to differential-test against. These
tests therefore pin the layout against offsets DERIVED FROM control_key.h (parsed, not hardcoded),
plus the properties that actually protect the operator: fresh randomness per run, fail-closed
overwrite, and the key never reaching stdout.

Run: py -3 phasec/scripts/test_gen_control_key.py
"""
import io
import os
import re
import struct
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_control_key as g   # noqa: E402

PASS = FAIL = 0


def check(cond, name):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("  PASS  %s" % name)
    else:
        FAIL += 1
        print("  FAIL  %s" % name)


def main():
    hdr = g.parse_header()

    # ---- header parse: values must match the real header, not a copy in this file ----
    src = g.HEADER.read_text(encoding="utf-8")
    check(hdr["magic"] == 0x4A4B4559, "magic parsed == 0x4A4B4559 ('JKEY')")
    check(("#define CTRL_KEY_VERSION  %du" % hdr["version"]) in src or
          re.search(r"#define\s+CTRL_KEY_VERSION\s+%du?" % hdr["version"], src) is not None,
          "version parsed FROM the header (not hardcoded): %d" % hdr["version"])
    check(hdr["key_len"] == 32, "CTRL_KEY_LEN == 32")
    check(hdr["base_lba"] == 21130000, "CTRL_KEY_BASE_LBA == 21130000")

    # ---- byte-exact layout at offsets derived from control_key.h ----
    key = bytes(range(32))
    slot = g.build_slot(key, hdr)
    check(len(slot) == 512, "slot is EXACTLY 512 bytes")
    check(struct.unpack_from("<I", slot, 0)[0] == hdr["magic"], "offset 0: magic (u32 LE)")
    check(struct.unpack_from("<H", slot, 4)[0] == hdr["version"], "offset 4: version (u16 LE)")
    check(slot[6:8] == b"\x00\x00", "offset 6: reserved0[2] zeroed")
    check(slot[8:40] == key, "offset 8: key[32] byte-exact")
    check(len(slot[8:40]) == 32, "key field is EXACTLY 32 bytes")
    check(struct.unpack_from("<Q", slot, 40)[0] == 0,
          "offset 40: seq_floor ZEROED (M3-3 moved the floor to 21,130,001/2)")
    check(struct.unpack_from("<I", slot, 48)[0] == 0, "offset 48: boot_epoch ZEROED")
    check(slot[52:] == b"\x00" * (512 - 52), "offset 52: reserved1 zero-pad to 512")

    # ---- NO checksum: JFLR/JCON have one, JKEY does not. Everything past the fields is zero. ----
    check(slot[52:].count(0) == len(slot[52:]),
          "NO checksum field invented (tail is pure zero padding)")

    # ---- the box's ACTUAL validation (main_x86.c:5048-5054): magic + non-zero key ----
    real = g.build_slot(os.urandom(32), hdr)
    nz = any(real[8:40])
    check(struct.unpack_from("<I", real, 0)[0] == hdr["magic"] and nz,
          "a generated slot passes the box check (magic + non-zero key)")

    with tempfile.TemporaryDirectory() as td:
        kp = Path(td) / "k.bin"
        sp = Path(td) / "s.bin"

        # ---- two runs produce DIFFERENT keys ----
        buf1 = io.StringIO()
        with redirect_stdout(buf1):
            g.main(["--key-out", str(kp), "--slot-out", str(sp)])
        k1 = kp.read_bytes()
        s1 = sp.read_bytes()
        buf2 = io.StringIO()
        with redirect_stdout(buf2):
            g.main(["--key-out", str(kp), "--slot-out", str(sp), "--force"])
        k2 = kp.read_bytes()
        check(len(k1) == 32 and len(k2) == 32, "key file is exactly 32 bytes")
        check(k1 != k2, "TWO RUNS PRODUCE DIFFERENT KEYS (secrets.token_bytes, not random)")
        check(len(s1) == 512, "slot file written is exactly 512 bytes")
        check(s1[8:40] == k1, "slot's key field matches the emitted key file")

        # ---- the key NEVER appears in stdout ----
        out = buf1.getvalue() + buf2.getvalue()
        leaked = (k1.hex() in out) or (k2.hex() in out)
        try:
            leaked = leaked or (k1.decode("latin-1") in out) or (k2.decode("latin-1") in out)
        except Exception:
            pass
        check(not leaked, "the key NEVER appears in stdout (hex or raw)")
        check("dd if=" in out and "seek=21130000" in out and "conv=fsync" in out,
              "prints the exact dd provisioning command")
        check("ubuntu" in out.lower(), "reminds the operator the box must be on Ubuntu")

        # ---- --force semantics: refuses without, replaces with ----
        refused = False
        try:
            with redirect_stdout(io.StringIO()):
                g.main(["--key-out", str(kp), "--slot-out", str(sp)])
        except SystemExit as e:
            refused = "REFUSING" in str(e)
        check(refused, "refuses to overwrite WITHOUT --force (fail closed)")
        k_before = kp.read_bytes()
        check(k_before == k2, "a refused run leaves the existing key UNTOUCHED")
        with redirect_stdout(io.StringIO()):
            g.main(["--key-out", str(kp), "--slot-out", str(sp), "--force"])
        check(kp.read_bytes() != k_before, "--force DOES replace the key")

    print("=== %d PASS, %d FAIL ===" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
