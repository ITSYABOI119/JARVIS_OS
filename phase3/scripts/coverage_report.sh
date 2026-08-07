#!/usr/bin/env bash
# Coverage INSTRUMENT for the three fuzz-target modules.
#
# THIS IS AN INSTRUMENT, NOT A SCORE. It produces a judged list of UNCOVERED
# branches so a human can decide which are worth aiming a fuzz corpus at. It
# deliberately does not compute or gate on a repo-wide percentage. The per-module
# percentages that appear in the output are INCIDENTAL context for reading the
# uncovered list, never a target and never a pass/fail criterion.
#
# THE QUESTION IT ANSWERS: "what does NEITHER the unit suite NOR the fuzzer
# reach?" -- not what each reaches separately. The merge is structural: the
# module under test is compiled ONCE into a single coverage-instrumented object
# that is linked into BOTH the unit binary and the fuzz binary, so both runs
# accumulate into the SAME .gcda (gcov counters are additive). Verified: adding
# the fuzz run moved route.c's taken-at-least-once from 92% to 94%.
#
# TOOLING: gcov directly (gcc 13.3.0 ships it). gcovr/lcov are NOT installed and
# pip refuses to install gcovr under PEP 668 without --break-system-packages, so
# taking a dependency would mean modifying the system Python of every machine
# that runs this. gcov's own -j JSON output carries per-line and per-branch
# counts, which is all this instrument needs, and stdlib Python parses it.
#
# TWO DELIBERATE DEVIATIONS from the CI build flags, both stated in the report:
#   -O0 instead of -O2/-O1 -- optimisation merges and elides branches, so an
#       optimised build's gcov line/branch mapping does not correspond to source
#       lines a human can judge. -O0 is the standard coverage build.
#   no ASan/UBSan -- sanitizers do not change control flow, only what happens
#       when a bad access occurs, so they cannot change which branches execute.
#       Dropping them keeps the run fast without affecting the measurement.
# The SOURCE LISTS are otherwise copied verbatim from the CI steps, so this
# measures the same code CI builds.
#
# Fuzzers run their FULL iteration counts (300,000 / 300,000 / 300,020). None of
# the three harnesses takes an argv or env iteration override -- each is
# main(void) with a compile-time count -- and none needs one: the whole suite
# runs in seconds. No reduced-iteration caveat applies to this report.
#
# Usage:  bash phase3/scripts/coverage_report.sh [--outdir DIR]
# Output: phase3/scripts/coverage_report.json
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${TMPDIR:-/tmp}/jarvis_coverage"
OUT_JSON="${REPO_ROOT}/phase3/scripts/coverage_report.json"

while [ $# -gt 0 ]; do
    case "$1" in
        --outdir) BUILD_DIR="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

cd "$REPO_ROOT"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

CFLAGS_COV=(--coverage -O0 -g -std=c11)

echo "=== coverage instrument: gcc $(gcc -dumpversion), gcov $(gcov --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
echo "=== build dir: $BUILD_DIR"

# ---------------------------------------------------------------------------
# build_module <name> <module.c> <include-flags...> -- compile the module under
# test ONCE with coverage. Everything else (harnesses, support modules) is built
# WITHOUT coverage: we are measuring the module, not its test scaffolding.
# ---------------------------------------------------------------------------
build_module() {
    local name="$1" src="$2"; shift 2
    gcc "${CFLAGS_COV[@]}" "$@" -c "$src" -o "${BUILD_DIR}/${name}.o"
}

echo
echo "--- query_shield.c"
build_module query_shield phase3/src/ai/query_shield.c -I phase3/src/ai
# unit suite -- source list verbatim from CI step "Phase 6: 6-5/M3-1 Query SHIELD (C)"
gcc --coverage -O0 -g -std=c11 -I phase3/src/ai \
    phase3/src/ai/test_query_shield.c "${BUILD_DIR}/query_shield.o" \
    phase3/src/ai/shield_action.c phase3/src/ai/action_allowlist.c \
    phase3/src/ai/shield_learn.c -o "${BUILD_DIR}/t_qs"
# fuzzer -- source list verbatim from CI step "...Query SHIELD Fuzz (C, ASAN)" (ASan dropped, see header)
gcc --coverage -O0 -g -std=c11 -I phase3/src/ai \
    phase3/src/ai/fuzz_query_shield.c "${BUILD_DIR}/query_shield.o" \
    -lm -o "${BUILD_DIR}/fuzz_qs"
"${BUILD_DIR}/t_qs"    > "${BUILD_DIR}/t_qs.log"    2>&1 || { echo "FAIL: t_qs";    tail -20 "${BUILD_DIR}/t_qs.log";    exit 1; }
"${BUILD_DIR}/fuzz_qs" > "${BUILD_DIR}/fuzz_qs.log" 2>&1 || { echo "FAIL: fuzz_qs"; tail -20 "${BUILD_DIR}/fuzz_qs.log"; exit 1; }
tail -1 "${BUILD_DIR}/t_qs.log"; tail -1 "${BUILD_DIR}/fuzz_qs.log"

echo
echo "--- route.c"
build_module route phase3/src/ai/route.c -I phase3/src/ai
# unit suite -- verbatim from CI step "Phase 6: 6-6/B0 Query router (C)"
gcc --coverage -O0 -g -std=c11 -I phase3/src/ai \
    phase3/src/ai/test_route.c "${BUILD_DIR}/route.o" -o "${BUILD_DIR}/t_route"
# fuzzer -- verbatim from CI step "...Query router Fuzz (C, ASAN)" (ASan dropped)
gcc --coverage -O0 -g -std=c11 -I phase3/src/ai \
    phase3/src/ai/fuzz_route.c "${BUILD_DIR}/route.o" -o "${BUILD_DIR}/fuzz_route"
"${BUILD_DIR}/t_route"    > "${BUILD_DIR}/t_route.log"    2>&1 || { echo "FAIL: t_route";    tail -20 "${BUILD_DIR}/t_route.log";    exit 1; }
"${BUILD_DIR}/fuzz_route" > "${BUILD_DIR}/fuzz_route.log" 2>&1 || { echo "FAIL: fuzz_route"; tail -20 "${BUILD_DIR}/fuzz_route.log"; exit 1; }
tail -1 "${BUILD_DIR}/t_route.log"; tail -1 "${BUILD_DIR}/fuzz_route.log"

echo
echo "--- control_verify.c"
build_module control_verify phase3/src/net/control_verify.c -I phase3/src/net -I phase3/src/crypto
# unit suite -- verbatim from CI step "Phase 6: 6-5/M1 Control verify orchestrator (C)"
gcc --coverage -O0 -g -std=c11 -I phase3/src/net -I phase3/src/crypto \
    phase3/src/net/test_control_verify.c "${BUILD_DIR}/control_verify.o" \
    phase3/src/net/control_parser.c phase3/src/net/control_replay.c \
    phase3/src/net/control_ratelimit.c \
    phase3/src/crypto/hmac_sha256.c phase3/src/crypto/sha256.c -o "${BUILD_DIR}/t_cv"
# fuzzer -- verbatim from CI step "...Control-IN Fuzz (C, ASAN)" (ASan dropped)
gcc --coverage -O0 -g -std=c11 -I phase3/src/net -I phase3/src/crypto \
    phase3/src/net/fuzz_control_in.c "${BUILD_DIR}/control_verify.o" \
    phase3/src/net/control_parser.c phase3/src/net/control_replay.c \
    phase3/src/net/control_ratelimit.c \
    phase3/src/crypto/hmac_sha256.c phase3/src/crypto/sha256.c -lm -o "${BUILD_DIR}/fuzz_ci"
"${BUILD_DIR}/t_cv"      > "${BUILD_DIR}/t_cv.log"      2>&1 || { echo "FAIL: t_cv";      tail -20 "${BUILD_DIR}/t_cv.log";      exit 1; }
"${BUILD_DIR}/fuzz_ci"   > "${BUILD_DIR}/fuzz_ci.log"   2>&1 || { echo "FAIL: fuzz_ci";   tail -20 "${BUILD_DIR}/fuzz_ci.log";   exit 1; }
tail -1 "${BUILD_DIR}/t_cv.log"; tail -1 "${BUILD_DIR}/fuzz_ci.log"

# ---------------------------------------------------------------------------
# gcov emits <name>.gcov.json.gz next to the object; the .gcda under it already
# holds the SUM of the unit run and the fuzz run.
#
# THE JSON FLAG IS VERSION-DEPENDENT and this is a real portability trap: gcc 13
# spells it -j/--json-format and lists -i as the deprecated alias, while gcc 11
# (the ubuntu-22.04 runner's default) spells it -i. Probe --help rather than
# assuming, so this runs on both the dev box and CI.
# ---------------------------------------------------------------------------
echo
echo "=== gcov"
if gcov --help 2>&1 | grep -qE '^\s*-j, --json-format'; then
    GCOV_JSON=-j
else
    GCOV_JSON=-i
fi
echo "gcov json flag: $GCOV_JSON  ($(gcov --version | head -1))"
( cd "$BUILD_DIR" && for m in query_shield route control_verify; do
    gcov -b "$GCOV_JSON" "${m}.gcno" > "${m}.gcov.txt" 2>&1
    [ -f "${m}.gcov.json.gz" ] || { echo "gcov produced no JSON for ${m}"; exit 1; }
done )
grep -h -A3 "^File .*\(query_shield\|route\|control_verify\)\.c'" "${BUILD_DIR}"/*.gcov.txt | grep -v '^--' || true

echo
echo "=== report"
python3 "${REPO_ROOT}/phase3/scripts/coverage_report.py" \
    --build-dir "$BUILD_DIR" --repo-root "$REPO_ROOT" --out "$OUT_JSON"
echo "wrote $OUT_JSON"
