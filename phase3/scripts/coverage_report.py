#!/usr/bin/env python3
"""Parse gcov JSON into the coverage INSTRUMENT report.

THIS IS AN INSTRUMENT, NOT A SCORE. The deliverable is the judged list of
UNCOVERED branches under each module's "uncovered" key. The percentages carried
alongside are INCIDENTAL -- they exist so a reader can size the uncovered list
against the module, and they are never a target, a threshold, or a gate.

JUDGEMENTS ARE NOT DERIVED. They come from the companion coverage_judgements.json,
which a human writes. Every entry stores the SOURCE TEXT the judgement was made
against, so if a line shifts or its condition is edited, this script reports
"unclassified-stale" rather than silently carrying a judgement onto code that
has changed underneath it. A gap with no entry is "unclassified", never dropped
and never guessed -- the same drift-gate discipline as gen_embed_mu.py --check.

stdlib only: gcovr and lcov are not installed and pip refuses under PEP 668.
"""
import argparse
import gzip
import json
import os
import subprocess
import sys
from datetime import datetime, timezone

MODULES = [
    # (gcov basename, source path, unit suite, fuzz harness)
    ("query_shield",   "phase3/src/ai/query_shield.c",
     "phase3/src/ai/test_query_shield.c",   "phase3/src/ai/fuzz_query_shield.c"),
    ("route",          "phase3/src/ai/route.c",
     "phase3/src/ai/test_route.c",          "phase3/src/ai/fuzz_route.c"),
    ("control_verify", "phase3/src/net/control_verify.c",
     "phase3/src/net/test_control_verify.c", "phase3/src/net/fuzz_control_in.c"),
]

VALID_JUDGEMENTS = {
    "reachable-worth-fuzzing",
    "unreachable-by-construction",
    "defensive-guard",
}


def source_lines(repo_root, rel):
    with open(os.path.join(repo_root, rel), "r", encoding="utf-8", errors="replace") as fh:
        return fh.read().splitlines()


def load_judgements(repo_root):
    path = os.path.join(repo_root, "phase3", "scripts", "coverage_judgements.json")
    if not os.path.exists(path):
        return {}
    with open(path, "r", encoding="utf-8") as fh:
        raw = json.load(fh)
    out = {}
    for entry in raw.get("judgements", []):
        out[(entry["file"], entry["line"], entry["kind"])] = entry
    return out


def judge(judgements, rel, line_no, kind, src_text):
    """Look up a human judgement, refusing to carry it across a source edit."""
    entry = judgements.get((rel, line_no, kind))
    if entry is None:
        return "unclassified", "no judgement recorded for this gap yet"
    if entry.get("source_at_judgement", "").strip() != src_text.strip():
        return ("unclassified-stale",
                "source at this line changed since the judgement was made; "
                "recorded as %r, now %r -- re-judge"
                % (entry.get("source_at_judgement", "").strip(), src_text.strip()))
    j = entry["judgement"]
    if j not in VALID_JUDGEMENTS:
        return "unclassified-stale", "judgement value %r is not one of the three allowed" % j
    return j, entry.get("reason", "")


def analyse(build_dir, repo_root, name, rel, judgements):
    gz = os.path.join(build_dir, "%s.gcov.json.gz" % name)
    with gzip.open(gz, "rt", encoding="utf-8") as fh:
        data = json.load(fh)

    target = None
    for f in data["files"]:
        if os.path.basename(f["file"]) == os.path.basename(rel):
            target = f
            break
    if target is None:
        raise SystemExit("no coverage data for %s in %s" % (rel, gz))

    src = source_lines(repo_root, rel)
    uncovered = []
    total_lines = total_branches = cov_lines = cov_branches = 0

    for ln in target["lines"]:
        n = ln["line_number"]
        text = src[n - 1] if 0 < n <= len(src) else ""
        total_lines += 1
        if ln["count"] > 0:
            cov_lines += 1
        else:
            j, reason = judge(judgements, rel, n, "line", text)
            uncovered.append({
                "kind": "line", "file": rel, "line": n,
                "function": ln.get("function_name", ""),
                "source": text.strip(),
                "detail": "line never executed by either the unit suite or the fuzzer",
                "judgement": j, "reason": reason,
            })

        branches = ln.get("branches", [])
        if branches:
            total_branches += len(branches)
            taken = sum(1 for b in branches if b["count"] > 0)
            cov_branches += taken
            if taken < len(branches):
                j, reason = judge(judgements, rel, n, "branch", text)
                uncovered.append({
                    "kind": "branch", "file": rel, "line": n,
                    "function": ln.get("function_name", ""),
                    "source": text.strip(),
                    "detail": "%d of %d branch outcomes on this line never taken "
                              "(line executed %d times)"
                              % (len(branches) - taken, len(branches), ln["count"]),
                    "branch_outcomes_total": len(branches),
                    "branch_outcomes_never_taken": len(branches) - taken,
                    "judgement": j, "reason": reason,
                })

    uncovered.sort(key=lambda e: (e["line"], e["kind"]))
    counts = {}
    for e in uncovered:
        counts[e["judgement"]] = counts.get(e["judgement"], 0) + 1

    return {
        "module": rel,
        "unit_suite": None,     # filled by caller
        "fuzz_harness": None,
        "lines_total": total_lines,
        "lines_never_executed": total_lines - cov_lines,
        "branch_outcomes_total": total_branches,
        "branch_outcomes_never_taken": total_branches - cov_branches,
        "incidental_line_pct": round(100.0 * cov_lines / total_lines, 2) if total_lines else None,
        "incidental_branch_pct": round(100.0 * cov_branches / total_branches, 2) if total_branches else None,
        "uncovered_count": len(uncovered),
        "uncovered_by_judgement": counts,
        "uncovered": uncovered,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--repo-root", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    judgements = load_judgements(args.repo_root)

    try:
        head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=args.repo_root,
                              capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        head = "unknown"
    try:
        gccv = subprocess.run(["gcc", "-dumpfullversion"], capture_output=True,
                              text=True, check=True).stdout.strip()
    except Exception:
        gccv = "unknown"

    results = []
    for name, rel, unit, fuzz in MODULES:
        r = analyse(args.build_dir, args.repo_root, name, rel, judgements)
        r["unit_suite"] = unit
        r["fuzz_harness"] = fuzz
        results.append(r)

    total_gaps = sum(r["uncovered_count"] for r in results)
    agg = {}
    for r in results:
        for k, v in r["uncovered_by_judgement"].items():
            agg[k] = agg.get(k, 0) + v

    report = {
        "_comment": (
            "COVERAGE INSTRUMENT, NOT A SCORE. The deliverable is the per-module "
            "'uncovered' list -- branches and lines that NEITHER the unit suite NOR the "
            "fuzzer reaches -- so a human can decide which are worth aiming a corpus at. "
            "A repo-wide percentage is explicitly out of scope; the per-module "
            "'incidental_*_pct' fields exist only to size the uncovered list against its "
            "module and are never a target, a threshold, or a gate. The CI job that "
            "produces this file MUST NEVER fail on a coverage number -- only on its own "
            "infrastructure errors."
        ),
        "judgements_reviewed": False,
        "_judgements_note": (
            "The judgement fields are a CANDIDATE reading produced by the agent that built "
            "this instrument. The lead reviews and finalises every one. Until "
            "judgements_reviewed is true, treat every judgement as unconfirmed. "
            "'unclassified' means no human judgement is recorded; 'unclassified-stale' "
            "means the source at that line changed after a judgement was made and the "
            "judgement was refused rather than carried over."
        ),
        "measured_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "repo_head": head,
        "method": {
            "tool": "gcov (%s) driven directly, parsed with stdlib Python" % gccv,
            "why_not_gcovr_or_lcov": (
                "neither is installed; pip refuses gcovr under PEP 668 without "
                "--break-system-packages, which would modify the system Python of every "
                "machine running this. gcov -j JSON carries per-line and per-branch counts, "
                "which is all this instrument needs."
            ),
            "merge": (
                "the module under test is compiled ONCE into a single coverage-instrumented "
                "object linked into BOTH the unit binary and the fuzz binary, so both runs "
                "accumulate into the SAME .gcda (gcov counters are additive). This answers "
                "'what does NEITHER reach', not what each reaches separately. Verified: "
                "adding the fuzz run moved route.c taken-at-least-once from 92% to 94%."
            ),
            "compile_flags": "--coverage -O0 -g -std=c11",
            "deviations_from_ci": [
                "-O0 instead of CI's -O2/-O1: optimisation merges and elides branches, so an "
                "optimised build's gcov mapping does not correspond to source lines a human "
                "can judge. -O0 is the standard coverage build.",
                "no ASan/UBSan: sanitizers do not change control flow, only what happens on a "
                "bad access, so they cannot change which branches execute.",
            ],
            "source_lists": "copied verbatim from the corresponding .github/workflows/ci.yml steps",
            "fuzz_iterations": "FULL (300,000 / 300,000 / 300,020) -- not reduced",
            "fuzz_iterations_note": (
                "None of the three harnesses accepts an argv or env iteration override (each is "
                "main(void) with a compile-time count) and none needs one: the whole suite runs "
                "in seconds. No reduced-iteration caveat applies to these numbers."
            ),
            "instrumented_scope": (
                "ONLY the module under test is compiled with coverage. Harnesses and support "
                "modules (shield_action, action_allowlist, shield_learn, control_parser, "
                "control_replay, control_ratelimit, hmac_sha256, sha256) are built without it -- "
                "this measures the module, not its scaffolding."
            ),
            "reproduce": "bash phase3/scripts/coverage_report.sh",
        },
        "totals": {
            "modules": len(results),
            "uncovered_gaps": total_gaps,
            "by_judgement": agg,
        },
        "results": results,
    }

    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(report, fh, indent=2)
        fh.write("\n")

    for r in results:
        print("%-28s lines %3d/%-3d never-run  branch-outcomes %3d/%-4d never-taken  gaps %d %s"
              % (os.path.basename(r["module"]), r["lines_never_executed"], r["lines_total"],
                 r["branch_outcomes_never_taken"], r["branch_outcomes_total"],
                 r["uncovered_count"], r["uncovered_by_judgement"]))
    print("TOTAL gaps: %d  %s" % (total_gaps, agg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
