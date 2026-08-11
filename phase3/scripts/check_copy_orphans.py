#!/usr/bin/env python3
"""No orphan source copies — the half of the ring divergence that went unseen for four months.

THE DEFECT CLASS THIS EXISTS FOR (live 2026-03-28 -> 2026-08-10, fixed `148551a`):
`build_jarvis_x86.sh` wrote `src/ai/shmem_ipc.c` into Process B's tree and NOTHING
compiled it, while PB's CMakeLists compiled `src/ipc/shmem_ipc.c` and NOTHING wrote it.
Two halves; either one, detected, would have exposed the whole thing. The reverse half
(a CMakeLists source no one writes) fails loudly in cmake ONLY on a CLEAN tree — on the
box's long-lived tree a STALE sibling satisfies cmake silently, which is exactly how the
historical bug lived for 4.5 months. So BOTH directions are checked here, from the
committed sources of truth alone:

  inputs:  phase3/scripts/build_jarvis_x86.sh          (the copy destinations)
           phase3/sel4-tree/.../apps/*/CMakeLists.txt  (the vendored compile lists)
           the jarvis-input CMakeLists HEREDOC inside the build script itself
           phase3/src/sel4/jarvis_debug.h              (the JARVIS_CONTROL_IN gate)

  CHECK 1  DIVERGENCE (never allowlistable): a copied .c whose BASENAME the receiving
           app compiles from a DIFFERENT path. This is the exact historical shape —
           the fresh copy is dead, the compiled path is stale forever.
  CHECK 2  ORPHANS: a copied .c the receiving app does not compile at all. Legitimate
           cases are allowlisted BY EXACT PATH with a stated reason; an allowlist entry
           that is no longer an orphan FAILS as stale (the A5 completeness-gate rule).
  CHECK 3  THE REVERSE: every .c the app's CMakeLists compiles must be a copy
           destination of the script. Pre-`148551a`, PB's `src/ipc/shmem_ipc.c` fails
           exactly this.
  CHECK 4  HEADER SHADOWING (2026-08-11): no .h BASENAME may be copied to two or more
           destinations that are BOTH on the receiving app's include path (include-dir
           sets read from the vendored CMakeLists / the jarvis-input heredoc, never
           hardcoded). Two on-path copies are refreshed-identical today and one
           copy-list edit from being the 148551a stale-sibling mechanism applied to
           headers, where no CMakeLists source list can ever see it.

HOW THE COPY DESTINATIONS ARE RESOLVED (verified against the script, not assumed):
every copy is a `copy_file "SRC" "DST"` call. DST is built from plain double-quoted
variable assignments (`DEST=...`, `AI_DST="$DEST/src/ai"`, `PROC_B_DIR=...`), array
loops (`for f in "${AI_FILES[@]}"`) and literal `for f in a b c` loops (with backslash
continuations). This parser handles exactly those idioms — a tiny state machine over
assignments / array blocks / for-loops / copy_file lines / heredocs — NOT general bash.
If the script grows an idiom this cannot see, the sanity floors below fail loudly
rather than passing on a partial parse.

SCOPE, stated honestly:
- HEADERS are covered on ONE axis only (CHECK 4): duplicate copies that the SCRIPT'S
  OWN COPY LIST puts on an app's include path. A .h is included, not compiled, so
  CMakeLists source lists cannot see it — no orphan/divergence/reverse check is
  possible for headers — and a stale header ALREADY sitting in a long-lived tree from
  history is invisible to any committed-source check. The build script's `rm -f`
  teardown lines remain the guard for those. CHECK 4 narrows the recorded blind spot;
  it does not eliminate it.
- THE COMMITTED CONFIGURATION ONLY: the vendored CMakeLists are the box's
  JARVIS_CONTROL_IN=1 post-state, so this gate asserts the committed flag is 1 and
  refuses to run otherwise (at 0 the script tears down crypto/net and the vendored
  lists no longer describe the built tree).

CI runs `--self-test` first: the control (unmutated script) must PASS, then an
in-memory reconstruction of the historical defect (the shmem_ipc copies redirected
back to src/ai/) must FAIL with CHECK 1 and CHECK 3 naming the exact files, and a
reconstruction of the pre-2026-08-11 jarvis_ui_tokens.h double-copy must FAIL with
CHECK 4 naming the basename and both on-path destinations — so the gate can never go
vacuous without CI noticing.
"""

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BUILD_SCRIPT = REPO / "phase3" / "scripts" / "build_jarvis_x86.sh"
DEBUG_HDR = REPO / "phase3" / "src" / "sel4" / "jarvis_debug.h"
VENDORED_CMAKE = {
    "sel4test-driver": REPO / "phase3" / "sel4-tree" / "jarvis-x86" / "files"
    / "apps" / "sel4test-driver" / "CMakeLists.txt",
    "jarvis-inference": REPO / "phase3" / "sel4-tree" / "jarvis-x86" / "files"
    / "apps" / "jarvis-inference" / "CMakeLists.txt",
    # jarvis-input has NO vendored CMakeLists: the build script generates it from a
    # heredoc under the CONTROL_IN gate. It is parsed out of the script text below,
    # so the generated app is covered from the same committed source of truth.
}
APPS = ("sel4test-driver", "jarvis-inference", "jarvis-input")

# ── CHECK 2 allowlist: (app, relpath) -> reason ─────────────────────────────────
# Every entry is an EXACT path with a reason; a stale entry (no longer an orphan)
# fails the gate. Do NOT add entries to make a new orphan pass — a new orphan is
# the defect this gate exists for.
#
# The three groups below share one mechanism: the script reuses ONE shared file list
# (AI_FILES) for both Process A and Process B, and each app compiles its own subset.
# The uncompiled remainder are deliberate dead copies, REFRESHED on every build — the
# WRITTEN side of the mechanism, which cannot go stale. The dangerous shape (the same
# basename compiled from a DIFFERENT path) is CHECK 1 and ignores this allowlist.
# Trimming the copy lists is a build-affecting change with its own gate; it is not
# smuggled into a checker commit.
_R_PA = ("the shared AI_FILES loop copies the full module set into Process A; PA "
         "compiles its subset (inference lives in Process B). Refreshed every build; "
         "deliberately uncompiled here.")
_R_PB = ("the shared AI_FILES loop copies the full module set into Process B's "
         "src/ai/ ('Process B needs its own copy of AI modules'); PB compiles the "
         "inference subset. Refreshed every build; deliberately uncompiled here.")
_R_IPC_HANDLER = ("phase2 IPC carried alongside ring_buffer.c/dual_ring_buffer.c "
                  "(which PA does compile as legacy-compat); ipc_handler.c itself is "
                  "deliberately not in the rootserver source list.")

_PA_UNCOMPILED_AI = (
    "gguf_parser.c", "gguf_vocab.c", "llama_quant.c", "llama_load.c",
    "llama_forward.c", "inference.c", "ssm.c", "qdot.c", "threadpool_sel4.c",
)
_PB_UNCOMPILED_AI = (
    "decision_cache.c", "cache_patterns.c", "shield.c", "episodic_store.c",
    "g3_retrieval.c", "cache_growth.c", "shield_learn.c", "semantic_store.c",
    "semantic_distill.c", "action_allowlist.c", "shield_action.c", "action_audit.c",
    "km2b_trigger.c", "km2b_miss.c", "km2b_fault.c", "monitors.c", "wake.c",
    "behaviors.c", "query_shield.c", "route.c", "embed_store.c", "embed_project.c",
    "route_veto.c", "ctrl_epi_index.c", "pb_health.c", "ctrl_exit.c",
)
ORPHAN_ALLOWLIST = {}
for _f in _PA_UNCOMPILED_AI:
    ORPHAN_ALLOWLIST[("sel4test-driver", "src/ai/" + _f)] = _R_PA
for _f in _PB_UNCOMPILED_AI:
    ORPHAN_ALLOWLIST[("jarvis-inference", "src/ai/" + _f)] = _R_PB
ORPHAN_ALLOWLIST[("sel4test-driver", "src/ipc/ipc_handler.c")] = _R_IPC_HANDLER

# CHECK 3 allowlist: compiled-but-not-copied .c with a reason. Empty today — every
# compiled source is a script copy destination (main.c arrives as a RENAME of
# main_x86.c / inference_server.c / input_server.c, which the destination view
# makes transparent).
REVERSE_ALLOWLIST = {}


def fail(msg):
    print("::error::" + msg)
    return 1


def parse_control_in_flag(text):
    m = re.search(r"^#define\s+JARVIS_CONTROL_IN\s+(\S+)\s*$", text, re.M)
    return m.group(1) if m else None


def parse_build_script(text):
    """Return (copies, input_cmake_text).

    copies: sorted set of (app, relpath) for every copy_file DESTINATION that lands
    under apps/<app>/. input_cmake_text: the jarvis-input CMakeLists heredoc body.
    """
    variables = {}
    arrays = {}
    copies = set()
    input_cmake = None

    lines = text.split("\n")
    i = 0
    loop_items = None  # items bound to $f inside the current for-loop
    while i < len(lines):
        line = lines[i]

        # Heredoc: capture (for the jarvis-input CMakeLists) or skip (anything else),
        # so heredoc bodies are never misread as script statements.
        hd = re.search(r"<<-?\s*'?([A-Z_][A-Z_0-9]*)'?\s*$", line)
        if hd and "<<" in line:
            tag = hd.group(1)
            body = []
            i += 1
            while i < len(lines) and lines[i].rstrip() != tag:
                body.append(lines[i])
                i += 1
            if "CMakeLists.txt" in line and "INPUT_APP_DIR" in line:
                input_cmake = "\n".join(body)
            i += 1
            continue

        # Plain double-quoted assignments (several may share a line, ';'-separated).
        for m in re.finditer(r'(?:^|[;\s])([A-Za-z_][A-Za-z_0-9]*)="([^"]*)"', line):
            variables[m.group(1)] = m.group(2)

        # Array definition blocks: NAME=( "a" "b" ... ).
        am = re.match(r'\s*([A-Za-z_][A-Za-z_0-9]*)=\(', line)
        if am:
            name = am.group(1)
            chunk = [line.split("=(", 1)[1]]
            # Close-detection is PER LINE (comment-stripped): checking the whole
            # accumulated buffer would cut at the FIRST '#' of any interior comment
            # line and never see a ')' that follows it.
            while ")" not in chunk[-1].split("#", 1)[0]:
                i += 1
                if i >= len(lines):
                    print(f"::error::array {name} never closes — parser confused")
                    sys.exit(1)
                chunk.append(lines[i])
            items = []
            for raw in chunk:
                raw = raw.split("#", 1)[0]
                if ")" in raw:
                    raw = raw.split(")", 1)[0]
                items.extend(re.findall(r'"([^"]+)"', raw))
            arrays[name] = items
            i += 1
            continue

        # for-loops binding $f: array expansion or a literal list (with \-continuations).
        fm = re.match(r'\s*for f in (.*)$', line)
        if fm:
            rest = fm.group(1)
            while rest.rstrip().endswith("\\"):
                i += 1
                rest = rest.rstrip()[:-1] + " " + lines[i].strip()
            rest = rest.split(";", 1)[0]
            arr = re.search(r'"\$\{([A-Za-z_][A-Za-z_0-9]*)\[@\]\}"', rest)
            if arr:
                loop_items = list(arrays.get(arr.group(1), []))
                if not loop_items:
                    print(f"::error::loop over unknown/empty array {arr.group(1)}")
                    sys.exit(1)
            else:
                loop_items = [t for t in rest.split() if t not in ("do",)]
            i += 1
            continue
        if re.match(r'\s*done\b', line):
            loop_items = None

        # copy_file "SRC" "DST"
        cm = re.match(r'\s*copy_file\s+"([^"]+)"\s+"([^"]+)"', line)
        if cm:
            dst = cm.group(2)
            dsts = []
            if "$f" in dst:
                if loop_items is None:
                    print(f"::error::copy_file uses $f outside a parsed loop: {line.strip()}")
                    sys.exit(1)
                dsts = [dst.replace("$f", item) for item in loop_items]
            else:
                dsts = [dst]
            for d in dsts:
                resolved = d
                for _ in range(10):
                    expanded = re.sub(
                        r"\$\{?([A-Za-z_][A-Za-z_0-9]*)\}?",
                        lambda m: variables.get(m.group(1), m.group(0)),
                        resolved,
                    )
                    if expanded == resolved:
                        break
                    resolved = expanded
                for app in APPS:
                    marker = f"apps/{app}/"
                    if marker in resolved:
                        rel = resolved.split(marker, 1)[1]
                        # PROC_B_DIR already ends in .../jarvis-inference/src
                        copies.add((app, rel))
                        break
        i += 1

    return copies, input_cmake


def cmake_include_dirs(text, where):
    """Parse target_include_directories(...) into a deduped set of relative dirs.

    Tokens may be quoted or bare (the vendored PA list mixes both and repeats
    src/drivers); keywords and the target name are dropped. Same fail-loud
    discipline as cmake_c_sources: a missing/unterminated block is an error,
    never an empty pass.
    """
    m = re.search(r"target_include_directories\s*\(", text)
    if not m:
        print(f"::error::no target_include_directories( found in {where}")
        sys.exit(1)
    seg = text[m.end():]
    close = seg.find(")")
    if close < 0:
        print(f"::error::unterminated target_include_directories in {where}")
        sys.exit(1)
    dirs = set()
    for line in seg[:close].splitlines():
        line = line.split("#", 1)[0]
        for tok in line.split():
            tok = tok.strip('"')
            if tok in ("PRIVATE", "PUBLIC", "INTERFACE") or not tok:
                continue
            if re.match(r"[A-Za-z_][A-Za-z_0-9-]*$", tok) and "/" not in tok and tok != "src":
                # bare identifier that is not a path (the target name)
                continue
            dirs.add(tok)
    if not dirs:
        print(f"::error::parsed ZERO include dirs from {where}")
        sys.exit(1)
    return dirs


def cmake_c_sources(text, where):
    m = re.search(r"add_executable\s*\(", text)
    if not m:
        print(f"::error::no add_executable( found in {where}")
        sys.exit(1)
    seg = text[m.end():]
    close = seg.find("\n)")
    if close < 0:
        print(f"::error::unterminated add_executable in {where}")
        sys.exit(1)
    srcs = []
    for line in seg[:close].splitlines():
        line = line.split("#", 1)[0]
        srcs.extend(tok for tok in line.split() if tok.endswith(".c"))
    return srcs


def run_checks(script_text, quiet=False):
    """Run all three checks. Returns the number of failures."""
    failures = 0

    flag = parse_control_in_flag(DEBUG_HDR.read_text(encoding="utf-8"))
    if flag != "1":
        return fail(
            f"JARVIS_CONTROL_IN reads {flag!r}, not '1'. The vendored CMakeLists are "
            "the CONTROL_IN=1 post-state; at any other value the script tears down "
            "crypto/net and this gate's model no longer describes the built tree. "
            "If the flag legitimately changed, this gate needs a deliberate update.")

    copies, input_cmake = parse_build_script(script_text)
    if input_cmake is None:
        return fail("could not find the jarvis-input CMakeLists heredoc in the build script")

    compiled = {}
    for app, path in VENDORED_CMAKE.items():
        compiled[app] = cmake_c_sources(path.read_text(encoding="utf-8"), str(path))
    compiled["jarvis-input"] = cmake_c_sources(input_cmake, "jarvis-input heredoc")

    # Sanity floors: refuse to pass on a partial parse (the A5 no-vacuous-pass rule).
    c_copies = {(a, p) for (a, p) in copies if p.endswith(".c")}
    for app in APPS:
        if not any(a == app for (a, p) in c_copies):
            failures += fail(f"parsed ZERO .c copies for {app} — parser broken or script reshaped")
        if not compiled.get(app):
            failures += fail(f"parsed ZERO compiled .c for {app} — CMakeLists parse broken")
    if len(c_copies) < 40:
        failures += fail(f"only {len(c_copies)} .c copies parsed (expected ~90+ dests, ~40+ distinct) — partial parse")
    if failures:
        return failures

    if not quiet:
        for app in APPS:
            n_cp = sum(1 for (a, p) in c_copies if a == app)
            print(f"  {app}: {n_cp} copied .c, {len(compiled[app])} compiled .c")

    # CHECK 1 — divergence (never allowlistable).
    for (app, rel) in sorted(c_copies):
        base = rel.rsplit("/", 1)[-1]
        comp = compiled[app]
        if rel in comp:
            continue
        twins = [c for c in comp if c.rsplit("/", 1)[-1] == base]
        if twins:
            failures += fail(
                f"DIVERGENCE in {app}: the script copies '{rel}' but the app compiles "
                f"'{twins[0]}' — the compiled path is never refreshed and the copy is "
                "dead. This is the exact shape of the 2026-03-28..2026-08-10 shmem_ipc "
                "ring divergence (fixed 148551a).")

    # CHECK 2 — orphans (allowlist by exact path, with reasons; stale entries fail).
    orphans = set()
    for (app, rel) in sorted(c_copies):
        base = rel.rsplit("/", 1)[-1]
        comp = compiled[app]
        if rel in comp or any(c.rsplit("/", 1)[-1] == base for c in comp):
            continue
        orphans.add((app, rel))
        if (app, rel) not in ORPHAN_ALLOWLIST:
            failures += fail(
                f"ORPHAN copy in {app}: '{rel}' is copied by build_jarvis_x86.sh but "
                "appears in no CMakeLists source list of that app. Either compile it, "
                "stop copying it, or allowlist it with a reason.")
    for (app, rel), reason in sorted(ORPHAN_ALLOWLIST.items()):
        if not reason.strip():
            failures += fail(f"allowlist entry ({app}, {rel}) has no reason")
        if (app, rel) not in orphans:
            failures += fail(
                f"STALE allowlist entry: ({app}, {rel}) is no longer an orphan "
                "(now compiled, or no longer copied). Remove it — a stale allowlist "
                "is a hole waiting for a new orphan of the same name.")

    # CHECK 3 — the reverse direction: every compiled .c must be a copy destination.
    # cmake only fails loudly on a MISSING file; on a long-lived tree a stale sibling
    # satisfies it silently — the historical bug itself.
    copy_paths = {(a, p) for (a, p) in c_copies}
    for app in APPS:
        for src in compiled[app]:
            if (app, src) in copy_paths:
                continue
            if (app, src) in REVERSE_ALLOWLIST:
                continue
            failures += fail(
                f"UNWRITTEN source in {app}: CMakeLists compiles '{src}' but "
                "build_jarvis_x86.sh never copies to that path. On a long-lived tree "
                "this compiles a STALE file silently (pre-148551a shmem_ipc); on a "
                "clean tree cmake fails on it. Make the script write it, or allowlist "
                "with a reason.")

    # CHECK 4 — header shadowing (2026-08-11): no .h basename copied to two+
    # destinations that are BOTH on the app's include path. Quoted includes resolve
    # to whichever copy the search order finds first; two on-path copies are
    # refreshed-identical today but one copy-list edit away from the 148551a
    # stale-sibling mechanism — on the one file class (headers) where no CMakeLists
    # source list could ever surface it. Include dirs are read from the same
    # committed CMakeLists as the compile lists, never hardcoded.
    inc_dirs = {}
    for app, path in VENDORED_CMAKE.items():
        inc_dirs[app] = cmake_include_dirs(path.read_text(encoding="utf-8"), str(path))
    inc_dirs["jarvis-input"] = cmake_include_dirs(input_cmake, "jarvis-input heredoc")
    # Sanity floors: PA's list must show the real breadth (6 dirs today) and the
    # parse must have seen a realistic number of header copies, or the check is
    # running vacuously on a partial parse.
    h_copies = {(a, p) for (a, p) in copies if p.endswith(".h")}
    if len(inc_dirs["sel4test-driver"]) < 4:
        failures += fail(
            f"parsed only {sorted(inc_dirs['sel4test-driver'])} include dirs for "
            "sel4test-driver (expected the 6-dir set) — CMakeLists parse broken, "
            "CHECK 4 would be vacuous")
    if len(h_copies) < 20:
        failures += fail(
            f"only {len(h_copies)} .h copies parsed (expected ~60+) — partial parse, "
            "CHECK 4 would be vacuous")
    for app in APPS:
        on_path = {}
        for (a, rel) in sorted(h_copies):
            if a != app:
                continue
            d = rel.rsplit("/", 1)[0] if "/" in rel else ""
            if d in inc_dirs[app]:
                on_path.setdefault(rel.rsplit("/", 1)[-1], set()).add(rel)
        for base, dests in sorted(on_path.items()):
            if len(dests) >= 2:
                failures += fail(
                    f"HEADER SHADOWING in {app}: '{base}' is copied to "
                    f"{sorted(dests)} — two+ destinations on the app's include path "
                    f"({sorted(inc_dirs[app])}). A quoted #include resolves to "
                    "whichever copy the search order finds first, and one copy-list "
                    "edit turns identical copies into the 148551a stale-sibling "
                    "mechanism. Keep exactly ONE copy on the include path.")

    return failures


def self_test():
    """Control first, then an in-memory reconstruction of the historical defect."""
    text = BUILD_SCRIPT.read_text(encoding="utf-8")

    print("self-test control (unmutated script):")
    if run_checks(text, quiet=True) != 0:
        print("::error::self-test control FAILED — the unmutated tree does not pass")
        return 1
    print("  PASS")

    old_h = 'copy_file "$IPC3_SRC/shmem_ipc.h" "$PROC_B_DIR/ipc/shmem_ipc.h"'
    old_c = 'copy_file "$IPC3_SRC/shmem_ipc.c" "$PROC_B_DIR/ipc/shmem_ipc.c"'
    mutated = text.replace(old_h, old_h.replace("/ipc/", "/ai/")) \
                  .replace(old_c, old_c.replace("/ipc/", "/ai/"))
    if mutated == text:
        print("::error::self-test could not reconstruct the defect — the shmem_ipc "
              "copy lines moved; update the self-test anchors")
        return 1

    print("self-test mutant (shmem_ipc copies redirected to src/ai/ -- the pre-148551a state):")
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        n = run_checks(mutated, quiet=True)
    out = buf.getvalue()
    ok = (n > 0
          and "DIVERGENCE in jarvis-inference" in out
          and "src/ai/shmem_ipc.c" in out
          and "UNWRITTEN source in jarvis-inference" in out
          and "src/ipc/shmem_ipc.c" in out)
    if not ok:
        print("::error::self-test mutant did NOT fail as required. Output was:")
        print(out)
        return 1
    print(f"  FAILED AS REQUIRED ({n} findings; CHECK 1 named src/ai/shmem_ipc.c, "
          "CHECK 3 named src/ipc/shmem_ipc.c)")

    # CHECK 4 teeth: reconstruct the pre-2026-08-11 state — jarvis_ui_tokens.h copied
    # to BOTH $DEST/src/ and $DRV_DST/ (both on PA's include path). Appended at the
    # end of the script text so every variable is already bound when the parser
    # reaches it.
    mutated4 = (text + '\ncopy_file "$JARVIS_DIR/phase3/src/sel4/jarvis_ui_tokens.h" '
                       '"$DRV_DST/jarvis_ui_tokens.h"\n')
    print("self-test mutant 2 (jarvis_ui_tokens.h double-copied -- the pre-fix header duplicate):")
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        n4 = run_checks(mutated4, quiet=True)
    out4 = buf.getvalue()
    ok4 = (n4 > 0
           and "HEADER SHADOWING in sel4test-driver" in out4
           and "jarvis_ui_tokens.h" in out4
           and "src/jarvis_ui_tokens.h" in out4
           and "src/drivers/jarvis_ui_tokens.h" in out4)
    if not ok4:
        print("::error::self-test mutant 2 did NOT fail as required. Output was:")
        print(out4)
        return 1
    print(f"  FAILED AS REQUIRED ({n4} finding(s); CHECK 4 named jarvis_ui_tokens.h "
          "and both on-path destinations)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-script", type=Path, default=BUILD_SCRIPT,
                    help="alternate build script (for teeth demonstrations)")
    ap.add_argument("--self-test", action="store_true",
                    help="prove the gate catches the reconstructed historical defect")
    args = ap.parse_args()

    if args.self_test:
        rc = self_test()
        sys.exit(rc)

    text = args.build_script.read_text(encoding="utf-8")
    failures = run_checks(text)
    if failures:
        print(f"FAIL: {failures} finding(s)")
        sys.exit(1)
    print("OK: every copied .c is compiled by its app; every compiled .c is written "
          "by the script; no header basename is double-copied onto an app's include "
          "path; allowlist current (header scope is CHECK 4's copy-list axis only "
          "-- see docstring)")


if __name__ == "__main__":
    main()
