# Phase 5 Goal #6 (Cache Growth) — Autonomous Finish Report (M2→M3→M4)

**Date:** 2026-07-03 (unattended run)
**Outcome:** ✅ **FLIPPED — `JARVIS_CACHE_GROWTH` is default-ON (deployed).** All six M3d flip-bar items were met with captured box evidence (verbatim below). The it-remembers MVP arc (#1 episodic → #2 context → #3 retrieval → #6 cache-growth) is **COMPLETE**.
**Commits (oldest→newest, all CI-green):** `cdc1aeb` (#6/M2 telemetry+console) → `38e15d2` (#6/M3a serve path) → `99419fb` (#6/M3 flip default-ON) → this docs commit.

---

## 1. What completed

| Stage | Result |
|---|---|
| **M2** telemetry + console | ✅ `reserved_i` (u16 @ offset 90) → `cache_growth_count`, `TLM_F_CACHE_GROWTH` 0x100 (flags is u16), **no size bump** (216 B/CRC@212/v3 unchanged); receiver + fixture + golden lockstep (INFER frame carries 12 + flag); console "Cache growth — learns frequent queries" Capabilities row + System "Patterns promoted" stat (flag-gated, `—` until live); e2e value-pin. Host: telemetry C **41/41**, receiver **99/99**, honesty **40/40**, logic **14/14**, e2e **22/22**. |
| **M3a** serve path | ✅ Gated READ-only `cache_lookup`-before-infer on the inference lane (the two workload lanes ask disjoint query sets — without this, promoted patterns were dead weight and hit-rate could never improve). HIT ⇒ serve + `q_hits++` + `EPI_ACT_CACHE` episodic record + skip inference; MISS ⇒ unchanged. NO insert on this path (canon D-a — the reverted #6a inserted; this only serves). `[CACHE-GROW]` gains `served=`; first-2 serves print verbatim `[CACHE-SERVE]`. |
| **M3b** coherence | ✅ **Empirical finding: episodic `resp` stores the response HEAD** (`episodic_fill` copies the first ≤256 B of `full_response[512]`, which holds the complete ≤50-token answer; the design doc's C3 "tail" wording was imprecise — the `main_x86.c` comment "NUL-terminate the response tail" refers to terminating the buffer end, not tail-storage). Both verbatim `[CACHE-SERVE]` captures are coherent multi-sentence answer heads → per the decision rule (heads → OK) the `resp_len < EPI_RESP_MAX` promotion filter was **evaluated and NOT needed**. |
| **M3c** HWM guardrail | ✅ Max `used=261` across a 283,400-query run — far under `CG_PROMOTE_HWM=409` and `CACHE_SIZE=512`. EMPTY slots survive; the SEC-024 LRU never fires; the <1 ms miss path is preserved. The **optional** hardware LRU-force was **skipped with cause**: the deterministic workload has only ~9 distinct promotable keys, so the cache cannot fill from promotion even with the HWM lifted — LRU eviction remains **host-proven** (`test_decision_cache_lru.c` 10/10) and unreachable-by-design on the box (the SYSTEM_DESIGN §7 refinement of the plan's original "LRU fires on the box" done-when wording). |
| **M3d** flip | ✅ Bar met 6/6 (see §2) → `JARVIS_CACHE_GROWTH 0→1` (`99419fb`), flip smoke green, **ESP-deployed** (checksum-pinned) + one-shot bare-metal boot (boot_id=12) + live telemetry confirm (§4). |
| **M4** docs + tag | ✅ This report + CLAUDE.md + GOAL6/PLAN/week docs. Tag **proposed, not created** (§6). |

## 2. The M3d flip bar — captured box evidence (verbatim)

Protocol: identical episodic-store state (`S1`, a 4 MiB `dd` snapshot of the store span — the only region a QEMU run mutates) for all three 1800 s KVM legs. REF = the pre-M3a `e9ac21d` flag-0 binary; OFF = the new code flag-0; ON = the new code flag-1 (transient sed, tree reset after).

| Bar item | Evidence |
|---|---|
| (1) Growth real | ON: `[CACHE-GROW] promoted=6 used=258 grow=6 hwm=409 served=0` (q=100) → `promoted=2 … grow=8` → `promoted=1 … grow=9`; thereafter `promoted=0` every tick (idempotent, no churn). |
| (2) Hit-rate improved | ON final: `[STATS] T+1402685 q=283400 hits=240769 infer=17 hb=28398 shield=14216 err=0` with **`served=42404`** — `infer` FROZEN at 17 (13 pre-promotion + 4 stragglers) while the OFF leg managed `q=200 hits=148 infer=17` in the same 1800 s. Promoted queries convert miss→hit; the ~×1,300 throughput multiple is a property of the repeat-heavy deterministic workload (honest claim: learns frequently-asked queries, serves them fast — never a universal speedup). |
| (3) Served text coherent | `[CACHE-SERVE] q="Describe the TCP three-way handshake" action="The TCP three-way handshake is a fundamental process used to establish a reliable connection between a client and a server before any actual data transmission can occur. It ensures that both parties are ready and able to communicate reliably.` · `[CACHE-SERVE] q="The seL4 microkernel is" action="The seL4 microkernel is a **formally verified, high-assurance microkernel**.` — coherent stored answer heads. |
| (4) HWM held | Max `used=261 < hwm=409 < 512` over 2,836 ticks; LRU never fired. |
| (5) Clean run | `err=0` across **283,400 queries** (the largest single-run query count this system has produced; hb=28,398 + shield=14,216 lanes clean too), `faults: 0`, coherent `[INFER]` samples. |
| (6) OFF byte-identical | OFF vs REF: **23/23 `[INFER]`**, 22 byte-identical + 1 known serial-interleave artifact (`"…handshake iWaiting for query #57"` — the PB wait line splicing, previously documented); `[STATS]` counters identical (`q=100 hits=71 infer=13` / `q=200 hits=148 infer=17` both legs, err=0). 0 `[CACHE-GROW]`, 0 `[CACHE-SERVE]`, 0 faults. |

Flip smoke (committed default-ON build, no sed, 1200 s): `grow=9`, `served=6692`, `used=261`, retrieval co-live (`[RETR]`×16), `[STATS] q=44400 hits=37633 infer=16 err=0`, faults 0, `### W`=0. CI green on the flip commit (run 28653472883).

## 3. Deployment

- `install_jarvis_x86.sh --target esp --esp /dev/nvme0n1p4 --skip-build --skip-model` — verify OK (grubx64.efi, kernel, grub.cfg, rootserver image), **ubuntu kept `BootOrder[0]`**.
- ESP image **checksum-pinned to the flip build**: `md5 5de620b6e29878575ba78b56f5f792be` (ESP copy == `~/sel4-x86` build of `99419fb`).
- One-shot `efibootmgr --bootnext 0000` (Boot0000 = "JARVIS seL4") + reboot. **The next power-cycle returns to Ubuntu automatically.**

## 4. Live bare-metal confirm (Main-PC receiver, boot_id=12)

Captured over the real I211 (`--json`, 333 records / 90 s window at ~10.5 min uptime, every record `crc_ok=True`, version 3 / 216 B):

```
seq=625 boot_id=12  q_total=30700 q_hits=26025 q_infer=16 q_errors=0
flags_list=[MODEL_LOADED, FB_DRAWABLE, FB_MAPPED, SELFTEST_PASS, MEMORY, CONTEXT, RETRIEVAL, CACHE_GROWTH]
cache_growth_count=9  retrieval_hits=16  episodic_count=8192  crc_ok=True
```

- **`TLM_F_CACHE_GROWTH` live** + **`cache_growth_count=9`** — matches the QEMU legs' `grow=9` (6 at the first promotion tick → 9; the count climbed with the later promotions).
- **Bare-metal hit-rate conversion live:** q=30,700 in ~10 min (≈50 q/s), `q_infer` frozen at 16, `err=0` — the promoted patterns serve on real hardware exactly as in the KVM proof.
- **Retrieval co-live** (`retrieval_hits=16`) — both deployed memory features in one boot; `episodic_count=8192` = the circular store rolled full (by design).
- Console rows ("Cache growth — learns frequent queries" + "Patterns promoted") render from exactly this record shape — value-pinned by the e2e (22/22) against the golden fixture carrying the same fields.
- Earlier follow-mode captures from the same boot: healthy records from `up≈55 s` (`self=5/5`, `NN=6`, `model load=100%`, `fb=1024x768x32`, `CRC=OK` throughout).
- The **durable NVMe-log read for boot_id=12 awaits the user's power-cycle** back to Ubuntu (recovery command in §5 of `USER_GUIDE.md`; the one-shot BootNext is already consumed, so the power-cycle lands in Ubuntu automatically).

## 5. Deviations, gotchas, and incidents (for the next session)

- **First orchestration attempt failed twice on process-detection bugs**, both fixed: (a) `pgrep -c X || echo 0` yields `"0\n0"` on no-match (pgrep prints 0 AND exits 1) — earlier smoke watchers silently never detected exit and only worked because their poll limits exceeded the smoke length; (b) `pgrep -x qemu-system-x86_64` NEVER matches (`/proc` comm truncates to 15 chars → `qemu-system-x86`). One collateral: an ON qemu launch failed on the image write-lock while a mis-detected OFF qemu still ran; the stray was killed, all legs re-run cleanly from restored S1 state. Use `pgrep qemu-system-x86 > /dev/null && … || …` (exit code, prefix match) from now on.
- **The 12 GB `nvme_test.img` snapshot does NOT fit the box root partition** (98% full, 4.6 GB free). Right-size fix: only the episodic span mutates in a QEMU run → `dd` snapshot/restore of 8,193 sectors @ LBA 21,100,000 (4 MiB).
- **`telemetry_receiver.py --follow` crashes under Windows redirect** (`UnicodeEncodeError` on `≈` U+2248, cp1252): run with `PYTHONIOENCODING=utf-8`. (Interactive terminals are fine; this bit the file-redirected capture.)
- **Installer over ssh**: exec bit stripped by `git reset --hard` (run via `bash script.sh`); the interactive confirms pipe cleanly (`printf "/dev/nvme0n1p4\nADD-JARVIS\nDELETE-STALE-ESP-MODEL\n" | sudo HOME=/home/jarvis bash …`).
- **Pre-existing, NOT #6-related:** one `<|channel>thought` inference wobble ("How does virtual memory work in modern CPUs") appears in the CG-absent REF leg at this store state — a known G3-injection artifact class, present with cache-growth entirely off. Recorded so it is not misattributed to cache growth.
- **The durable NVMe-log read for boot_id=12 awaits the user's power-cycle** back to Ubuntu (the box is bare-metal JARVIS until then; one-shot BootNext already consumed).
- Box left clean: tree at `origin/master` (flag committed ON), snapshot + `.ref` artifacts removed; A/B logs `/tmp/g6_{ref,off,on,flip}.log` left for inspection (tmpfs — gone on power-cycle).

## 6. Proposed tag (NOT created — the user names/creates all tags)

`git tag -l` at proposal time: latest release tags are `v0.2.1-beta` and `v1.0.0` (monotonic ceiling: v1.0.0). The `memory` milestone tag was always scoped as a **milestone marker, not a release**. Proposal:

- **Name:** `memory` (as canonized in `PHASE_5_GOAL6_CACHE_GROWTH.md` / ROADMAP — "the early `memory` milestone tag"); alternative if a versioned form is preferred: `v1.1.0-memory`.
- **Message:**
  > memory — the it-remembers MVP arc is complete (Phase 5 #1/#2/#3/#6).
  > JARVIS remembers across power-cycles (episodic store), shares working
  > memory between processes (context pool), recalls prior answers into
  > inference (retrieval, default-ON since 2026-07-02), and learns
  > frequently-asked queries into its decision cache (cache growth,
  > default-ON since 2026-07-03) — frequency-based and deterministic; it
  > never "understands". Deployed image intentionally diverges from v1.0.0.
- **Suggested target:** the #6/M4 docs commit (this report).

## 7. Honest limits (unchanged claims)

- The cache **learns frequently-asked queries and serves them fast** (~50 ms–s → <1 ms). Frequency-based, deterministic. **Never "understands".**
- Served answers are stored response heads — exactly what the system previously said, served verbatim; a served answer never varies (unlike a fresh G3-injected inference, which may build on prior answers). That trade (freshness vs speed) is the designed behavior for *frequent* repeats.
- Hit/serve/growth counts are the metrics; "memory helped" remains a non-claim (offline-A/B territory).
