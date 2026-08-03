#!/usr/bin/env python3
"""check_ci_completeness.py -- every test_*.c / fuzz_*.c has a CI step, or a stated reason.

CLAUDE.md carries the rule "When creating new test files, ALWAYS add a corresponding
step to ci.yml". That was a discipline a human had to remember. This makes it an
invariant: a new test with no CI step fails the build instead of silently never running.

SCOPE -- the COMPLETENESS direction only (every test is referenced). The inverse
(nagging about modules with no test) is deliberately NOT checked: main_x86.c and
inference_server.c are seL4-only and can never be host-compiled, so they would fail
such a gate by construction. That is backlog A9's conversation, not this gate's.

An allowlist entry is a claim that the file CANNOT run on a hermetic runner -- not
that it does not matter. Each entry carries its reason.

WHY basename matching: ci.yml invokes tests by path, but paths get reorganised and a
basename is what survives. A basename is unique across phase3/src today, and the
script FAILS LOUDLY if that ever stops being true rather than silently comparing the
wrong file (an ambiguous basename would make "referenced" meaningless).

Exit 0 = every test referenced or allowlisted. Exit 1 = orphan(s), listed.
Run: python3 phase3/scripts/check_ci_completeness.py
"""

import pathlib
import re
import subprocess
import sys
from collections import defaultdict

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
CI = REPO / ".github" / "workflows" / "ci.yml"
ALLOWLIST = REPO / ".github" / "ci_test_allowlist.txt"
TEST_RE = re.compile(r"^(test_|fuzz_).*\.c$")


def tracked_tests():
    """Tracked AND untracked-but-not-ignored test files.

    `--others --exclude-standard` matters: with `--cached` alone a brand-new test
    file is invisible until it is staged, so the gate would tell a developer
    "all good" at exactly the moment they had forgotten the CI step. In CI the two
    are equivalent (a checkout has only tracked files), so this costs nothing there
    and makes a local run honest. Found by a mutation control that a --cached-only
    enumeration silently survived.
    """
    out = subprocess.run(["git", "ls-files", "--cached", "--others",
                          "--exclude-standard", "phase3/src"], cwd=REPO,
                         capture_output=True, text=True, check=True).stdout.split()
    return sorted(set(p for p in out if TEST_RE.match(pathlib.PurePosixPath(p).name)))


def load_allowlist():
    """basename -> reason. A bare entry with no reason is itself a failure."""
    allowed, bad = {}, []
    if not ALLOWLIST.exists():
        return allowed, bad
    for raw in ALLOWLIST.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        name, _, reason = line.partition("#")
        name, reason = name.strip(), reason.strip()
        if not reason:
            bad.append(name)          # silencing the gate without saying why
        allowed[name] = reason
    return allowed, bad


def main():
    if not CI.exists():
        print("FAIL: %s not found" % CI, file=sys.stderr)
        return 1

    tests = tracked_tests()
    if not tests:
        # A glob that matches nothing would "pass" vacuously -- refuse instead.
        print("FAIL: found no test_*.c/fuzz_*.c under phase3/src -- the gate cannot "
              "be vacuously green", file=sys.stderr)
        return 1

    # Basenames must be unique, or "is it referenced?" is an ambiguous question.
    seen = defaultdict(list)
    for p in tests:
        seen[pathlib.PurePosixPath(p).name].append(p)
    dupes = {n: ps for n, ps in seen.items() if len(ps) > 1}
    if dupes:
        print("FAIL: duplicate test basenames -- basename matching is unsafe here:",
              file=sys.stderr)
        for n, ps in sorted(dupes.items()):
            print("  %s -> %s" % (n, ", ".join(ps)), file=sys.stderr)
        return 1

    ci_text = CI.read_text(encoding="utf-8")
    allowed, bad = load_allowlist()

    orphans, allowlisted = [], []
    for path in tests:
        name = pathlib.PurePosixPath(path).name
        if name in ci_text:
            continue
        (allowlisted if name in allowed else orphans).append(path)

    print("test_*.c / fuzz_*.c under phase3/src : %d" % len(tests))
    print("referenced by a step in ci.yml       : %d" % (len(tests) - len(allowlisted) - len(orphans)))
    print("allowlisted with a reason            : %d" % len(allowlisted))
    for p in allowlisted:
        print("    %-44s %s" % (p, allowed[pathlib.PurePosixPath(p).name]))

    rc = 0
    if bad:
        print("\nFAIL: allowlist entries with no reason: %s" % ", ".join(bad), file=sys.stderr)
        rc = 1

    # An allowlist entry for a file that is gone, or that IS referenced, is stale.
    live = {pathlib.PurePosixPath(p).name for p in tests}
    stale = [n for n in allowed if n not in live or (n in live and n in ci_text)]
    if stale:
        print("\nFAIL: stale allowlist entries (file gone, or now referenced in ci.yml "
              "-- delete them so the list keeps meaning something): %s"
              % ", ".join(sorted(stale)), file=sys.stderr)
        rc = 1

    if orphans:
        print("\nFAIL: %d test file(s) with no CI step and no allowlist entry:" % len(orphans),
              file=sys.stderr)
        for p in orphans:
            print("    %s" % p, file=sys.stderr)
        print("\nAdd a step to .github/workflows/ci.yml, or add the basename to "
              "%s with a reason.\nA test that never runs is not coverage."
              % ALLOWLIST.relative_to(REPO), file=sys.stderr)
        rc = 1

    if rc == 0:
        print("\nOK: every test file either runs in CI or has a stated reason not to.")
    return rc


if __name__ == "__main__":
    sys.exit(main())
