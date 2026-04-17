# Remove Dynamic Model Scaling — Implementation Plan

**Status:** Completed 2026-04-17 (commits 5c63836..7c72279 on master; CI run 24564285520 green; JARVIS PC QEMU smoke test PASS — self-test 5/5, zero `[SCALE]`/`[SWAP]` output)

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete the dynamic model scaling subsystem from JARVIS AI-OS. Keep the observability metric (rolling cache-miss window, renamed) and keep all five adjacent systems unchanged (SHIELD, decision cache, trust level, specialists, shared context pool). Ship single-model (Gemma 4 E2B) for 30-day stability test.

**Architecture:** Surgical removal of ~520 LOC across 11 source/test files and coordinated edits to 7+ docs. Code deletions land first as one atomic commit; doc churn is batched separately so `git bisect` stays clean. Final gate is a bare-metal QEMU smoke test on the JARVIS PC before push.

**Tech Stack:** C11 (seL4 rootserver + inference server), Bash (build scripts), YAML (GitHub Actions CI), Markdown (docs).

---

## Decision Context

From the synthesis "SYNTHESIS: Dynamic Model Scaling — Keep or Delete?" (approved by user and strategic guide, 80% confidence DELETE):

> Phase 0 measured memory savings but never measured utility. Phase 2 Pi 4 never ran tiering on hardware. The "60% memory savings" number is unproven in production. What currently exists (`model_scaling.c` + miss-rate-driven swap orchestration in `main_x86.c`) is implementation drift from the original design — the original called for a safety-ensemble (CRITICAL = primary + validator), which was never built. Five adjacent systems (SHIELD, decision cache, trust level, specialists, shared context pool) are tier-agnostic and survive deletion unchanged.

What stays:
- Rolling cache-miss window counters (useful metric, rename only)
- All other subsystems (SHIELD, decision cache, trust, specialists, context pool)
- Both models on disk (Llama 1B + Gemma 4 E2B) — only the runtime swap is removed

What is deleted:
- `phase3/src/ai/model_scaling.{c,h}` + test
- Process A scaler orchestration block in `main_x86.c` (lines 1887–2033, ~146 LOC)
- Process B `MSG_MODEL_SWAP` handler in `inference_server.c` (lines 588–630, ~42 LOC)
- `MSG_MODEL_SWAP` (0x10) + `SWAP_*` constants from `shmem_ipc.h`
- CI step for `test_model_scaling.c`
- Superseded design spec + plan under `docs/superpowers/`

**Model choice for single-model mode:** Gemma 4 E2B Q4_K_M (FAT32 8.3 filename: `GEMMA2B GUF`). Bench-off winner, 8.40/10 avg quality, 8.63 tok/s @16T, already validated on seL4 QEMU Process B.

What would trigger resurrection (captured in ADR): Pi 5 port commits, or real safety-ensemble feature work (primary + validator). Not observational "might be useful someday."

---

## Pre-Flight Checks

### Task 0: Verify how `model_scaling.c` is linked (BLOCKING — gate for all deletion tasks)

**Files touched:** None (read-only investigation).

- [ ] **Step 1: Inspect `build_jarvis_x86.sh` CMakeLists patching**

Already confirmed from this session: the script COPIES `phase3/src/ai/model_scaling.{c,h}` to `$AI_DST` (line 141 of `build_jarvis_x86.sh`, inside the `AI_FILES` array) but does **NOT** patch `CMakeLists.txt` to include `model_scaling.c` in the source list (only vga_text.c/pci.c/nvme.c/nvme_log.c/fat32.c get auto-added by sed). This means `model_scaling.c` must already be listed explicitly in the upstream `CMakeLists.txt` on the JARVIS PC — because `main_x86.c` calls `scaler_init`, `scaler_update`, `scaler_state_name`, `scaler_model_file` and the build succeeds today.

- [ ] **Step 2: SSH to JARVIS PC and read the CMakeLists.txt**

```bash
ssh jarvis 'cat ~/sel4-x86/projects/jarvis-x86/apps/sel4test-driver/CMakeLists.txt' > /tmp/cmake_sel4test_driver.txt
ssh jarvis 'cat ~/sel4-x86/projects/jarvis-x86/apps/jarvis-inference/CMakeLists.txt' > /tmp/cmake_jarvis_inference.txt
grep -n "model_scaling\|ai/" /tmp/cmake_sel4test_driver.txt
grep -n "model_scaling\|ai/" /tmp/cmake_jarvis_inference.txt
```

Expected: at least one `src/ai/model_scaling.c` entry in the sel4test-driver CMakeLists (Process A). Possibly also in jarvis-inference CMakeLists (Process B) if Process B's CMakeLists enumerates AI sources individually.

- [ ] **Step 3: Record the finding**

Write the grep output into this plan's `[[CMAKE_SURVEY]]` section (below — Task 9) so the executor knows what lines to delete. Three possible outcomes:

  - (a) Explicit list entry like `src/ai/model_scaling.c` → Task 9 edits the CMakeLists.txt to remove that line.
  - (b) Glob pattern like `file(GLOB AI_SOURCES src/ai/*.c)` → Task 9 is a no-op (deletion from filesystem is enough; glob re-runs on next cmake).
  - (c) Compiled-but-unlinked (unlikely given runtime behaviour) → Investigate further before proceeding.

**What NOT to do:** Do not proceed to Task 2 or later until the CMakeLists investigation is complete and recorded. Every downstream task depends on knowing whether CMakeLists edits are needed.

**Verification step:** Print the relevant grep output inline under `[[CMAKE_SURVEY]]` below. Confirm the executor knows exactly which CMakeLists lines (if any) to delete.

**Dependencies:** None. This is the first task.

**Estimated effort:** 10 minutes.

```
[[CMAKE_SURVEY]]  — filled in by executor 2026-04-17

sel4test-driver/CMakeLists.txt model_scaling refs:
  44:    src/ai/decision_cache.c
  45:    src/ai/cache_patterns.c
  47:    src/ai/shield.c
  49:    src/ai/model_scaling.c    ← DELETE THIS LINE
  58:    src/ai/tensor_ops.c
  (... remaining AI sources unchanged ...)
  66:    "src/ai"                  (include dir — keep)

jarvis-inference/CMakeLists.txt model_scaling refs:
  (none — PB does not link model_scaling.c; confirmed consistent with
   inference_server.c which only uses MSG_MODEL_SWAP constants from shmem_ipc.h,
   not the scaler header itself.)

Decision: (a) explicit entry — sel4test-driver/CMakeLists.txt line 49 must be
deleted in Task 9 Step 2 via ssh+sed. jarvis-inference CMakeLists needs no edit.
```

---

### Task 1: Confirm docs state and model choice (non-blocking, fast)

**Files touched:** Read-only: `CLAUDE.md`, `phase3/docs/PHASE_3_KICKOFF.md`, `phase3/docs/PHASE_3_IMPLEMENTATION_PLAN.md`.

- [ ] **Step 1: Confirm `CLAUDE.md` does NOT contain a "Pending Cleanup" section**

```bash
grep -in "pending cleanup" CLAUDE.md
```

Expected: no output (section was already removed by commit `c472077`). Do NOT attempt to remove it again in any later doc task.

- [ ] **Step 2: Confirm single-model pick is Gemma 4 E2B**

This plan hardcodes `GEMMA2B GUF` as the single-model FAT32 filename (bench-off winner). If the user has since changed their mind, substitute the new filename in Task 5 before starting.

**Verification step:** `grep -in "pending cleanup" CLAUDE.md` returns nothing. Gemma 4 E2B is still the accepted winner (no user override).

**Dependencies:** None.

**Estimated effort:** 2 minutes.

---

## Code Changes (Commit 1 — one atomic commit)

All code edits below land in a single commit. Each step below is incremental but stays unpushed and uncommitted until Task 11. This guarantees every commit in history compiles and tests green individually.

### Task 2: Remove Process A scaler orchestration block

**Files touched:**
- Modify: `phase3/src/sel4/main_x86.c:1887-2033` (delete ~146 lines)

- [ ] **Step 1: Open `main_x86.c` and confirm the block boundaries**

The block starts at the comment `/* --- Dynamic scaling evaluation --- */` on line 1887 and ends at `swap_in_progress = 0;` on line 2033 (just before `next_query:`). The block is entirely inside the main `while (1)` IPC loop.

Verify the block starts with:
```c
        /* --- Dynamic scaling evaluation --- */
        if (!swap_in_progress && miss_win_total >= 10) {
```

And ends with:
```c
                    swap_in_progress = 0;
                }
            }
        }
```

- [ ] **Step 2: Delete the entire block (lines 1887–2033)**

Replace with a single blank line. The surrounding control flow (`next_query:` label, `while (1)` loop) must remain intact.

- [ ] **Step 3: Verify no stray references remain**

```bash
grep -n "scaler_\|scaling_state\|swap_in_progress\|current_model_tier\|SCALING_" phase3/src/sel4/main_x86.c
```

Expected after this step (before Task 3): remaining refs in lines 1258 (`scaler_model_file(SCALING_IDLE)` — handled by Task 5) and lines 50, 1505–1509 (handled by Task 3).

**What NOT to do:**
- Do NOT delete the `next_query:` label that sits just below the removed block.
- Do NOT delete `miss_win_*` variables or the `miss_history[]` array — they survive (renamed in Task 4).
- Do NOT delete `model_local_ptr` yet (handled in Task 3).

**Verification step:** File compiles locally (`gcc -fsyntax-only phase3/src/sel4/main_x86.c …`) after Task 3 + Task 5 are also done. Standalone compile of `main_x86.c` as a seL4 rootserver requires the full seL4 tree; we defer compile verification to Task 22 (build on JARVIS PC).

**Dependencies:** Task 0.

**Estimated effort:** 15 minutes.

---

### Task 3: Remove Process A scaler state variables + include

**Files touched:**
- Modify: `phase3/src/sel4/main_x86.c:50` (delete `#include "model_scaling.h"`)
- Modify: `phase3/src/sel4/main_x86.c:141` (delete `static void *model_local_ptr = NULL;`)
- Modify: `phase3/src/sel4/main_x86.c:1332` (delete `model_local_ptr = model_local;`)
- Modify: `phase3/src/sel4/main_x86.c:1505-1509` (delete scaler init block)

- [ ] **Step 1: Delete the `#include "model_scaling.h"` at line 50**

Before:
```c
#include "model_scaling.h"
```

After: line removed.

- [ ] **Step 2: Delete `model_local_ptr` declaration + assignment**

Line 141 — delete:
```c
static void *model_local_ptr = NULL;  /* model vaddr for swap orchestration */
```

Line 1332 — delete:
```c
                                                    model_local_ptr = model_local;
```

(`model_local` itself is a local variable used for other purposes and must NOT be deleted.)

- [ ] **Step 3: Delete the scaler init + `swap_in_progress` + `current_model_tier` block at lines 1505–1509**

Before:
```c
    /* Dynamic model scaling */
    model_scaler_t scaler;
    scaler_init(&scaler);
    scaling_state_t current_model_tier = SCALING_IDLE;
    int swap_in_progress = 0;
```

After: entire block removed (5 lines + 1 comment line).

- [ ] **Step 4: Verify no stray references remain**

```bash
grep -n "scaler\|scaling_state\|SCALING_\|swap_in_progress\|current_model_tier\|model_scaling\|model_local_ptr" phase3/src/sel4/main_x86.c
```

Expected: only the `scaler_model_file(SCALING_IDLE)` call at line ~1258 (handled by Task 5). Everything else should be gone.

**What NOT to do:**
- Do NOT delete `nvme_model_size` or `nvme_model_loaded` — they're still used by the initial boot path and by the Process B spawn call. Only scaler-specific state goes.
- Do NOT delete `model_local` — that's the buffer pointer, not the scaler's copy.

**Verification step:** Grep returns only the single `scaler_model_file(SCALING_IDLE)` line.

**Dependencies:** Task 2.

**Estimated effort:** 10 minutes.

---

### Task 4: Rename `miss_win_*` → `cache_miss_window_*` (keep as metric)

**Files touched:**
- Modify: `phase3/src/sel4/main_x86.c:1512-1517` (declaration block)
- Modify: `phase3/src/sel4/main_x86.c:1559-1576` (cache hit/miss branches)
- Modify: `phase3/src/sel4/main_x86.c:1742-1747` (inference branch, same pattern)

- [ ] **Step 1: Update the declaration block (lines 1512–1517)**

Before:
```c
    /* Cache miss rate tracking (rolling window) */
    #define MISS_WINDOW 100
    int miss_history[MISS_WINDOW];
    memset(miss_history, 0, sizeof(miss_history));
    int miss_win_idx = 0;
    int miss_win_count = 0;
    int miss_win_total = 0;
```

After:
```c
    /* Rolling cache-miss window — metrics only, no longer feeds any scaler */
    #define CACHE_MISS_WINDOW 100
    int cache_miss_history[CACHE_MISS_WINDOW];
    memset(cache_miss_history, 0, sizeof(cache_miss_history));
    int cache_miss_window_idx = 0;
    int cache_miss_window_count = 0;
    int cache_miss_window_total = 0;
```

- [ ] **Step 2: Global rename across all three usage sites (lines 1559–1576 + 1742–1747)**

Apply the following literal renames everywhere inside `main_x86.c` (there should be exactly three call-site blocks: cache hit, cache miss, inference/miss-equivalent):

  - `miss_win_total` → `cache_miss_window_total`
  - `miss_win_count` → `cache_miss_window_count`
  - `miss_win_idx` → `cache_miss_window_idx`
  - `miss_history` → `cache_miss_history`
  - `MISS_WINDOW` → `CACHE_MISS_WINDOW`

Every `if (miss_win_total >= MISS_WINDOW) miss_win_count -= miss_history[miss_win_idx];` pattern becomes `if (cache_miss_window_total >= CACHE_MISS_WINDOW) cache_miss_window_count -= cache_miss_history[cache_miss_window_idx];`

- [ ] **Step 3: Verify no stale names remain**

```bash
grep -n "miss_win\|miss_history\|MISS_WINDOW" phase3/src/sel4/main_x86.c
```

Expected: no output.

```bash
grep -n "cache_miss_window\|cache_miss_history\|CACHE_MISS_WINDOW" phase3/src/sel4/main_x86.c
```

Expected: 3 declaration lines + ~3 hit/miss/inference blocks × ~5 refs each ≈ ~18 lines.

**What NOT to do:**
- Do NOT delete any of these variables. They are being RENAMED, not removed. The comment change (`Metrics only — no longer feeds any scaler`) must be present so a future reader understands why these counters exist but aren't read.
- Do NOT rename inside doc files or comments that describe the old swap logic — those are separate doc edits (Task 16).

**Verification step:** Both grep commands in Step 3 return expected results. Compilation defers to Task 22.

**Dependencies:** Task 2, Task 3 (order doesn't matter between Task 2/3 and Task 4, but all three must be done before compile).

**Estimated effort:** 10 minutes.

---

### Task 5: Replace `scaler_model_file(SCALING_IDLE)` with single-model constant

**Files touched:**
- Modify: `phase3/src/sel4/main_x86.c` near the top (add `#define JARVIS_MODEL_FILE ...`)
- Modify: `phase3/src/sel4/main_x86.c:1258` (replace the call)

- [ ] **Step 1: Add the model filename constant near the top of `main_x86.c`**

Just after the existing `#include` block (around line 55, after the last AI include), add:

```c
/* Single-model deployment — dynamic tiering removed 2026-04-17.
 * Gemma 4 E2B Q4_K_M is the bench-off winner (8.40/10 quality, 8.63 tok/s @16T). */
#define JARVIS_MODEL_FILE "GEMMA2B GUF"
```

- [ ] **Step 2: Replace the call at line 1258**

Before:
```c
                                        int found = fat32_find_file(&fs, scaler_model_file(SCALING_IDLE),
                                                                     &model_cluster, &model_size);
```

After:
```c
                                        int found = fat32_find_file(&fs, JARVIS_MODEL_FILE,
                                                                     &model_cluster, &model_size);
```

- [ ] **Step 3: Verify all scaler refs in `main_x86.c` are gone**

```bash
grep -n "scaler_\|model_scaling\|SCALING_\|scaling_state" phase3/src/sel4/main_x86.c
```

Expected: no output.

**What NOT to do:**
- Do NOT inline the string `"GEMMA2B GUF"` at every call site. Use the `#define` so future model changes are a one-line edit.
- Do NOT change `nvme_model_size` handling or the rest of the initial boot path. Only the filename argument changes.

**Verification step:** Grep returns no output. `#define JARVIS_MODEL_FILE` appears exactly once in `main_x86.c`.

**Dependencies:** Task 3.

**Estimated effort:** 5 minutes.

---

### Task 6: Remove Process B `MSG_MODEL_SWAP` handler

**Files touched:**
- Modify: `phase3/src/sel4/inference_server.c:588-630` (delete `case MSG_MODEL_SWAP:` block)

- [ ] **Step 1: Confirm the block boundaries**

Verify the block starts at line 588 with:
```c
            case MSG_MODEL_SWAP: {
```

And ends at line 630 with:
```c
                break;
            }
```

(Note: the closing brace on line 630 closes the `case MSG_MODEL_SWAP:` block. The outer `switch (msg_type)` remains.)

- [ ] **Step 2: Delete the entire `case MSG_MODEL_SWAP:` block (lines 588–630)**

Replace with a single blank line. The `default:` case that follows must remain.

- [ ] **Step 3: Verify no stray refs remain in Process B**

```bash
grep -n "MSG_MODEL_SWAP\|SWAP_\|scaler_\|model_scaling" phase3/src/sel4/inference_server.c
```

Expected: no output.

**What NOT to do:**
- Do NOT delete other `case MSG_*` branches (QUERY, HEARTBEAT, SHIELD_CHECK) — they survive.
- Do NOT delete `model_loaded`, `model_data`, `model_data_size`, `qm`, `state`, `tok`, `vocab`, `gguf_ctx` — they're needed for the single-model startup path.
- Do NOT delete the `pb_log`, `pb_log_num` helpers — still used elsewhere.

**Verification step:** Grep returns no output.

**Dependencies:** Task 0.

**Estimated effort:** 10 minutes.

---

### Task 7: Remove `MSG_MODEL_SWAP` + `SWAP_*` from `shmem_ipc.h`, add reserved comment

**Files touched:**
- Modify: `phase3/src/ipc/shmem_ipc.h:39-46`

- [ ] **Step 1: Replace the MSG_MODEL_SWAP block with a reserved comment**

Before (lines 39–46):
```c
#define MSG_MODEL_SWAP     0x10  /* Model hot-swap: PA <-> PB */

/* MSG_MODEL_SWAP payload commands (first byte of payload) */
#define SWAP_UNLOAD    0x01  /* PA -> PB: free current model */
#define SWAP_UNLOADED  0x02  /* PB -> PA: model freed */
#define SWAP_LOAD      0x03  /* PA -> PB: load model (payload[1..4] = uint32 size) */
#define SWAP_LOADED    0x04  /* PB -> PA: model ready */
#define SWAP_FAILED    0x05  /* PB -> PA: load failed */
```

After:
```c
/* 0x10 reserved (was MSG_MODEL_SWAP, removed 2026-04-17) */
```

- [ ] **Step 2: Verify no stray refs remain anywhere in the repo**

```bash
grep -rn "MSG_MODEL_SWAP\|SWAP_UNLOAD\|SWAP_UNLOADED\|SWAP_LOAD\|SWAP_LOADED\|SWAP_FAILED" phase3/src/ \
    --include="*.c" --include="*.h"
```

Expected: no output (all three code sites — main_x86.c, inference_server.c, shmem_ipc.h — are now clean).

**What NOT to do:**
- Do NOT remove the reserved comment. Future developers might reuse `0x10` and the comment prevents silent protocol collisions.
- Do NOT change `MSG_DEBUG = 0x0F` or any other message type.

**Verification step:** Grep across `phase3/src/` returns no output.

**Dependencies:** Task 2, Task 6 (the `.c` consumers must be gone first or the `.h` edit breaks compilation).

**Estimated effort:** 5 minutes.

---

### Task 8: Delete `model_scaling.{c,h}` + `test_model_scaling.c`

**Files touched:**
- Delete: `phase3/src/ai/model_scaling.c`
- Delete: `phase3/src/ai/model_scaling.h`
- Delete: `phase3/src/ai/test_model_scaling.c`

- [ ] **Step 1: Delete the three files**

```bash
rm phase3/src/ai/model_scaling.c
rm phase3/src/ai/model_scaling.h
rm phase3/src/ai/test_model_scaling.c
```

- [ ] **Step 2: Verify deletion**

```bash
ls phase3/src/ai/model_scaling.* phase3/src/ai/test_model_scaling.c 2>&1
```

Expected: three "No such file or directory" errors.

**What NOT to do:**
- Do NOT delete any other file under `phase3/src/ai/`. Shield, tokenizer, sampling, ggml, dequant, etc. all survive.

**Verification step:** `ls` returns errors for all three files.

**Dependencies:** Task 7 (must be gone from `shmem_ipc.h` first so no surviving source references the scaler header).

**Estimated effort:** 2 minutes.

---

### Task 9: Remove `model_scaling` from `build_jarvis_x86.sh` + JARVIS PC CMakeLists

**Files touched:**
- Modify: `phase3/scripts/build_jarvis_x86.sh:141` (delete one line from `AI_FILES` array)
- Modify (on JARVIS PC, via SSH): `~/sel4-x86/projects/jarvis-x86/apps/sel4test-driver/CMakeLists.txt` and possibly `~/sel4-x86/projects/jarvis-x86/apps/jarvis-inference/CMakeLists.txt` — depending on Task 0 finding.

- [ ] **Step 1: Delete the `model_scaling.c/h` pair from `AI_FILES`**

In `phase3/scripts/build_jarvis_x86.sh`, in the `AI_FILES=( ... )` array (around line 132–151), remove:

```
    "model_scaling.c"     "model_scaling.h"
```

The array previously listed model_scaling between `shield.*` and `llama_model.h`. After deletion the surrounding entries stay unchanged.

- [ ] **Step 2: Apply the CMakeLists edit on the JARVIS PC (per Task 0 finding)**

Based on `[[CMAKE_SURVEY]]` outcome from Task 0:

  - **(a) explicit entry:** SSH to jarvis and edit both CMakeLists.txt files to remove the `src/ai/model_scaling.c` line:
    ```bash
    ssh jarvis "sed -i '/src\/ai\/model_scaling.c/d' \
        ~/sel4-x86/projects/jarvis-x86/apps/sel4test-driver/CMakeLists.txt \
        ~/sel4-x86/projects/jarvis-x86/apps/jarvis-inference/CMakeLists.txt"
    ```
    Confirm with:
    ```bash
    ssh jarvis "grep model_scaling ~/sel4-x86/projects/jarvis-x86/apps/*/CMakeLists.txt"
    ```
    Expected: no output.

  - **(b) glob pattern:** No-op on the JARVIS PC. The next `cmake` reconfigure (triggered by `build_jarvis_x86.sh`) will re-glob and pick up the deletion automatically. Still re-run cmake manually to be safe:
    ```bash
    ssh jarvis "cd ~/sel4-x86/jbuild && cmake . >/dev/null 2>&1"
    ```

  - **(c) other:** Stop. Investigate before proceeding. Do not improvise.

- [ ] **Step 3: Verify local `build_jarvis_x86.sh` no longer references `model_scaling`**

```bash
grep -n "model_scaling" phase3/scripts/build_jarvis_x86.sh
```

Expected: no output.

**What NOT to do:**
- Do NOT remove other entries from `AI_FILES` (shield, dequant, etc. all stay).
- Do NOT commit changes made on the JARVIS PC directly — the local tree is the source of truth; JARVIS PC CMakeLists is a build-tree artifact. The sync script re-syncs every build.

**Verification step:** Local grep empty. Remote grep empty (or glob-based build script still compiles — verified at Task 22).

**Dependencies:** Task 0, Task 8.

**Estimated effort:** 10 minutes.

---

### Task 10: Stage Commit 1 — code removal

**Files touched:** Commit the edits from Tasks 2–9 (code only; CI and docs defer to later commits).

- [ ] **Step 1: Stage and review diff**

```bash
git add \
    phase3/src/sel4/main_x86.c \
    phase3/src/sel4/inference_server.c \
    phase3/src/ipc/shmem_ipc.h \
    phase3/src/ai/model_scaling.c \
    phase3/src/ai/model_scaling.h \
    phase3/src/ai/test_model_scaling.c \
    phase3/scripts/build_jarvis_x86.sh
git status
git diff --cached --stat
```

Expected: 7 files changed; 3 deletions (model_scaling*), 4 modifications (main_x86, inference_server, shmem_ipc.h, build_jarvis_x86.sh). LOC removed ≈ 400–520.

- [ ] **Step 2: Create the commit**

```bash
git commit -m "$(cat <<'EOF'
refactor: remove dynamic model scaling subsystem

Delete model_scaling.{c,h}, test_model_scaling.c, the Process A swap
orchestration block in main_x86.c (~146 LOC), the Process B
MSG_MODEL_SWAP handler (~42 LOC), and MSG_MODEL_SWAP / SWAP_* protocol
constants. Preserves the rolling cache-miss window (renamed
cache_miss_window_*) as an observability metric. Ships single-model
Gemma 4 E2B (JARVIS_MODEL_FILE).

See docs/decisions/2026-04-17-remove-dynamic-model-scaling.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**What NOT to do:**
- Do NOT include `.github/workflows/ci.yml` here — that's Commit 2.
- Do NOT include doc edits here — those are Commits 3–5.
- Do NOT push yet. Final gate is Task 22 (JARVIS PC smoke test).

**Verification step:** `git log -1 --stat` shows exactly the 7 files above.

**Dependencies:** Tasks 2–9.

**Estimated effort:** 5 minutes.

---

## Build / CI Changes (Commit 2)

### Task 11: Remove `test_model_scaling` CI step + update CLAUDE.md test count (part 1)

**Files touched:**
- Modify: `.github/workflows/ci.yml:399-405` (delete 7 lines — one step)

- [ ] **Step 1: Delete the CI step**

In `.github/workflows/ci.yml`, delete lines 399–405 (the step named `Phase 3: Model Scaling State Machine (C)`). The surrounding steps (`Phase 3: SHIELD Safety (C)` before, `Phase 3: Generation Quality (C)` after) must remain.

Before (lines 399–405):
```yaml
    - name: "Phase 3: Model Scaling State Machine (C)"
      run: |
        gcc -Wall -Werror -O2 -std=c11 \
          -I phase3/src/ai \
          phase3/src/ai/test_model_scaling.c phase3/src/ai/model_scaling.c \
          -lm -o /tmp/test_model_scaling
        /tmp/test_model_scaling
```

After: entire step removed.

- [ ] **Step 2: Verify the YAML still parses**

```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))"
```

Expected: no error.

- [ ] **Step 3: Stage and commit**

```bash
git add .github/workflows/ci.yml
git commit -m "$(cat <<'EOF'
ci: drop test_model_scaling step

The model_scaling module was removed in the prior commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**What NOT to do:**
- Do NOT edit `CLAUDE.md` test count here — that's part of Commit 4 (Task 15).
- Do NOT remove other Phase 3 CI steps.

**Verification step:** YAML parses. `git log -1 --stat` shows only `.github/workflows/ci.yml`.

**Dependencies:** Task 10.

**Estimated effort:** 5 minutes.

---

## Doc Updates Batch 1 — Superseded Notes + Specialist Clarifications (Commit 3)

### Task 12: Mark superseded design sections with `Superseded 2026-04-17` note

**Files touched:**
- Modify: `JARVIS_UNIFIED_PLAN.md` (section starting around line 158 — "### 2. Dynamic Model Scaling")
- Modify: `ARCHITECTURE_ENHANCEMENTS.md` (sections near lines 311, 314–331, 450–480, 485–492)

- [ ] **Step 1: Add superseded banner to `JARVIS_UNIFIED_PLAN.md`**

In `JARVIS_UNIFIED_PLAN.md`, find the line `### 2. Dynamic Model Scaling (Opus Innovation) ⭐` (around line 158). Insert **immediately after** that heading (before the `**Problem:**` line):

```markdown
> **Superseded 2026-04-17** — this section describes a design that was never fully built. What existed (`model_scaling.c` + miss-rate-driven swap orchestration) was removed; system now ships single-model (Gemma 4 E2B). See `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`. Text retained below for historical reference only.
```

Leave the rest of the section (lines 159–199) intact as historical reference.

- [ ] **Step 2: Add superseded banner to `ARCHITECTURE_ENHANCEMENTS.md`**

In `ARCHITECTURE_ENHANCEMENTS.md`, find `#### State 3: CRITICAL` (around line 314) — this is inside the "Enhancement 2: Dynamic Model Scaling" section. Walk up to find the enhancement heading (`## Enhancement 2: Dynamic Model Scaling` or similar, around line 280–295). Insert **immediately after** that enhancement heading, before the first subsection:

```markdown
> **Superseded 2026-04-17** — the 4-state scaler described below was never fully built. `model_scaling.c` and the runtime swap orchestration have been removed; system now ships single-model (Gemma 4 E2B). See `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`. Text retained below for historical reference only.
```

Leave lines 311, 314–331, 450–480, 485–492 intact as historical reference.

- [ ] **Step 3: Verify the banners are in place**

```bash
grep -A1 "Superseded 2026-04-17" JARVIS_UNIFIED_PLAN.md ARCHITECTURE_ENHANCEMENTS.md
```

Expected: one match per file.

**What NOT to do:**
- Do NOT delete any of the historical text below the banner. The synthesis explicitly called for "mark superseded, don't delete blindly."
- Do NOT add a superseded banner to the Decision Cache section, SHIELD section, or Shared Context Pool section — those subsystems survive.

**Verification step:** `grep` shows both banners.

**Dependencies:** None (can run in parallel with Task 11 but must land after).

**Estimated effort:** 10 minutes.

---

### Task 13: Add specialist-agent clarification note to three docs

**Files touched:**
- Modify: `CLAUDE.md` (Architecture section, near line 36: "Specialist agents (4-6)")
- Modify: `JARVIS_UNIFIED_PLAN.md` (wherever "specialist agents" first appears)
- Modify: `ARCHITECTURE_ENHANCEMENTS.md` (wherever "specialist agents" first appears)

The exact clarifying note to add, verbatim:

```
Specialist agents refer to domain-expert agents (device, network, filesystem, user) — NOT model-size tiers.
```

- [ ] **Step 1: Locate "specialist" mentions**

```bash
grep -in "specialist" CLAUDE.md JARVIS_UNIFIED_PLAN.md ARCHITECTURE_ENHANCEMENTS.md
```

Record line numbers. You will insert a short clarification near the first mention in each file.

- [ ] **Step 2: Insert the clarification in `CLAUDE.md`**

In `CLAUDE.md` around line 36, find:
```
• Specialist agents (4-6)
```

Replace with:
```
• Specialist agents (4-6) — domain-expert agents (device, network, filesystem, user), NOT model-size tiers
```

- [ ] **Step 3: Insert the clarification in `JARVIS_UNIFIED_PLAN.md`**

Find the first occurrence of "specialist agents" (grep result from Step 1). Immediately after the sentence that first mentions them, insert:

```
> Specialist agents refer to domain-expert agents (device, network, filesystem, user) — NOT model-size tiers.
```

(Blockquote format — keeps visually distinct.)

- [ ] **Step 4: Insert the clarification in `ARCHITECTURE_ENHANCEMENTS.md`**

Same as Step 3 — find first "specialist agents" mention, insert the identical blockquote clarification immediately after.

- [ ] **Step 5: Verify**

```bash
grep -c "domain-expert agents" CLAUDE.md JARVIS_UNIFIED_PLAN.md ARCHITECTURE_ENHANCEMENTS.md
```

Expected: `1` in each file.

**What NOT to do:**
- Do NOT reword the clarification sentence. The exact text is the contract; rewording risks re-conflation.
- Do NOT add the note to other docs (the synthesis specified exactly these three).

**Verification step:** All three files have exactly one match for "domain-expert agents".

**Dependencies:** None.

**Estimated effort:** 10 minutes.

---

### Task 14: Stage Commit 3 — Doc batch 1

- [ ] **Step 1: Stage and commit**

```bash
git add JARVIS_UNIFIED_PLAN.md ARCHITECTURE_ENHANCEMENTS.md CLAUDE.md
git status
git diff --cached --stat
git commit -m "$(cat <<'EOF'
docs: mark dynamic-scaling design sections superseded; clarify specialist agents

Superseded banners on JARVIS_UNIFIED_PLAN.md and ARCHITECTURE_ENHANCEMENTS.md
point to the ADR. Added one-line note in CLAUDE.md, JARVIS_UNIFIED_PLAN.md, and
ARCHITECTURE_ENHANCEMENTS.md stating that specialist agents are domain-expert
agents (device/network/filesystem/user), NOT model-size tiers — to prevent
future conflation.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Verification step:** `git log -1 --stat` shows the three files.

**Dependencies:** Tasks 12, 13.

**Estimated effort:** 3 minutes.

---

## Doc Updates Batch 2 — CLAUDE.md + Phase 3 Docs (Commit 4)

### Task 15: Update `CLAUDE.md`

**Files touched:**
- Modify: `CLAUDE.md` (arch section, milestone table row, "Current" paragraph "Next" line, file/test counts, `Dynamic Model Scaling` line in Architecture Enhancements)

- [ ] **Step 1: Update the "AI Decision Engine" architecture block**

Around line 38 (inside the ASCII diagram):

Before:
```
• Dynamic model scaling (1B→7B→13B)
```

After: delete this line entirely. The surrounding bullets (Decision cache, SHIELD, Shared Context Pool) remain.

- [ ] **Step 2: Update the "Architecture Enhancements" bullet list (around line 68)**

Before:
```
- **Dynamic Model Scaling:** IDLE(1B)→ACTIVE(7B)→CRITICAL(7B+validator)→EMERGENCY(rules). 60% memory savings.
```

After: delete this bullet.

- [ ] **Step 3: Update the "Current" paragraph (line 19)**

Find:
```
Next: wire dynamic scaling, 30-day stability test.
```

Replace with:
```
Next: 30-day stability test on single-model Gemma 4 E2B.
```

- [ ] **Step 4: Update the milestone table**

Grep for milestone rows mentioning scaling:
```bash
grep -n "scaling\|Dynamic Model\|SCALING_\|CRITICAL.*validator\|IDLE.*ACTIVE" CLAUDE.md
```

Identify each scaling-specific row (e.g., "Dynamic model scaling (miss rate -> scaler -> NVMe hot-swap)" and "Dynamic scaling tiers: Llama 1B (IDLE ...) ..."). Delete those rows entirely.

Update the bench-off winner row (around line 155) if present:
```
| **Bench-off winner: Gemma 4 E2B (8.40/10 avg, 7 judges, 19.7 tok/s) — needs ~1000 LOC engine work** | **DECISION** |
```

Keep this row — it's still the active decision.

Delete rows specific to CRITICAL tier or tier routing, e.g.:
```
| **CRITICAL: Mistral 7B Q8_0 (7.50/10, 5.5 tok/s) — drop-in, ships now** | **DECISION** |
```

- [ ] **Step 5: Update file / test counts at bottom of file**

Find "Phase 3: ~24,000 LOC, 90+ files, 395+ tests":

Before:
```
- **Phase 3:** ~24,000 LOC, 90+ files, 395+ tests (IN PROGRESS — **11/11 models, 6 families, qdot+threadpool**)
```

After (subtract the 12 tests from test_model_scaling.c, subtract 1 file for model_scaling.c/h pair, and update the LOC by ~200 for the deletion):
```
- **Phase 3:** ~23,500 LOC, 88+ files, 383+ tests (IN PROGRESS — **11/11 models, 6 families, qdot+threadpool**)
```

(Exact counts should be adjusted based on actual deletion. If uncertain, run `find phase3/src -name '*.c' -o -name '*.h' | wc -l` and use that.)

- [ ] **Step 6: Remove the `Dynamic Model Scaling` entry from the "Phase 3 Early Work" table**

Find the table row:
```
| Dynamic model scaling | — | DONE | model_scaling.c/h | 9 PASS |
```

Delete this row.

- [ ] **Step 7: Remove `Dynamic Model Scaling` Quick Reference entry**

Find:
```
- **Dynamic Model Scaling:** `phase3/src/ai/model_scaling.c/h`
```

Delete this line.

- [ ] **Step 8: Verify no orphan refs remain in `CLAUDE.md`**

```bash
grep -in "model_scaling\|scaler_\|SCALING_IDLE\|SCALING_ACTIVE\|SCALING_CRITICAL\|dynamic scaling\|dynamic model" CLAUDE.md
```

Expected: only the `dynamic-scaling design sections superseded` reference (if any), and nothing else related to the active codebase.

**What NOT to do:**
- Do NOT touch unrelated milestone rows (bench-off quality/speed results stay, model family support stays, etc.).
- Do NOT update the "Validated Metrics" table (those are historical facts).
- Do NOT remove the specialist-agent clarification added in Task 13.

**Verification step:** Grep in Step 8 returns nothing meaningful.

**Dependencies:** Task 11 (test count subtraction must match the CI step that was removed).

**Estimated effort:** 20 minutes.

---

### Task 16: Update Phase 3 docs

**Files touched:**
- Modify: `phase3/docs/PHASE_3_KICKOFF.md` (S3 section, lines 63–68)
- Modify: `phase3/docs/PHASE_3_IMPLEMENTATION_PLAN.md` (Week 36 section around line 1840; Model Scaling row around line 2060)
- Modify: `phase3/docs/QEMU_INFERENCE_MILESTONE.md` (scaler refs at lines 253, 276, 334)
- Modify: `phase3/docs/MODEL_BENCH_OFF_2026-04-07.md` (editorial note at §7 / lines 79–91 area)

- [ ] **Step 1: Mark S3 in `PHASE_3_KICKOFF.md` as REMOVED**

Find the block at lines 63–68:
```markdown
**S3: Dynamic Model Scaling (Production)**
Implement the full 4-state scaling design from ARCHITECTURE_ENHANCEMENTS.md with GPU acceleration:
- IDLE: 1B model (CPU, instant)
- ACTIVE: 3.8B model (GPU, ~13ms)
- CRITICAL: 7B model + validator (GPU, ~26ms)
- EMERGENCY: Deterministic rules only (<1ms)
```

Replace with:
```markdown
**S3: Dynamic Model Scaling (Production) — REMOVED 2026-04-17**
The 4-state scaler was never fully built; what existed was implementation drift (miss-rate-driven swap, not the designed safety-ensemble). System now ships single-model (Gemma 4 E2B). Success criterion changed to: single-model (Gemma 4 E2B) stable for 30 days. See `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`.
```

- [ ] **Step 2: Remove Week 36 from `PHASE_3_IMPLEMENTATION_PLAN.md`**

Find the `### Week 36: Wire Dynamic Model Scaling` heading (around line 1840) and the entire section up to (but not including) the next `### Week ...` heading.

Replace the entire section with:
```markdown
### Week 36: Wire Dynamic Model Scaling — REMOVED 2026-04-17

This week was removed. Rationale: the 4-state scaler was never fully built (what existed was miss-rate-driven swap, not the safety-ensemble design). System ships single-model (Gemma 4 E2B). See `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`.
```

- [ ] **Step 3: Remove the Model Scaling module entry from the "New Phase 3 Modules" table**

Around line 2060:
```markdown
| Model Scaling | model_scaling.c | 300-500 | 33-34 | 4-state IDLE→EMERGENCY |
```

Delete this row.

- [ ] **Step 4: Update `QEMU_INFERENCE_MILESTONE.md` references**

Grep for scaler refs:
```bash
grep -n "scaling\|model_scaling\|scaler\|SCALING\|Dynamic" phase3/docs/QEMU_INFERENCE_MILESTONE.md
```

Line ~253 (file list): remove the `model_scaling.c/h | Dynamic model scaling state machine` row.

Line ~276 (test file list): remove the `test_model_scaling.c | 8 PASS` row.

Line ~334 (milestone summary): find the "Dynamic model scaling" bullet/line and delete it.

- [ ] **Step 5: Add editorial note to `MODEL_BENCH_OFF_2026-04-07.md` §7 area**

Find the section around lines 79–91 where the bench-off results map bench winners to IDLE/ACTIVE/CRITICAL tiers. Add **at the top of the section** (not as a rewrite):

```markdown
> **Editorial note 2026-04-17** — The tier mapping below reflects the original bench-off framing. The dynamic scaling subsystem was subsequently removed; current architecture ships single-model (Gemma 4 E2B Q4_K_M). This document is retained for historical record of the bench-off data. See `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`.
```

Do NOT rewrite or delete the bench-off analysis — it is historical data and should stay intact.

- [ ] **Step 6: Verify**

```bash
grep -rn "model_scaling\|scaler_\|SCALING_" phase3/docs/
```

Expected: only inside the new editorial/removed notes that reference the ADR or describe the deletion.

**What NOT to do:**
- Do NOT rewrite `MODEL_BENCH_OFF_2026-04-07.md` — it's a dated historical document. Editorial note only.
- Do NOT delete Week 33, Week 35, or Week 37 from the implementation plan.
- Do NOT delete the model comparison table from `MODEL_BENCH_OFF`.

**Verification step:** Grep returns only expected "removed"/"superseded"/"editorial" lines.

**Dependencies:** None.

**Estimated effort:** 20 minutes.

---

### Task 17: Stage Commit 4 — Doc batch 2

- [ ] **Step 1: Stage and commit**

```bash
git add CLAUDE.md phase3/docs/PHASE_3_KICKOFF.md phase3/docs/PHASE_3_IMPLEMENTATION_PLAN.md \
    phase3/docs/QEMU_INFERENCE_MILESTONE.md phase3/docs/MODEL_BENCH_OFF_2026-04-07.md
git diff --cached --stat
git commit -m "$(cat <<'EOF'
docs: update CLAUDE.md and Phase 3 docs for scaling removal

CLAUDE.md: drop scaling architecture bullet, update Next line, milestone
table, file/test counts, Quick Reference entries.
PHASE_3_KICKOFF.md: mark S3 as REMOVED, update success criterion.
PHASE_3_IMPLEMENTATION_PLAN.md: mark Week 36 REMOVED, drop table row.
QEMU_INFERENCE_MILESTONE.md: drop scaling refs from file and test lists.
MODEL_BENCH_OFF_2026-04-07.md: editorial note on tier mapping.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**Verification step:** `git log -1 --stat` shows the five doc files.

**Dependencies:** Tasks 15, 16.

**Estimated effort:** 3 minutes.

---

## Delete Superseded Specs (Commit 5)

### Task 18: Delete the superseded dynamic-scaling spec + plan

**Files touched:**
- Delete: `docs/superpowers/specs/2026-04-16-dynamic-model-scaling-design.md`
- Delete: `docs/superpowers/plans/2026-04-16-dynamic-model-scaling.md`

- [ ] **Step 1: Delete the two files**

```bash
rm docs/superpowers/specs/2026-04-16-dynamic-model-scaling-design.md
rm docs/superpowers/plans/2026-04-16-dynamic-model-scaling.md
```

- [ ] **Step 2: Verify**

```bash
ls docs/superpowers/specs/2026-04-16-dynamic-model-scaling-design.md \
   docs/superpowers/plans/2026-04-16-dynamic-model-scaling.md 2>&1
```

Expected: two "No such file or directory" errors.

- [ ] **Step 3: Check for in-repo references to the deleted files**

```bash
grep -rn "2026-04-16-dynamic-model-scaling" . --include="*.md"
```

Expected: no output. If any reference exists, update those docs inline.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/
git commit -m "$(cat <<'EOF'
docs: delete superseded dynamic-scaling spec and plan

These documents described the subsystem that was removed in an earlier
commit of this change. Replaced by the ADR at
docs/decisions/2026-04-17-remove-dynamic-model-scaling.md.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**What NOT to do:**
- Do NOT delete other specs under `docs/superpowers/specs/` — only the `2026-04-16-dynamic-model-scaling-design.md` file.
- Do NOT delete the `2026-04-15-fused-dequant-dot.md` plan or `2026-04-13-perf-fused-dequant-dot-design.md` spec — those are about perf work, unrelated.

**Verification step:** `ls` returns errors; `grep` returns no inbound references.

**Dependencies:** None.

**Estimated effort:** 3 minutes.

---

## ADR Creation (Commit 6)

### Task 19: Create the Architecture Decision Record

**Files touched:**
- Create: `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`

- [ ] **Step 1: Create the ADR file**

Write `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md` with this exact content:

```markdown
# ADR: Remove Dynamic Model Scaling

**Date:** 2026-04-17
**Status:** Accepted
**Deciders:** JARVIS Development, strategic review

## Context

The `model_scaling.c` subsystem implemented a 4-state scaler (IDLE / ACTIVE / CRITICAL / EMERGENCY) that swapped GGUF models at runtime based on rolling cache-miss rate. The original design (documented in `ARCHITECTURE_ENHANCEMENTS.md` and `JARVIS_UNIFIED_PLAN.md`) called for CRITICAL to be a safety-ensemble of primary + validator models. That ensemble was never built. What shipped was a miss-rate-driven swap — implementation drift from the plan.

Phase 0 measured memory savings from tiering in isolation but never measured whether tier transitions actually improved decision quality on real workloads. Phase 2 Pi 4 never ran the scaler on hardware at all. The "60% memory savings" figure is unproven in production.

A deep-research synthesis in April 2026 recommended deletion with 80% confidence. The user and strategic guide endorsed the recommendation.

## Decision

Delete `model_scaling.{c,h}`, `test_model_scaling.c`, the Process A swap orchestration in `main_x86.c`, the Process B `MSG_MODEL_SWAP` handler in `inference_server.c`, and the `MSG_MODEL_SWAP` / `SWAP_*` constants in `shmem_ipc.h`. Ship single-model Gemma 4 E2B Q4_K_M.

## Rationale

1. **Drift, not feature.** What existed was not the designed system — it was a miss-rate-driven swap with no safety-ensemble. The design rationale (ensemble validation for CRITICAL ops) was not realized.
2. **Unmeasured utility.** Memory savings were measured; quality/latency improvements from tiering were not. The cost of maintaining dead code exceeds the unproven benefit.
3. **No blocking dependency.** Five adjacent systems (SHIELD, decision cache, trust level, specialist agents, shared context pool) are tier-agnostic and continue unchanged.
4. **Bench-off data supports a single winner.** Gemma 4 E2B scored 8.40/10 across 7 judges at 8.63 tok/s @16T — sufficient for the Phase 3 stability target.

## What was kept

- **Rolling cache-miss window.** Useful observability metric. Renamed `miss_win_*` → `cache_miss_window_*` with an explicit `Metrics only — no longer feeds any scaler` comment to signal intent.
- **SHIELD, decision cache, trust level, specialist agents, shared context pool.** Untouched.
- **Both models on disk.** Llama 1B and Gemma 4 E2B GGUF files remain on NVMe. Only the runtime swap is removed.

## What was removed

- `phase3/src/ai/model_scaling.{c,h}` + `test_model_scaling.c`
- Process A scaler orchestration in `main_x86.c` (~146 LOC)
- Process B `MSG_MODEL_SWAP` handler in `inference_server.c` (~42 LOC)
- `MSG_MODEL_SWAP` (0x10) and `SWAP_*` protocol constants
- `test_model_scaling` CI step
- `docs/superpowers/specs/2026-04-16-dynamic-model-scaling-design.md`
- `docs/superpowers/plans/2026-04-16-dynamic-model-scaling.md`

Protocol slot `0x10` is reserved with a comment to prevent future collision.

## What would trigger resurrection

- **Pi 5 port commits.** If the project ports to Pi 5 (smaller memory budget) and measurements show IDLE-tier savings are materially valuable under that constraint.
- **Real safety-ensemble feature work.** If a deliberate plan to build CRITICAL = primary + validator is prioritized (not drift, not observation, a funded feature).

Not triggers: generic "might be useful later," future model family support, or "we can always turn it back on."

## References

- Synthesis: internal research thread 2026-04-17 "Dynamic Model Scaling — Keep or Delete?"
- Previous design: `ARCHITECTURE_ENHANCEMENTS.md` (section marked Superseded)
- Previous plan: `JARVIS_UNIFIED_PLAN.md` §2 (section marked Superseded)
- Bench-off: `phase3/docs/MODEL_BENCH_OFF_2026-04-07.md`
```

- [ ] **Step 2: Verify the file**

```bash
wc -l docs/decisions/2026-04-17-remove-dynamic-model-scaling.md
head -5 docs/decisions/2026-04-17-remove-dynamic-model-scaling.md
```

Expected: ~50–80 lines; `# ADR: Remove Dynamic Model Scaling` as the first heading.

- [ ] **Step 3: Commit**

```bash
git add docs/decisions/2026-04-17-remove-dynamic-model-scaling.md
git commit -m "$(cat <<'EOF'
docs: add ADR for dynamic model scaling removal

Captures context, decision, rationale, what was kept, what was removed,
and what would trigger resurrection (Pi 5 port, real safety-ensemble).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

**What NOT to do:**
- Do NOT add per-task detail from this plan into the ADR. The ADR is a decision record, not an implementation log.
- Do NOT create additional ADRs for adjacent subsystems — only this one.

**Verification step:** ADR file exists at the correct path; commit landed with one file.

**Dependencies:** None.

**Estimated effort:** 10 minutes.

---

## Final Verification Before Push

### Task 20: Build and QEMU smoke test on JARVIS PC (FINAL GATE)

**Files touched:** None (verification only).

- [ ] **Step 1: Sync sources to JARVIS PC and build**

```bash
ssh jarvis 'cd ~/Desktop/JARVIS_OS && git pull' \
  || { echo "Remote repo sync failed — resolve before proceeding"; exit 1; }
# Note: if the remote hasn't fetched these unpushed commits yet, push to a
# temporary branch first, or rsync the worktree manually. Do NOT push to
# master yet — Task 22 is the push.

# Copy the local commits over via worktree rsync (one-shot, no push):
rsync -av --exclude=.git/ --exclude=build/ ./ jarvis:~/Desktop/JARVIS_OS/

# Build
ssh jarvis 'cd ~/Desktop/JARVIS_OS/phase3/scripts && bash build_jarvis_x86.sh'
```

Expected: build completes without errors. The ninja output should end with `BUILD COMPLETE`. Any undefined-symbol error on `scaler_*` or `model_scaling*` means a deletion step missed a reference — fix and rebuild.

- [ ] **Step 2: QEMU smoke test**

```bash
ssh jarvis 'cd ~/Desktop/JARVIS_OS/phase3/scripts && bash qemu_test.sh ~/nvme_test.img'
```

Expected: Process A boots, self-test 5/5 PASS, Process B spawns, Process B loads `GEMMA2B GUF` from NVMe, at least one query flows PA → PB → PA and generates coherent text. No scaler/swap log output (no `[SCALE]` or `[SWAP]` lines).

Kill QEMU after ~60 seconds of confirmed normal operation.

- [ ] **Step 3: Grep sweep for any orphan references**

```bash
grep -rn "model_scaling\|scaler_\|MSG_MODEL_SWAP\|SCALING_IDLE\|SCALING_ACTIVE\|SCALING_CRITICAL\|SCALING_EMERGENCY\|SWAP_UNLOAD\|SWAP_UNLOADED\|SWAP_LOAD\|SWAP_LOADED\|SWAP_FAILED" \
    phase3/src/ phase3/scripts/ .github/ CLAUDE.md
```

Expected: ZERO active-code matches. Matches inside `phase3/docs/` (superseded/editorial notes), the ADR, or the plan file itself are fine.

```bash
grep -rn "miss_win" phase3/src/
```

Expected: no output (all renamed to `cache_miss_window_*`).

- [ ] **Step 4: Confirm CLAUDE.md numbers are internally consistent**

```bash
find phase3/src -name '*.c' -o -name '*.h' | wc -l
```

Compare to the "Phase 3: ~23,500 LOC, 88+ files, 383+ tests" line in CLAUDE.md. Adjust if the file count is off by more than 2.

**What NOT to do:**
- Do NOT push to origin yet.
- Do NOT proceed to Task 21 if the build fails or the smoke test does not generate coherent text — fix the issue first.
- Do NOT ignore warnings from `-Wall -Werror` on new or changed files.

**Verification step:** All three greps return expected (empty or documentation-only) output. Smoke test generates coherent text.

**Dependencies:** All prior tasks.

**Estimated effort:** 30 minutes (depends on JARVIS PC build time + model load time).

---

### Task 21: Push to `master` and verify CI

- [ ] **Step 1: Push all six commits**

```bash
git log --oneline origin/master..HEAD
```

Expected: six commits, in this order (bottom → top):
  1. `refactor: remove dynamic model scaling subsystem`
  2. `ci: drop test_model_scaling step`
  3. `docs: mark dynamic-scaling design sections superseded; clarify specialist agents`
  4. `docs: update CLAUDE.md and Phase 3 docs for scaling removal`
  5. `docs: delete superseded dynamic-scaling spec and plan`
  6. `docs: add ADR for dynamic model scaling removal`

```bash
git push origin master
```

- [ ] **Step 2: Verify CI status**

```bash
sleep 30  # let GH register the push
gh run list --limit 1
```

Capture the run ID from the most recent run on `master`. Poll until completion:

```bash
gh run watch <run-id>
```

Expected: all steps green. The `test_model_scaling` step should be absent (we removed it).

- [ ] **Step 3: If CI fails, investigate and fix**

```bash
gh run view <run-id> --log-failed
```

Fix the root cause, create an additional commit, push again. Do NOT force-push or amend pushed commits.

**What NOT to do:**
- Do NOT push with `--no-verify` or `--force`.
- Do NOT skip CI verification — per CLAUDE.md Rules, CI green is mandatory.

**Verification step:** `gh run list --limit 1` shows the latest run as `completed success`.

**Dependencies:** Task 20.

**Estimated effort:** 10 minutes + CI wait time.

---

## Test Impact

### Tests removed

- `phase3/src/ai/test_model_scaling.c` — 12 test functions covering state transitions, hysteresis, threshold boundaries, and emergency lockout. No replacement is appropriate (the tested behavior no longer exists).

### Tests that should newly pass (or de-flake) by removal

- **Process B startup path.** Previously, Process B had a live `MSG_MODEL_SWAP` branch that held references to `qmodel_free`, `gguf_close`, and reload state. Its removal simplifies teardown logic and eliminates a class of race conditions where a swap mid-query could leave `model_loaded == 0` under an in-flight QUERY. No explicit new tests — the simplification shows up as clearer code paths in the bare-metal smoke test (Task 20).
- **Main IPC loop control flow.** The `swap_in_progress` gate was sometimes still set during a failed swap retry, which delayed the next cache-miss evaluation. Removing the entire orchestration block eliminates that state.

### Tests that continue unchanged

- All 11 model bench harness tests (`bench_engine.c`).
- All SHIELD, tokenizer, sampling, dequant, tensor_ops, shmem_ipc (minus the swap constants), NVMe, FAT32, PCI, and VGA tests.
- All Phase 1 and Phase 2 tests.

---

## Rollback Strategy

This plan produces six independent commits, each compiling green. Bisection is always available.

### If Task 10 (Commit 1 — code removal) is wrong

Symptom: JARVIS PC build fails on undefined symbol, or QEMU smoke test fails to generate text.

Recovery:
```bash
git reset --hard HEAD~1  # back out Commit 1
```

Re-examine the CMakeLists survey (Task 0) and main_x86.c edits. Most likely cause: a `scaler_*` call or `MSG_MODEL_SWAP` ref was missed.

### If any doc commit (Commits 3–6) is wrong

Symptom: Broken intra-repo link, misleading status table, missing specialist-agent clarification.

Recovery: Fix-forward with an additional commit. Do NOT revert — the code removal and ADR should stay landed.

### If a push is rejected or CI fails post-push (Task 21)

- Do NOT force-push.
- Investigate CI logs (`gh run view --log-failed`).
- Create a fix-forward commit and push.
- If the failure is catastrophic and blocks master, revert the six commits as a group (`git revert <sha1>..<sha6>`) and reopen this plan.

### Absolute ceiling: complete rollback

If somehow the deletion must be undone entirely:
```bash
git revert <commit-6>^..<commit-1>  # reverts all six commits in reverse order
```

This restores the scaler subsystem wholesale. The ADR will need an update explaining why rollback happened.

---

## Final Verification Checklist

Run each of these **after Task 21 completes** as one last confirmation.

- [ ] Build succeeds on JARVIS PC (`ninja` completes, images emitted).
- [ ] QEMU smoke test: Process A boots, Process B loads `GEMMA2B GUF` from NVMe, at least one coherent inference completes.
- [ ] No `[SCALE]` or `[SWAP]` log lines in QEMU output.
- [ ] GH Actions CI green on `master` (latest run).
- [ ] `grep -rn "model_scaling" phase3/src/ phase3/scripts/ .github/ CLAUDE.md` → empty.
- [ ] `grep -rn "MSG_MODEL_SWAP\|SWAP_UNLOAD\|SWAP_LOAD" phase3/src/ .github/` → empty.
- [ ] `grep -rn "miss_win" phase3/src/` → empty (all renamed).
- [ ] `grep -rn "scaler_\|SCALING_" phase3/src/ .github/` → empty.
- [ ] `ls phase3/src/ai/model_scaling.* phase3/src/ai/test_model_scaling.c 2>&1` → all three "No such file".
- [ ] `ls docs/superpowers/specs/2026-04-16-dynamic-model-scaling-design.md docs/superpowers/plans/2026-04-16-dynamic-model-scaling.md 2>&1` → both "No such file".
- [ ] ADR exists: `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`.
- [ ] Three docs (CLAUDE.md, JARVIS_UNIFIED_PLAN.md, ARCHITECTURE_ENHANCEMENTS.md) each have one instance of `domain-expert agents (device, network, filesystem, user)`.
- [ ] CLAUDE.md file count and test count reflect the deletion (decremented from previous numbers).
- [ ] CLAUDE.md "Next" line reads "30-day stability test on single-model Gemma 4 E2B" (not "wire dynamic scaling").

---

## Out of Scope

This plan deliberately does **not** do the following:

1. **Phase 4 specialist-agent design.** The specialist clarification note was added to prevent conflation, but no design or implementation work for specialist agents is included.
2. **Safety-ensemble revival.** The ADR explicitly lists the safety-ensemble as a potential resurrection trigger. Building it is a separate, future project — not this plan.
3. **Pi 5 port.** Also listed as a resurrection trigger. Not now.
4. **Changing the single-model to something other than Gemma 4 E2B.** If the user changes mind, substitute `JARVIS_MODEL_FILE` in Task 5 before starting. Post-hoc changes require a new plan.
5. **Removing Llama 1B from NVMe.** The file stays on disk (no harm; human-initiated recovery might want it).
6. **Editing Phase 1 or Phase 2 docs.** Those are historical snapshots for completed phases; scaling language there reflects past intent, not current plan.
7. **Adding new observability metrics.** The renamed `cache_miss_window_*` is retained; anything beyond is future work.
8. **Merging, reorganizing, or refactoring the remaining `main_x86.c`.** Keep the diff minimal and surgical.

---

## Estimated Total Effort

| Task | Effort |
|------|--------|
| 0. CMakeLists verification (BLOCKING) | 10 min |
| 1. Docs state + model pick confirmation | 2 min |
| 2. Delete PA scaler orchestration | 15 min |
| 3. Delete PA scaler state vars + include | 10 min |
| 4. Rename `miss_win_*` → `cache_miss_window_*` | 10 min |
| 5. Replace scaler filename call with `#define` | 5 min |
| 6. Delete PB `MSG_MODEL_SWAP` handler | 10 min |
| 7. Remove `MSG_MODEL_SWAP` + `SWAP_*` constants | 5 min |
| 8. Delete three scaler source files | 2 min |
| 9. `build_jarvis_x86.sh` + CMakeLists edit | 10 min |
| 10. Commit 1 (code removal) | 5 min |
| 11. CI step removal + Commit 2 | 5 min |
| 12. Superseded banners on 2 docs | 10 min |
| 13. Specialist clarification in 3 docs | 10 min |
| 14. Commit 3 (doc batch 1) | 3 min |
| 15. Update CLAUDE.md | 20 min |
| 16. Update 4 Phase 3 docs | 20 min |
| 17. Commit 4 (doc batch 2) | 3 min |
| 18. Delete 2 superseded specs + Commit 5 | 3 min |
| 19. Create ADR + Commit 6 | 10 min |
| 20. JARVIS PC build + QEMU smoke test | 30 min |
| 21. Push + CI verify | 10 min + CI wait |

**Total:** ~3h 30min of active work + CI wait time. Expect one 4-hour session end-to-end. If CMakeLists finding (Task 0) uncovers outcome (c), budget +1 hour for investigation.
