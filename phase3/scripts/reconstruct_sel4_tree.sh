#!/bin/bash
# ============================================================================
# reconstruct_sel4_tree.sh — rebuild the seL4 project tree that builds the
# shipped JARVIS image, from version control alone.
#
# WHY THIS EXISTS. Until 2026-08-09 the tree existed on exactly ONE DISK. Its
# JARVIS-specific customization lived as *uncommitted working-tree state* in a
# clone with no remote for the delta — not in this repo, not in any commit, not
# pushed anywhere. Every reproducibility claim in the project record
# (byte-identical rebuilds, ESP read-back verification, object-identity gates)
# therefore rested on a machine rather than on version control. If that disk had
# died, the deployed image would not have been reproducible, and nothing would
# have said so until someone tried.
#
# WHAT THE DELTA ACTUALLY IS. Upstream sel4test @808ff09 carries 99 tracked
# files. The JARVIS tree DELETES 72 of them, KEEPS 27, and MODIFIES 3 of those
# 27. That is the whole irreducible delta: three CMake files plus a deletion
# list. Everything else under apps/ is copied in from phase3/src by
# build_jarvis_x86.sh at build time, so it was never missing in the first place.
#
# WHAT THIS SCRIPT DOES NOT DO. It does not build. It produces the tree that
# build_jarvis_x86.sh expects, then stops. Run that script afterwards.
#
# Usage:
#   bash phase3/scripts/reconstruct_sel4_tree.sh [--tree DIR] [--verify-only]
#     --tree DIR      target (default: $HOME/sel4-x86)
#     --verify-only   skip fetching; just re-check an existing tree against the
#                     committed hash baseline
# ============================================================================
set -euo pipefail

: "${HOME:=$(getent passwd "$(id -u)" | cut -d: -f6)}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="$(cd "$SCRIPT_DIR/../sel4-tree" && pwd)"
TREE="$HOME/sel4-x86"
VERIFY_ONLY=0
MANIFEST_URL="https://github.com/seL4/sel4test-manifest.git"
SEL4TEST_URL="https://github.com/seL4/sel4test.git"
SEL4TEST_REV="808ff09469cfac9dfa84e329164473e323d142c2"

while [ $# -gt 0 ]; do
    case "$1" in
        --tree)        TREE="$2"; shift 2 ;;
        --verify-only) VERIFY_ONLY=1; shift ;;
        -h|--help)     sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "ERROR: unknown argument '$1'"; exit 2 ;;
    esac
done

say()  { printf '\n=== %s ===\n' "$1"; }
die()  { printf 'ERROR: %s\n' "$1" >&2; exit 1; }

JX="$TREE/projects/jarvis-x86"

# ---------------------------------------------------------------------------
# 1. upstream: the pinned manifest, fetched by `repo`
# ---------------------------------------------------------------------------
if [ "$VERIFY_ONLY" -eq 0 ]; then
    say "1/5  upstream tree (pinned manifest)"
    command -v repo >/dev/null || die "the 'repo' tool is not installed (apt-get install repo)"
    mkdir -p "$TREE"
    ( cd "$TREE"
      if [ ! -d .repo ]; then repo init -q -u "$MANIFEST_URL"; fi
      # Pin every project. The manifest's own default is a moving branch; the
      # committed XML is `repo manifest -r` output taken from the box, so the
      # revisions are the ones the deployed image was actually built from.
      cp "$DATA_DIR/manifest-pinned.xml" .repo/manifests/default.xml
      repo sync -q -j"$(nproc)" --no-clone-bundle )
    for p in kernel projects/sel4test projects/seL4_libs projects/musllibc; do
        [ -d "$TREE/$p" ] || die "repo sync did not produce $p"
    done

    # ---------------------------------------------------------------------
    # 2. projects/jarvis-x86 — a SEPARATE clone of sel4test at the same rev.
    #    Cloned fresh rather than copied from projects/sel4test: a repo-managed
    #    checkout's .git is an indirection into .repo, so copying it would give
    #    two working trees sharing one git dir.
    # ---------------------------------------------------------------------
    say "2/5  projects/jarvis-x86 (sel4test @${SEL4TEST_REV:0:7})"
    # REFUSE to run against a LIVE build tree. Step 2 ends in `git clean -fd`,
    # which would delete every untracked file — i.e. exactly the JARVIS sources
    # build_jarvis_x86.sh copies in. On the box the live tree sits at the DEFAULT
    # path this script targets ($HOME/sel4-x86), so an unguarded run there would
    # destroy it. This script is for building a tree from nothing, never for
    # refreshing one in use.
    if [ -e "$JX/apps/jarvis-inference/src" ] || [ -e "$JX/apps/sel4test-driver/src/ai" ]; then
        die "$JX looks like a LIVE build tree (apps/*/src present).
       This script would 'git clean -fd' those sources away. Refusing.
       Use --tree with a fresh path, or delete that tree deliberately first."
    fi
    if [ ! -d "$JX/.git" ]; then
        git clone -q "$SEL4TEST_URL" "$JX"
    fi
    git -C "$JX" fetch -q origin "$SEL4TEST_REV" 2>/dev/null || git -C "$JX" fetch -q origin
    git -C "$JX" checkout -q --force "$SEL4TEST_REV"
    git -C "$JX" clean -qfd
fi

[ -d "$JX/.git" ] || die "no tree at $JX — run without --verify-only first"

# ---------------------------------------------------------------------------
# 3. Verify we are patching the base we think we are, BEFORE touching anything.
#    A delta applied to the wrong upstream is the failure this guard exists for:
#    it would still "succeed" and produce a tree nobody has ever built.
# ---------------------------------------------------------------------------
say "3/5  verify upstream base"
have_rev="$(git -C "$JX" rev-parse HEAD)"
[ "$have_rev" = "$SEL4TEST_REV" ] || die "jarvis-x86 is at $have_rev, expected $SEL4TEST_REV"
echo "  HEAD $have_rev  OK"

# ---------------------------------------------------------------------------
# 4. apply the delta: 72 deletions, then the 4 vendored files
# ---------------------------------------------------------------------------
if [ "$VERIFY_ONLY" -eq 0 ]; then
    say "4/5  apply the delta"
    n_del=0
    missing=""
    while IFS= read -r p || [ -n "$p" ]; do
        [ -n "$p" ] || continue
        # `-e` FOLLOWS symlinks, so a DANGLING symlink tests false and would be
        # silently skipped. Not hypothetical here: upstream sel4test @808ff09 ships
        # apps/sel4test-tests/include/test_init_data.h as a SYMLINK (mode 120000) to
        # apps/sel4test-driver/include/test_init_data.h — and BOTH are on this list.
        # The target sorts first, so by the time the loop reaches the symlink it
        # dangles. `-e || -L` is the test for "a directory entry exists".
        # Found by the count check failing 71/72 on the first CI run; keeping the
        # count is what turned a silent partial delta into a diagnosable one.
        if [ -e "$JX/$p" ] || [ -L "$JX/$p" ]; then
            rm -f "$JX/$p"; n_del=$((n_del + 1))
        else
            missing="$missing
       $p"
        fi
    done < "$DATA_DIR/jarvis-x86/deleted-paths.txt"
    expect_del=$(grep -c . "$DATA_DIR/jarvis-x86/deleted-paths.txt")
    echo "  deleted $n_del of $expect_del listed paths"
    [ "$n_del" -eq "$expect_del" ] || die "deletion count mismatch — these listed paths were not present in upstream:$missing
       The list and the pinned upstream disagree; do not proceed on a partial delta."

    # remove now-empty dirs the deletions left behind, as the box tree has none
    find "$JX" -mindepth 1 -type d -empty -not -path '*/.git/*' -delete 2>/dev/null || true

    cp "$DATA_DIR/jarvis-x86/files/CMakeLists.txt"      "$JX/CMakeLists.txt"
    cp "$DATA_DIR/jarvis-x86/files/easy-settings.cmake" "$JX/easy-settings.cmake"
    cp "$DATA_DIR/jarvis-x86/files/settings.cmake"      "$JX/settings.cmake"
    mkdir -p "$JX/apps/jarvis-inference"
    cp "$DATA_DIR/jarvis-x86/files/apps/jarvis-inference/CMakeLists.txt" \
       "$JX/apps/jarvis-inference/CMakeLists.txt"
    echo "  installed 4 vendored files"
fi

# ---------------------------------------------------------------------------
# 5. PROVE it — compare the tracked, still-present file set against the
#    committed baseline taken from the box. This is the whole point of the
#    exercise: a reconstruction that merely "looks right" proves nothing.
#
#    SCOPE, stated so the result is not over-read: this covers 25 TRACKED files
#    — upstream minus the 72 deletions (27 remain), minus the 2 that
#    build_jarvis_x86.sh REWRITES at build time (build-generated-paths.txt).
#    Those 2 are excluded because this baseline describes the tree BEFORE the
#    build script runs; the box's copies are post-build and legitimately differ.
#    It also does NOT cover the untracked apps/ sources, which the build script
#    copies from phase3/src and which are in version control by another route.
#
#    LC_ALL=C is load-bearing: the first CI proof run "failed" on a diff that was
#    almost entirely '.github' vs 'apps' collation ordering between two locales.
# ---------------------------------------------------------------------------
say "5/5  verify against the committed baseline"
BASE="$DATA_DIR/jarvis-x86/tracked-hashes.txt"
GENLIST="$DATA_DIR/jarvis-x86/build-generated-paths.txt"
ACTUAL="$(mktemp)"
trap 'rm -f "$ACTUAL"' EXIT
# build an anchored regex of the build-generated paths, comments stripped
GEN_RE="$(grep -v '^#' "$GENLIST" | grep . | sed 's/[.[\*^$]/\\&/g' | paste -sd'|' -)"
( cd "$JX" && LC_ALL=C git ls-files -z | while IFS= read -r -d '' f; do
      [ -f "$f" ] && sha256sum "$f"
  done | grep -vE "^[0-9a-f]+  (${GEN_RE})\$" | LC_ALL=C sort -k2 ) > "$ACTUAL"

n_base=$(wc -l < "$BASE"); n_act=$(wc -l < "$ACTUAL")
echo "  baseline: $n_base tracked+present files"
echo "  rebuilt : $n_act"
if diff -u "$BASE" "$ACTUAL" > /tmp/tree-verify.diff 2>&1; then
    echo "  MATCH — every tracked file is byte-identical to the box"
else
    echo "  MISMATCH:"; head -40 /tmp/tree-verify.diff
    die "reconstruction does not match the committed baseline"
fi

say "DONE"
cat <<EOF
Tree ready at: $TREE
Next:  configure the build dir, then build.
       The deployed configure flags are recorded in
       phase3/sel4-tree/cmake-configure.txt — note CMAKE_HOME_DIRECTORY is
       projects/jarvis-x86, NOT projects/sel4test.

  mkdir -p $TREE/jbuild && cd $TREE/jbuild
  ../init-build.sh -DPLATFORM=x86_64 -DSIMULATION=OFF -DSMP=ON -DNUM_NODES=6 \\
      -DKernelFPU=XSAVE -DKernelXSave=XSAVEOPT -DKernelXSaveFeatureSet=7 \\
      -DKernelXSaveSize=832 -DKernelFastpath=ON \\
      -DKernelDebugBuild=ON -DKernelPrinting=ON \\
      -C ../projects/jarvis-x86/settings.cmake ../projects/jarvis-x86
  bash phase3/scripts/build_jarvis_x86.sh
EOF
