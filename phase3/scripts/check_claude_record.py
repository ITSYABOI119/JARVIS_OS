#!/usr/bin/env python3
"""Invariant: every CLAUDE.md record pointer resolves, and every record heading has a pointer.

CLAUDE.md is the MAP; docs/CLAUDE_RECORD.md is the evidence LEDGER it was carved out of
(archive pass, 2026-09-04). A long CLAUDE.md row was replaced by a one-line POINTER:

    <identity> <current state> <trap sentences> Full record: docs/CLAUDE_RECORD.md §"<heading>".

The pairing is the whole safety property. If a pointer names a heading that does not exist the
evidence is unreachable; if a heading has no pointer the evidence is orphaned and a future session
will never learn it exists. Both are silent failures - nothing else in the build reads either file -
so they are checked mechanically here.

Checked from committed files alone, stdlib only (CI has no numpy):
  1. every pointer in CLAUDE.md resolves to exactly one `### <heading>` in the record
  2. every `### ` heading in the record is named by exactly one pointer in CLAUDE.md
  3. headings in the record are unique
  4. the record's `##` / `###` structure parses (every `###` sits under some `##`)

NOTE ON THE POINTER REGEX. The heading is matched up to the closing double quote and NOT to a
following period: most pointers end the sentence, but at least one (Current Status) continues with a
parenthetical, and requiring the period would silently skip it - i.e. the check would pass by not
looking. Headings themselves never contain a double quote (the move replaces one with a single quote). The
path may or may not be wrapped in backticks - both shapes occur in CLAUDE.md and both must count.

  python3 phase3/scripts/check_claude_record.py [--self-test]
"""
import os
import re
import sys

try:                     # headings carry em-dashes and arrows; a cp1252 console must not crash the gate
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CLAUDE = os.path.join(REPO, "CLAUDE.md")
RECORD = os.path.join(REPO, "docs", "CLAUDE_RECORD.md")

POINTER_RE = re.compile(r'docs/CLAUDE_RECORD\.md`?\s*§"([^"\n]+)"')

# The four families the move script builds headings from. Used to tell an ENTRY heading apart from a
# `### ` line that is part of an archived body (see headings_of).
ENTRY_PREFIXES = (
    "Quick Reference — ",
    "Flags — ",
    "Project Overview — ",
    "Current Status (Phase 3) — ",
)

# Rows whose SOURCE text in CLAUDE.md was ALREADY markup-unbalanced before the 2026-09-04 archive pass
# (verified against 8b49727: Net Stack 15 `**`, JARVIS_EMBED 169 `**`; seven lines of that file were
# odd). A verbatim copy must reproduce the source, so these two pointer rows are odd by fidelity, not
# by a bad cut. Listed explicitly, and checked for staleness, so the exemption cannot quietly outlive
# the defect it excuses.
BALANCE_EXEMPT = (
    "Quick Reference — Net Stack",
    "Flags — JARVIS_EMBED",
)


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def headings_of(record_text):
    """Every ENTRY heading, in order, with the `## ` group it sits under.

    An entry heading is a `### ` line whose text starts with one of ENTRY_PREFIXES. That test is
    needed, not cosmetic: a moved unit can itself contain `### ` lines (the archived Current Status
    body carries `### Pre-Work Tasks`, `### Phase 3 Early Work` and `### Phase 3 Weeks` verbatim), and
    those are EVIDENCE, not entries - flagging them as orphans would make the gate cry wolf forever.
    The prefixes are exactly the four families the move script constructs headings from, so an entry
    can never be missed by this filter without the move script changing too.
    """
    out, group = [], None
    for line in record_text.split("\n"):
        if line.startswith("## ") and not line.startswith("### "):
            group = line[3:].strip()
        elif line.startswith("### "):
            name = line[4:].strip()
            if name.startswith(ENTRY_PREFIXES):
                out.append((name, group))
    return out


def check(claude_text, record_text):
    """Return a list of human-readable problems; empty means pass."""
    problems = []

    heads = headings_of(record_text)
    names = [h for h, _ in heads]

    for name, group in heads:
        if group is None:
            problems.append("record heading has no enclosing '## ' group: %s" % name)

    dupes = sorted({n for n in names if names.count(n) > 1})
    for d in dupes:
        problems.append("duplicate record heading (%d times): %s" % (names.count(d), d))

    pointers = POINTER_RE.findall(claude_text)
    nameset = set(names)
    for p in pointers:
        if p not in nameset:
            problems.append("CLAUDE.md pointer names a heading that is NOT in the record: %s" % p)
        elif names.count(p) != 1:
            problems.append("pointer resolves to %d headings, expected 1: %s" % (names.count(p), p))

    # Markup balance. A pointer row copies its identity and trap sentences VERBATIM out of the record
    # entry, so a cut that lands inside a `**...**` or a backtick span leaves the row's bold name
    # unclosed and, in several observed cases, mangled - which is how the 2026-09-04 archive pass first
    # shipped. Cuts are now taken only where the running ** and backtick counts are both even; this
    # asserts the result. Two rows are exempt because their SOURCE row was already unbalanced before
    # the archive (verified against 8b49727), and copying it faithfully must reproduce that: inventing
    # a closing ** would make the copy unfaithful. The exemption is keyed to the entry actually being
    # odd, so it cannot go stale silently and cannot hide a NEW imbalance (a new one would come from a
    # balanced entry and is therefore still caught).
    entries = {}
    cur = None
    for line in record_text.split("\n"):
        if line.startswith("### "):
            cur = line[4:].strip()
            entries[cur] = []
        elif cur is not None:
            entries[cur].append(line)
    entries = {k: "\n".join(v) for k, v in entries.items()}

    for line in claude_text.split("\n"):
        if not (line.startswith("- **") or line.startswith("  - `JARVIS_")):
            continue
        m = POINTER_RE.search(line)
        if m is None:
            continue     # merely MENTIONS the record path (e.g. the Codebase Metrics line) - not a pointer
        head = m.group(1)
        src = entries.get(head, "")
        for ch, what in (("**", "bold"), ("`", "backtick")):
            if line.count(ch) % 2 == 0:
                continue
            if src and src.count(ch) % 2 == 1:
                continue     # inherited from an already-unbalanced source row - see above
            problems.append("pointer row has an odd %s count (cut inside a span?): %s"
                            % (what, (head or line[:60])))

    for head in BALANCE_EXEMPT:
        src = entries.get(head)
        if src is None:
            problems.append("balance-exemption names a heading that no longer exists: %s" % head)
        elif src.count("**") % 2 == 0 and src.count("`") % 2 == 0:
            problems.append("balance-exemption is STALE - the source row is balanced now: %s" % head)

    # At least one pointer per entry. NOT exactly one: a second pointer is a legitimate
    # cross-reference (the Control-IN MODULE INDEX row points at the Query Router entry, because the
    # eight file-map sub-bullets are markdown-attached to that row rather than to MODULE INDEX).
    for name in names:
        if pointers.count(name) == 0:
            problems.append("record heading has NO pointer in CLAUDE.md (orphaned evidence): %s" % name)

    return problems, len(pointers), len(names)


def self_test():
    """Control must PASS; then two mutants must FAIL, naming the offender."""
    claude, record = read(CLAUDE), read(RECORD)

    problems, np_, nh = check(claude, record)
    if problems:
        print("SELF-TEST: control FAILED - the real files do not pass, so a mutant proves nothing")
        for p in problems[:10]:
            print("  " + p)
        return 1
    print("SELF-TEST control: PASS (%d pointers, %d headings)" % (np_, nh))

    heads = headings_of(record)
    if not heads:
        print("SELF-TEST: no headings to mutate")
        return 1
    victim = heads[0][0]

    # mutant 1: a pointer naming a heading that does not exist
    m1 = claude.replace('§"%s"' % victim, '§"%s ZZZ-MUTANT"' % victim, 1)
    if m1 == claude:
        print("SELF-TEST: mutant 1 could not be applied (pointer text not found)")
        return 1
    p1, _, _ = check(m1, record)
    if not any("NOT in the record" in x and "ZZZ-MUTANT" in x for x in p1):
        print("SELF-TEST mutant 1 (dangling pointer): NOT DETECTED - the check is vacuous")
        return 1
    print("SELF-TEST mutant 1 (dangling pointer): detected")

    # mutant 2: a record heading with no pointer
    m2 = record.replace("### %s" % victim, "### %s ZZZ-ORPHAN" % victim, 1)
    if m2 == record:
        print("SELF-TEST: mutant 2 could not be applied (heading not found)")
        return 1
    p2, _, _ = check(claude, m2)
    if not any("NO pointer" in x and "ZZZ-ORPHAN" in x for x in p2):
        print("SELF-TEST mutant 2 (orphaned heading): NOT DETECTED - the check is vacuous")
        return 1
    print("SELF-TEST mutant 2 (orphaned heading): detected")

    # mutant 3: a pointer row cut inside a bold span (a dangling **), the defect this invariant exists
    # for. Applied to a row whose SOURCE is balanced, so the exemption cannot mask it.
    victim_line = None
    for line in claude.split("\n"):
        if "CLAUDE_RECORD.md" not in line or not line.startswith("- **"):
            continue
        m = POINTER_RE.search(line)
        if m and m.group(1) not in BALANCE_EXEMPT and line.count("**") % 2 == 0:
            victim_line = line
            break
    if victim_line is None:
        print("SELF-TEST: mutant 3 could not be applied (no balanced pointer row found)")
        return 1
    m3 = claude.replace(victim_line, victim_line + " **dangling", 1)
    p3, _, _ = check(m3, record)
    if not any("odd bold count" in x for x in p3):
        print("SELF-TEST mutant 3 (dangling ** in a pointer row): NOT DETECTED - the check is vacuous")
        return 1
    print("SELF-TEST mutant 3 (dangling ** in a pointer row): detected")

    print("SELF-TEST: PASS")
    return 0


def main():
    if "--self-test" in sys.argv:
        return self_test()

    for p in (CLAUDE, RECORD):
        if not os.path.exists(p):
            print("MISSING FILE: %s" % p)
            return 1

    problems, npointers, nheadings = check(read(CLAUDE), read(RECORD))
    if problems:
        print("FAIL: %d problem(s)" % len(problems))
        for p in problems:
            print("  " + p)
        return 1
    print("OK: %d pointers <-> %d record headings, all paired" % (npointers, nheadings))
    return 0


if __name__ == "__main__":
    sys.exit(main())
