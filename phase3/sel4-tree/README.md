# `phase3/sel4-tree/` — the seL4 project tree, in version control

**Until 2026-08-09 the tree that builds the shipped JARVIS image existed on exactly one disk.**

`build_jarvis_x86.sh` does not build from this repo alone. It copies sources into an out-of-tree
seL4 checkout at `$HOME/sel4-x86` and runs `ninja` there, and it **hard-fails** if that tree or its
`jbuild/` is missing (`:91`, `:1032`) — it cannot bootstrap. Inside that tree,
`projects/jarvis-x86` was a clone of `seL4/sel4test` whose JARVIS-specific changes lived as
**uncommitted working-tree state**: no commit, no branch, no remote for the delta. `git ls-files |
grep -c jarvis-x86` in this repo returned **0**.

The consequence, stated plainly: every reproducibility claim in the project record — byte-identical
rebuilds, ESP read-back verification, the object-identity gates — rested on a **machine**, not on
version control. Had that disk failed, the deployed image would not have been reproducible, and
nothing would have said so until someone next tried to build.

This directory is that delta, committed.

---

## What the delta actually is

Upstream `sel4test` @`808ff09` carries **99 tracked files**. The JARVIS tree:

| | count |
|---|---|
| deletes | **72** |
| keeps | **27** |
| of those 27, modifies | **3** |

`99 − 72 = 27`, verified against the box. **That is the whole irreducible delta: three CMake files
and a deletion list.** Everything else under `apps/` — all 295 files across the three app dirs — is
copied in from `phase3/src` by `build_jarvis_x86.sh` at build time, so it was already in version
control by another route and was never actually missing.

### Why vendored files rather than a patch

The three modified files total ~7 KB. A `git apply` patch depends on matching upstream context and
fails in a way that is easy to mis-read; vendoring the **post-state** cannot drift. A vendored
subtree of all 27 files was considered and rejected as unnecessary — the upstream is public and
pinned, so re-fetching it is deterministic. The `diffs/` copies are kept as human-readable
documentation of *what* changed; nothing consumes them.

---

## Contents

| path | what |
|---|---|
| `manifest-pinned.xml` | `repo manifest -r` from the box — 10 projects, **all 10 pinned to 40-hex revisions** (kernel `ebbda2af…`, sel4test `808ff09…`). The manifest's own default is a moving branch; this is not. |
| `cmake-configure.txt` | the deployed `jbuild` configure settings, extracted from the box's `CMakeCache.txt`. **Note `CMAKE_HOME_DIRECTORY` is `projects/jarvis-x86`, not `projects/sel4test`** — configuring the wrong source dir produces a tree that builds and then behaves differently. |
| `jarvis-x86/deleted-paths.txt` | the 72 paths, sorted |
| `jarvis-x86/files/` | the 3 modified CMake files (post-state) + `apps/jarvis-inference/CMakeLists.txt` |
| `jarvis-x86/diffs/` | the same 3 changes as readable diffs vs upstream |
| `jarvis-x86/tracked-hashes.txt` | sha256 of **25** files as they are on the box — the 27 tracked-and-present, minus the 2 the build script rewrites. The baseline the reconstruction is checked against. Generated with `LC_ALL=C` (a locale-ordering difference produced a false MISMATCH on the first proof run). |
| `jarvis-x86/build-generated-paths.txt` | the 2 tracked files `build_jarvis_x86.sh` rewrites at build time, excluded from the baseline, with the reason |

Reconstruct with **`phase3/scripts/reconstruct_sel4_tree.sh`**.

---

## What the verification does and does not cover

`reconstruct_sel4_tree.sh` finishes by hashing every tracked, still-present file in the rebuilt
tree and diffing that against `tracked-hashes.txt`. It fails loudly on any mismatch, and it checks
the upstream revision **before** applying the delta, so a delta silently applied to the wrong base
cannot pass.

**Covered:** 25 files — that upstream is the right revision, that exactly the right 72 are gone, and
that the 3 hand-edited CMake files are byte-identical to the box. Of the 27 tracked-and-present
files, exactly 5 differ from pristine upstream (measured, not assumed): the 3 vendored here, plus
the 2 `build_jarvis_x86.sh` rewrites.

**NOT covered, and deliberately so:**

- **The `apps/` sources**, and the 2 tracked files the build script rewrites
  (`apps/sel4test-driver/src/main.c`, `apps/sel4test-driver/CMakeLists.txt`). All come from
  `phase3/src` at build time; they are version controlled already and are not part of this delta.
  Verifying them pre-build would compare a pre-build tree against a post-build one.
- **Byte-identity of the resulting binaries.** Build paths are embedded (`/home/jarvis` vs
  `/home/runner`), so a rebuilt kernel differs in bytes from the box's while matching in config.
  Config identity is the property that is checkable, and the build script's config gate checks it.
- **Whether the 72 deletions are load-bearing.** They are reproduced faithfully because that is
  what the box has. Whether the build would also succeed *without* deleting them is **untested** —
  the reconstruction is a faithful copy, not a minimisation.

## Known cruft on the box, deliberately not reproduced

The box tree carries three files this reconstruction does not, all untracked and none referenced by
any `CMakeLists.txt` (the app source lists are explicit — there is no globbing, so nothing here is
compiled):

- `apps/jarvis-inference/src/ai/test_model_scaling.c` and the `sel4test-driver` copy — dead since
  `model_scaling.{c,h}` was removed on 2026-04-17 (the dynamic-scaling ADR)
- `apps/jarvis-inference/src/main.cclear` — a typo artefact (`main.c` + `clear`)

A reconstruction not having them is correct. They are named here so a future tree-diff does not
read them as a defect.
