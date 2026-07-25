#!/usr/bin/env python3
"""gen_control_key.py -- generate a control-IN HMAC key + its JKEY slot image (Phase C, Main-PC).

Promotes the throwaway ~/scratch/make_ctrl_key_slot.py into a committed, tested tool.

WHY RE-KEYING EXISTS -- AND WHY IT IS NOT ROTATION (the load-bearing design decision)
------------------------------------------------------------------------------------
Re-keying here is **INCIDENT RESPONSE**: suspected compromise, lending someone access, or
before/after a demo. It is deliberately **NOT scheduled rotation**, for three reasons:

  1. The box has **no RTC**, so "every 24 hours" is not expressible on it and could not survive
     a reboot even if it were.
  2. The key **never touches the wire** -- only HMAC tags do. Rotation therefore closes no
     realistic leak path: every path that leaks today's key (a compromised Main PC, a stolen
     drive, a photographed screen) leaks tomorrow's just as easily.
  3. Any automated key-**delivery** channel built to support rotation would itself become a
     higher-value target than the static secret it replaces.

So: generate on demand, provision by hand, and keep the number of moving parts at zero.

WHAT THIS TOOL DOES AND DOES NOT DO
-----------------------------------
It emits TWO artifacts and touches nothing else:
  (a) the 512-byte JKEY slot image, which the OPERATOR dd's to the box from Ubuntu; and
  (b) the raw 32-byte key file the Main-PC receiver/launcher reads
      (the launcher default is %USERPROFILE%\\.jarvis\\control_key.bin).

**It NEVER writes to the box.** That is deliberate: the box has NO code path that writes the key
sector -- it only ever READS it, once, at boot, fail-closed (main_x86.c:5048-5054). Do not add one.

**The key is NEVER printed.** Not to stdout, not to a log, not in an error message.

FORMAT -- mirrored from phase3/src/net/control_key.h, which this script PARSES rather than
hardcodes (so a CTRL_KEY_VERSION bump cannot silently desync the generator from the box):

    offset  size  field
      0      4    magic       CTRL_KEY_MAGIC 0x4A4B4559 "JKEY", little-endian
      4      2    version     CTRL_KEY_VERSION (read from the header)
      6      2    reserved0   zero
      8     32    key         CTRL_KEY_LEN bytes from secrets.token_bytes -- NEVER random
     40      8    seq_floor   ZEROED (see below)
     48      4    boot_epoch  ZEROED (see below)
     52    460    reserved1   zero pad to exactly 512

**There is NO checksum field in this slot.** JFLR (the replay floor) and JCON (the console
address) each have one; JKEY does not. Do not add one by analogy -- the box's validation would
not check it, and a slot with an invented trailing field is just a slot with junk in reserved1.

`seq_floor` and `boot_epoch` are ZEROED on purpose. 6-5/M3-3 deliberately moved the persisted
replay floor OUT to the separate double-buffered A/B sectors at LBA 21,130,001/2, precisely so
that repeated floor writes stay OFF the key sector. Writing them here would re-couple what that
milestone decoupled.

VALIDATION THE BOX ACTUALLY PERFORMS (main_x86.c:5048-5054): it reads one sector at
CTRL_KEY_BASE_LBA, requires `ks.magic == CTRL_KEY_MAGIC`, and requires the 32-byte key to be
non-zero; on either failure it logs "[CTRL-IN] no key (fail-closed) - inert" and never calls
control_verify. Note it does NOT check `version` -- we still write the correct one, because the
field exists to make a future format change detectable. A slot from this tool satisfies both
checks by construction (correct magic; 32 cryptographically-random bytes are non-zero with
overwhelming probability).

There is **no `control_key.c`** -- the validation above is inline in main_x86.c and is seL4-only,
so unlike the M3-4b signer there is no host-compilable C parser to differential-test against. The
layout test (test_gen_control_key.py) is therefore Python-side against offsets derived from the
header, and the final proof is OPERATIONAL: provision, boot, and see "[CTRL-IN] key loaded - armed".

Run:
  py -3 phasec/scripts/gen_control_key.py               # writes both artifacts, refuses to clobber
  py -3 phasec/scripts/gen_control_key.py --force       # replaces an existing key (see the warning)
"""
import argparse
import os
import re
import secrets
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
HEADER = HERE.parent.parent / "phase3" / "src" / "net" / "control_key.h"

SLOT_BYTES = 512


def parse_header(path=HEADER):
    """Read the #defines from control_key.h. Parsed, never hardcoded, so a version bump here
    cannot silently desync from the box."""
    try:
        src = path.read_text(encoding="utf-8")
    except OSError as e:
        raise SystemExit("FATAL: cannot read %s (%s)" % (path, e))

    def d(name, base=0):
        m = re.search(r"#define\s+%s\s+([0-9A-Fa-fxX]+)u?" % name, src)
        if not m:
            raise SystemExit("FATAL: %s not found in %s" % (name, path))
        return int(m.group(1), base)

    return {
        "magic": d("CTRL_KEY_MAGIC"),
        "version": d("CTRL_KEY_VERSION"),
        "key_len": d("CTRL_KEY_LEN"),
        "base_lba": d("CTRL_KEY_BASE_LBA"),
    }


def build_slot(key, hdr):
    """Little-endian, packed, exactly SLOT_BYTES. Mirrors ctrl_key_slot_t field-for-field."""
    if len(key) != hdr["key_len"]:
        raise ValueError("key must be exactly %d bytes" % hdr["key_len"])
    slot = struct.pack(
        "<IHH%dsQI" % hdr["key_len"],
        hdr["magic"],        # magic
        hdr["version"],      # version
        0,                   # reserved0[2]
        key,                 # key[CTRL_KEY_LEN]
        0,                   # seq_floor  -- ZEROED (M3-3 moved the floor to 21,130,001/2)
        0,                   # boot_epoch -- ZEROED
    )
    slot += b"\x00" * (SLOT_BYTES - len(slot))   # reserved1 pad
    assert len(slot) == SLOT_BYTES, len(slot)
    return slot


def write_fail_closed(path, data, force, label):
    """Refuse to clobber without --force: silently regenerating breaks the channel and the
    previous key is UNRECOVERABLE (there is no escrow, by design)."""
    path = Path(path)
    if path.exists() and not force:
        raise SystemExit(
            "REFUSING to overwrite existing %s:\n  %s\n"
            "Re-keying is irreversible — the current key cannot be recovered, and the box will\n"
            "reject every frame signed with it until the new slot is dd'd over. Pass --force if\n"
            "that is what you intend." % (label, path)
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    # O_BINARY is REQUIRED on Windows: without it os.open uses TEXT mode and translates every
    # 0x0A byte to 0x0D 0x0A, silently corrupting the key file and the slot image. ~12% of random
    # 32-byte keys contain a 0x0A, so this fails intermittently, not obviously — the flake the
    # layout test caught. Same class as the seL4_DebugPutChar CR/LF corruption in CLAUDE.md:
    # never hand binary to a text-mode channel. getattr keeps it a no-op on POSIX.
    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC | getattr(os, "O_BINARY", 0)
    fd = os.open(str(path), flags, 0o600)
    try:
        written = 0
        while written < len(data):                # os.write may write short; loop to completion
            written += os.write(fd, data[written:])
    finally:
        os.close(fd)
    if path.stat().st_size != len(data):          # fail loudly rather than ship a bad artifact
        raise SystemExit("FATAL: %s is %d bytes, expected %d — refusing to continue"
                         % (path, path.stat().st_size, len(data)))
    return path


def main(argv=None):
    hdr = parse_header()
    default_dir = Path(os.path.expanduser("~")) / ".jarvis"
    ap = argparse.ArgumentParser(
        description="Generate a control-IN HMAC key + its JKEY slot image (incident response, not rotation)")
    ap.add_argument("--key-out", default=str(default_dir / "control_key.bin"),
                    help="raw %d-byte key for the Main PC (launcher default)" % hdr["key_len"])
    ap.add_argument("--slot-out", default=str(default_dir / "jkey_slot.bin"),
                    help="512-byte JKEY slot image to dd onto the box")
    ap.add_argument("--force", action="store_true",
                    help="replace existing artifacts (IRREVERSIBLE — see the refusal message)")
    args = ap.parse_args(argv)

    key = secrets.token_bytes(hdr["key_len"])     # NEVER `random` — this is key material
    slot = build_slot(key, hdr)

    # Fail closed BEFORE writing either artifact, so a refusal never leaves a half-provisioned
    # pair (a new key file with the old slot on the box would silently break the channel).
    for p, label in ((args.key_out, "key file"), (args.slot_out, "slot image")):
        if Path(p).exists() and not args.force:
            write_fail_closed(p, b"", False, label)   # raises with the explanatory message

    key_path = write_fail_closed(args.key_out, key, args.force, "key file")
    slot_path = write_fail_closed(args.slot_out, slot, args.force, "slot image")

    # NOTE: the key itself is never printed — only paths, sizes, and the provisioning command.
    print("wrote key file : %s (%d bytes, mode 0600)" % (key_path, hdr["key_len"]))
    print("wrote slot     : %s (%d bytes)" % (slot_path, SLOT_BYTES))
    print("")
    print("Provision the box (BOOT IT INTO UBUNTU FIRST — never while JARVIS is running):")
    print("  sudo dd if=%s of=/dev/nvme0n1 bs=512 seek=%d count=1 conv=fsync"
          % (slot_path, hdr["base_lba"]))
    print("")
    print("Then boot JARVIS and confirm: [CTRL-IN] key loaded - armed")
    print("(the fail-closed alternative is: [CTRL-IN] no key (fail-closed) - inert)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
