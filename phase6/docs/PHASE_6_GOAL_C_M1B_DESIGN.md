# Phase C / C/M1b — BOX plumbing for a co-resident embedding model: DESIGN + PRE-MORTEM

**Status: PLAN-FIRST.** No code, no flag, no CI, no box build/flash. Authored 2026-07-25, after
C/M1a host parity closed GREEN (`ea1baf0`). Follows the `PHASE_6_GOAL_K_SYSTEM_DESIGN.md` precedent:
components, data flow, and the concurrency/failure model read out of the **as-built code**, plus the
preserve/teardown boundary, a milestone split with real gates, and an honest-ceiling section.

**Every `file:line` in this document was re-grepped and read by the author at HEAD `ea1baf0`.**
Adversarial lenses were used to generate hypotheses; nothing entered this doc on an agent's say-so.
Where a lens claim survived my own verification it is marked VERIFIED; where it died, §11 records it.

---

## 1. Goal + scope

C/M1b makes the Qwen3-Embedding-0.6B model **resident and callable on the box**: PA provisions and
loads a SECOND GGUF, maps it into PB, and PB can compute a 1024-d embedding on request. That is all.

**Explicitly NOT C/M1b:** the semantic-recall lane (cosine top-k over the control-IN store, replacing
`g3_select_exact_only`) is **C/M2**; telemetry/console surfacing is **C/M3**; the `JARVIS_EMBED` flip
is C/M3's. C/M1b ends with a gated, deploy-inert capability that is *proven to work*, not *used*.

---

## 2. Ground truth (as-built, VERIFIED — not from plan text)

### 2.1 Measured on the box (read-only `ssh jarvis`, 2026-07-25)

| Fact | Value | Source |
|---|---|---|
| Gemma GGUF actual size | **3,106,731,392 B** (2962.8 MB) | `ls -l` on `/dev/nvme0n1p2` (`JARVIS_DATA`, 10 G vfat) |
| Gemma pages | **758,480** | `(size+4095)/4096`, per `main_x86.c:4110` |
| Gemma mapped span | **3,106,734,080 B = 2.8934 GiB** | derived from the above |
| PA root CNode | `CONFIG_ROOT_CNODE_SIZE_BITS 22` → **4,194,304 slots** | box `jbuild/kernel/gen_config/kernel/gen_config.h:138` |
| PB CSpace | `CONFIG_SEL4UTILS_CSPACE_SIZE_BITS 12` → **4,096 slots** | box `libsel4utils/gen_config/sel4utils/gen_config.h` |
| **PB musl heap** | `CONFIG_LIB_SEL4_MUSLC_SYS_MORECORE_BYTES` = **134,217,728 B = 128 MiB** | box `libsel4muslcsys/gen_config/.../gen_config.h` |
| Retype fan-out | `CONFIG_RETYPE_FAN_OUT_LIMIT 256` | box kernel gen_config.h:141 |
| SMP nodes | `CONFIG_MAX_NUM_NODES 6` | box kernel gen_config.h:150 |

The doc-inherited "~2962 MB" is **confirmed by measurement**, not assumed.

### 2.2 Address map (VERIFIED in `main_x86.c`)

| Region | Base | Note |
|---|---|---|
| `SHMEM_VADDR_A` | `0x10000000` | PA view of the IPC rings (`:1435`) |
| `SHMEM_VADDR_B` | `0x50000000` | PB view (`:1436`) |
| `SCTX_VADDR_A/B` | `SHMEM_VADDR_* + 2*4096` | shared-context page (`:1439-1440`) |
| `MODEL_VADDR_B` | `0x60000000` (1.5 GiB) | Gemma in PB (`:1445`) |
| **Gemma end (computed)** | **`0x1192D0000` = 4.3934 GiB** | base + 2.8934 GiB — **past the 4 GiB line**, as suspected |

The >4 GiB base survives the argv round-trip: PA formats with `%lu` (`main_x86.c:1830`) and PB parses
with `atol` (`inference_server.c:769`), both 64-bit on this LP64 ABI. The `put_dec((uint32_t)(model_vaddr
>> 20))` at `inference_server.c:788` is display-only and does not truncate meaningfully. **No 32-bit
truncation hazard exists on this path** — a hypothesis I specifically tested and disproved.

### 2.3 Allocator

`ALLOCATOR_STATIC_POOL_SIZE = BIT(seL4_PageBits)*500` = 2 MiB (`main_x86.c:449`);
`ALLOCATOR_VIRTUAL_POOL_SIZE = BIT(seL4_PageBits)*100000` ≈ 390 MiB (`:451`); bootstrapped
`..._leaky` at `:1314`. **`vspace_unmap_pages` appears ZERO times in `main_x86.c`** (verified by count)
— PA never unmaps; the forward-only/leaky property that forced reuse-in-place in K/M2 is intact.

---

## 3. Components + data flow (proposed)

```
                    ┌──────────────────────── Process A (rootserver) ───────────────────────┐
NVMe ── FAT32 ──►   │ fat32_find_file("QWENEMB GUF")  ── SAME fat32_fs_t handle (cache warm) │
                    │ vka_alloc_frame × 155,904  ──►  model2_frame_caps[]                    │
                    │ vspace_map_pages (PA staging)   ──►  gguf magic check                  │
                    │ map_frame_direct × N  ──►  PB @ EMBED_VADDR_B                          │
                    └───────────────────────────────┬──────────────────────────────────────┘
                                                    │ argv[5], argv[6] = base, size
                    ┌───────────────────────────────▼──────────── Process B ────────────────┐
                    │ g_pbe_qm / g_pbe_state  (FILE-SCOPE statics, .bss — see §4.1)          │
                    │ qmodel_load(embed)  +  llama_alloc_state(CAPPED ctx — see §4.3)        │
                    │ pb_serve_loop: MSG_EMBED ──► qmodel_embed_last ──► embed shared page   │
                    └───────────────────────────────────────────────────────────────────────┘
```

Transport is a **dedicated shared page pair, not the response ring** — see §4.5.

---

## 4. The five hazards, with verified evidence

### 4.1 The respawn interaction (L1) — REFRAMED, and the seed's severity CONFIRMED

**The self-heal respawn is compiled into the deployed image.** `JARVIS_RESPAWN = (JARVIS_KM2A_SPIKE ||
JARVIS_ACTIONS)` (`jarvis_debug.h:510`) and `JARVIS_ACTIONS` is **default-ON**. CLAUDE.md's framing of
`pb_restart_entry` as `JARVIS_KM2A_SPIKE`-gated is **imprecise**: only the *spike probes/markers* ride
`KM2A_SPIKE`; the respawn machinery itself rides `ACTIONS`. So this path fires on real faults today.

**(a) Does a second model's state survive re-entry?** YES, structurally — and no new stash is required.
`g_pbm_qm/g_pbm_vocab/g_pbm_tok/g_pbm_state` are already **file-scope statics** (`inference_server.c:119-122`),
aliased into `main()` by `#define` (`:717-720`). They live in `.bss`. `pb_restart_entry` (`:677`) only
re-enters on a fresh stack and calls `pb_serve_loop`; it never touches `.bss`. A second model declared
the same way is untouched by a respawn **by construction**.

**A stale comment must be corrected while we are here.** `inference_server.c:1016-1017` says the
`&qm/&state/&tok` stash pointers "into this frame stay valid because the restart re-enters on
`g_km2a_restart_stack` (fresh SP), preserving main()'s frame." That rationale is **obsolete** since the
K/M2b-2-prereq "G" hoist: those are no longer frame locals, they are `.bss` addresses, so validity is
unconditional and *stronger* than the comment claims. Someone "fixing" the restart stack on the strength
of that comment would be reasoning from a false premise.

**(b) Does the zero-RESOURCE gate still detect a leak? NO — and this is the real finding.**
`pb_restart_entry` prints exactly four pointers: `g_km2a_state->key_cache`, `->value_cache`, `->logits`,
`g_km2a_qm->layers` (`:693-696`), all **Gemma's**. With two models, the gate's proxy no longer represents
PB's heap: an embed-state re-alloc or leak would leave all four byte-identical and **pass**. The gate
does not become wrong, it becomes **partial** — and C/M1b is exactly the change that makes it partial.
⇒ **C/M1b-2 must extend the printed baseline with the embed state's pointers.** Non-negotiable.

**(c) What re-runs on a respawn?** `main()` does **not**. `pb_restart_entry` calls `pb_serve_loop`
directly (`:705`). So the embed model is loaded once, at boot, and is *not* re-loaded on respawn —
which is correct and desirable (no re-alloc, no leak), provided (d).

**(d) Signature or statics?** `pb_serve_loop` takes `qm/state/tok` as **parameters** (`:564-567`), and
`pb_restart_entry` passes the stashed Gemma ones. If the MSG_EMBED handler took the embed model as a
new parameter, the signature change would ripple into `pb_restart_entry` and the stash. **Recommendation:
reference the embed file-scope statics DIRECTLY inside the handler.** Then the respawn path is not
touched at all — the smallest possible blast radius against a default-ON path.

### 4.2 VADDR layout (L2) — and what an overlap actually does

Gemma ends at `0x1192D0000` (§2.2). A second region must start above that, but **Gemma's span is
runtime-sized** (from the file), so a hard-coded embed base encodes an assumption about Gemma's size.

**What an overlap does is worse than either option the seed offered.** It is **neither** silent
corruption **nor** a hard fault: `seL4_X86_Page_Map` over an existing mapping returns an error, and the
mapping loop does `merr++; continue;` (`main_x86.c:1690-1695`), then merely **prints** the error count
(`:1704-1706`) and proceeds. The result is a **partially mapped model that still boots and runs** —
garbage weights presented as a working system. That is the worst failure class this project has a name
for, and it is unguarded today.

**The obvious placement is already ruled out.** The only free window *below* 4 GiB is between the shmem
pages and Gemma: `[SHMEM_VADDR_B + 3*4096, MODEL_VADDR_B)` = `[0x50003000, 0x60000000)` = **exactly
256.0 MiB** — too small for a 609 MB model. So the embed region *must* go above Gemma's end, and any
instinct to drop it at `0x80000000`/`0xA0000000` lands **inside** the Gemma mapping.

**Recommendation (collision impossible, not merely unlikely):** derive the embed base at runtime as
`EMBED_VADDR_B = round_up(MODEL_VADDR_B + nvme_model_n_pages*4096, 1 GiB)` + a 1 GiB guard gap, pass it
in argv (the path already carries a >4 GiB base safely, §2.2), and add a **`_Static_assert`-style runtime
check plus a one-line `[VMAP]` boot dump** of every PB base and computed end. Additionally: make
`merr > 0` **fatal to the embed capability** (refuse to advertise the embedder) rather than a printed
statistic. Do not make it fatal to the *boot* — see §4.6 degrade rule.

### 4.3 The budget (L3) — the binding constraint is the HEAP, not cslots

**cslots are fine.** The mapping loop allocates 2 caps per page — `vka_alloc_frame` (`:4134`) plus
`vka_cspace_alloc_path` + `vka_cnode_copy` (`:1689-1691`) — and **both live in PA's CSpace**, because
`&vka` is PA's allocator. PB receives only the *mapping* in its VSpace, not caps. (This corrects the
seed's "count BOTH cspaces": PB's 4,096-slot CNode is not in the picture at all, which is also why PB's
tiny CNode has never been a problem.) So:

| | pages | PA cslots (2/page) |
|---|---|---|
| Gemma (measured) | 758,480 | 1,516,960 |
| Embed Q8_0 (609 MB) | 155,904 | 311,808 |
| **Total** | 914,384 | **≈1,828,768 of 4,194,304 → ~44%** |

Headroom is ample. Frame memory: 2.89 + 0.60 = **3.49 GiB** of 32 GB RAM, mapped **twice** (PA staging
+ PB) but the same physical frames.

**The heap is the problem.** PB's musl morecore is **128 MiB total** (§2.1) and is shared by both models'
`llama_state_t`. Weights are zero-copy (they point into the mapped GGUF), but the state is `calloc`'d:
`kv_size = kv_n_layers * max_seq * max_kv_dim` floats × 2 caches (`llama_load.c:743-752`), and
`max_seq` defaults to `LLAMA_MAX_SEQ_LEN` = **512** (`llama_model.h:83`, clamped at `llama_load.c:144-148`).

For the measured Qwen3-Embedding config (28 layers, 8 kv-heads, head_dim 128, no shared-KV — printed by
the C/M1a harness), KV costs **229,376 B per context token**:

| `max_seq` | KV | state total |
|---|---|---|
| **512 (default)** | **112.0 MiB** | **113.2 MiB** |
| 128 | 28.0 MiB | 28.8 MiB |
| **64** | 14.0 MiB | **14.7 MiB** |

**At the default context the embed state alone is 113.2 MiB of a 128 MiB heap that Gemma is already
using.** This is a cycle-1 brick, not a tuning issue. **Capping the embed context is therefore a
CORRECTNESS REQUIREMENT of C/M1b, not an optimization** — and it must be enforced explicitly, because
`llama_load_config` will happily take 512 from the GGUF. 64 tokens is ample: a control-IN query is
≤172 bytes (~40-50 tokens) and the C/M1a probes tokenized to 5–15.

**Good news on the failure mode:** `llama_alloc_state` validates every allocation and returns −1 via
`llama_free_state` (`llama_load.c:818-827`), and PB checks it and cleans up (`inference_server.c:843-849`,
"State alloc failed (OOM?)"). So heap exhaustion **fails loudly and recoverably** — which makes the
§4.6 degrade rule implementable.

**A latent defect C/M1b makes far more likely.** In the frame-allocation loop, exhaustion sets
`n_pages = p; alloc_fail = 1; break;` (`main_x86.c:4138-4140`) — but **`alloc_fail` is never read
again** (only two occurrences exist in the file: the declaration at `:4131` and that assignment).
Execution falls through to `vspace_map_pages(..., n_pages, ...)` with the *truncated* count and then
`fat32_read_file(&fs, model_cluster, model_size, model_local)` (`:4185`) with the **full, untruncated
`model_size`** — writing past the mapped region. Today this is unreachable (Gemma fits); a second
155,904-frame allocation is precisely what would first reach it. **C/M1b must honour `alloc_fail`.**

**(e) A silent-wrong-vector hazard the seed did not name.** The Gemma query path resets the
autoregressive state on every query — `state->pos = 0;` plus a KV memset (`inference_server.c:377-381`).
`qmodel_embed_last` deliberately does **not** do this itself; its own contract says "The CALLER resets
`state->pos` + KV cache per sequence" (`llama_quant.c`, the C/M1a comment). If the MSG_EMBED handler
omits that reset — or if a respawn interrupts an embed mid-sequence, leaving `pos`/KV dirty — the *next*
embed returns a **plausible but wrong vector**: no crash, no fault, no gate signal, and cosine-recall
would silently degrade. **The handler must reset per embed request**, and C/M1b-2's parity gate must
include a back-to-back embed (same probe twice, byte-identical vectors) to prove it.

### 4.4 The shared worker pool (L4) — sequentiality is already structural

`threadpool_sel4.c` holds a **single global** `static sel4_threadpool_t g_pool` (`:33`).
`jarvis_parallel_for` publishes `fn/end/ctx_buf/next_idx` into it and bumps `gen` (release), workers
acquire on `gen` (`:53`) and join via an `active` counter (`:61-62`). It is therefore **not re-entrant**:
a second concurrent dispatch would overwrite `fn/end/ctx_buf` and reset `next_idx` — corruption, not
serialization.

**But it cannot happen from the design as scoped.** PB's `pb_serve_loop` is single-threaded and handles
**one message at a time**; a Gemma forward and an embed forward are both driven from that loop, so they
are serialized by the loop itself. The invariant is **structural, enforced by PB's single-threaded serve
loop** — provided C/M1b does *not* introduce a second dispatch context (a worker thread, a notification
handler, or an embed call inside a Gemma forward). PA's lanes (wake / proactive / control-IN / heartbeat)
all *send* to PB and wait; they do not execute in PB.

**The design rule to write down:** the embed forward is invoked from exactly one place — the MSG_EMBED
case of `pb_serve_loop` — and is never called from inside `qmodel_forward`, a worker callback, or an
interrupt path. C/M1b-2's gate should include a mid-inference embed request proving it is *queued*, not
interleaved.

### 4.5 IPC (L7) — do NOT put the vector through the response ring

**Why `0x10` is reserved (answered):** `shmem_ipc.h:39` — `/* 0x10 reserved (was MSG_MODEL_SWAP,
removed 2026-04-17) */`. That is the dynamic-model-scaling removal ADR. It is a deliberate tombstone,
so **MSG_EMBED should take `0x12`**, leaving the tombstone intact.

**The ring cannot carry a 1024-float vector safely.** Geometry: 15 slots × 256 B, `SHMEM_MAX_PAYLOAD`
240 (`shmem_ipc.h:18-20`). A 4096 B vector = **18 chunks into a 15-slot ring**. And the existing chunk
loop **ignores the send return code**: `int rc = shmem_ipc_send(...)`, then `(void)rc;` when
`JARVIS_DBG_RING` is off (the deploy default), with `offset += chunk; msg_seq++;` unconditional
(`inference_server.c:500-516`), while `shmem_ipc_send` returns −1 on a full ring (`shmem_ipc.c:58,60-70`).
**A full ring silently drops the chunk and the loop advances** — producing a truncated payload with no
error anywhere.

Why this has never bitten: `char text_out[512]` (`inference_server.c:479`) caps a response at ≤3 chunks.
An 18-chunk burst is a **6× jump into a regime the code has never been in**. A silently truncated
embedding is still a plausible-looking float array — the exact silent-failure class the C/M1a parity
harness exists to catch, arriving through a different door.

**Recommendation: a dedicated shared region, following the SCTX precedent.** The shared frames are
allocated as an array of 3 at `main_x86.c:1617-1625` (2 rings + the context pool) and mapped into both
vspaces; extending to **5** gives an 8 KiB embed region: page 0 = control header (request seq, status,
length, a release-store ready flag), page 1 = the 4096 B vector. The ring then carries only a small
`MSG_EMBED` request and a small `MSG_EMBED_RESULT` completion (seq + status), so **no lane is starved
and no chunking exists to truncate**. A 1024-float vector is exactly one page with zero bytes spare for a
header, which is why the header gets its own page rather than being squeezed in.

**Correlation:** `wait_for_response` polls the ring for an expected *type* and drains stale messages
(the documented race-free pattern); a small typed completion fits that model unchanged. A seq field in
the control page makes staleness detectable independently of the ring.

**Also worth recording:** CLAUDE.md states "PB must reserve 3 for MSG_RESPONSE — use `pb_can_log()`".
That is **stale as fact for the shipped build**: `pb_can_log` is `__attribute__((unused))` (`:151`) and
its only call site (`:164`) is inside `#if JARVIS_DBG_PB` (default 0). No reservation is enforced at
runtime. Harmless today (the debug sends compile out, so nothing competes), but it means **there is no
existing backpressure mechanism** to lean on — another reason not to route bulk data through the ring.

### 4.6 Provisioning, boot time, and the degrade rule (L5, L6)

**FAT32 handle reuse: YES, and it is free.** `fat32_fs_t` carries `fat_cache[512]`, `fat_cache_lba`,
`fat_cache_valid`, documented "read-only FS: never invalidated" (`fat32.h`). The **same `fs` handle**
serves a second `fat32_find_file` + `fat32_read_file` with the FAT-sector cache still warm — no re-init.
One caveat: `fs.progress` is a **single hook** (`main_x86.c:4179`) driving the HUD load bar; a second
load must re-point it or the on-screen percentage will be wrong for the second file.

**Boot time.** The added read is 609 MB against Gemma's 2962 MB — **≈21% more model I/O**, plus ~156k
more frame allocations. I decline to convert that into seconds: I could not find a *measured* MB/s for
the deployed NVMe path in-repo, and inventing a throughput figure would be exactly the kind of unmeasured
number this project rejects. **C/M1b-1's gate is to MEASURE it** (boot-relative `T+ms` timestamps already
exist in `[SNAP]`/`[STATS]`), and to report it, because it lands on both the deploy loop and the 6-7 soak.

**8.3 name.** `setup_nvme_partition.sh` already hard-asserts the pattern: `DEST_FILENAME="GEMMA2B.GUF"`
(`:32`), `EXPECTED_SHORTNAME="GEMMA2B GUF"` (`:39`), a basename→8.3 helper (`:159-176`), a **loud
failure** if they disagree (`:176-179`), and a post-copy on-disk re-verify (`:356-362`). C/M1b mirrors
this exactly for a second file. **Proposed: `QWENEMB.GUF` → 8.3 `"QWENEMB GUF"`** (7 + 3, unambiguous,
no LFN).

**Degrade rule (must be explicit).** Today a missing model file merely logs and continues
(`main_x86.c:4211-4212`, non-fatal). C/M1b must preserve that shape: **a box with no second model file,
or a failed embed load, or a failed embed state alloc, must boot and serve exactly as it does today**,
with the embed capability simply unavailable. This matters because the deployed ESP image and the
provisioned NVMe partition are updated by different steps — a mismatch must never brick the box.

---

## 5. Preserve / teardown boundary

**PRESERVE across a respawn** (already true, must stay true): `g_pbm_*` and any new `g_pbe_*`
file-scope statics in `.bss`; both models' mapped frames and their PA-side caps; the shmem/sctx/embed
pages; the notification objects.

**One caveat on the G hoist's completeness.** The hoist covered four objects; `gguf_ctx_t gguf_ctx;`
remains a **`main()` local** (`inference_server.c:805`). It survives a respawn only because the restart
re-enters on a *separate* stack, freezing `main()`'s frame — an accident of the design, not a stated
property. A second model's `gguf_ctx_t` would inherit that same undocumented fragility. Since
`qtensor_t.data` points into the mapped model image rather than the ctx, post-load use is probably not
required — but "probably" is the operative word, and C/M1b should either hoist the second ctx to file
scope or state explicitly why it need not be. **TEARDOWN: none.** The allocator never frees (§2.3) and PA never
unmaps (`vspace_unmap_pages` = 0 occurrences), so C/M1b introduces **no new teardown path** — the second
model is allocated once at boot and lives for the boot. Any design that wants to *reload* the embed
model at runtime is out of scope and would collide with the leaky allocator exactly as K/M2 found.

---

## 6. Milestone split

The seed proposed two milestones. **I propose three**, and the evidence for the extra split is §4.3:
the PB-side *state allocation* against a 128 MiB heap is the single most likely cycle-1 failure and it is
independent of both provisioning and IPC. Bundling it with either neighbour makes a red gate ambiguous.

**C/M1b-1 — provisioning + co-resident LOAD/MAP.** Extend `setup_nvme_partition.sh` for the second file;
PA finds/loads/maps it; PB does *nothing* with it. Gates: `[VMAP]` layout dump with computed ends and a
proven-impossible overlap; `merr == 0`; `alloc_fail` honoured; measured boot-time delta; measured
frame/cslot headroom; **absent-file degrade proven**; EMBED=0 identity (§7).

**C/M1b-2 — PB embed state + forward, PROBE-driven, no IPC.** `qmodel_load` on the second model,
`llama_alloc_state` with the **capped context**, `qmodel_embed_last` invoked from a probe. Gates: PB heap
headroom measured and reported; **the K/M2a-2 pointer baseline EXTENDED to the embed state** (§4.1b) and
flat across ≥8 induced respawns; a post-respawn embed still correct; **on-box vector parity against the
C/M1a host golden** (the same 15 probes — this is the milestone that proves the box reproduces the host);
per-embed latency measured.

**C/M1b-3 — MSG_EMBED transport.** The `0x12` request + the shared embed page + completion + correlation.
Gates: no lane starvation (heartbeat/shield/control-IN/wake unaffected); a mid-inference embed request is
queued not interleaved (§4.4); err=0 over a sustained run; EMBED=0 identity re-held.

---

## 7. EMBED=0 identity plan (stated up front)

**Objects:** `main.c.obj`, `inference_server.c.obj`, `llama_quant.c.obj`, `llama_load.c.obj`,
`tokenizer.c.obj` — every TU a second model can touch. (`llama_quant.c` / `llama_load.c` are already
proven identical at C/M1a and must *stay* so.)

**Method, carrying the C/M1a correction forward:**
1. **Preprocessed-source identity** (`-E -P`, byte-compare) — compiler-independent, the strongest axis,
   and cheap. C/M1a proved this is achievable for gated edits.
2. **Section content + symbols** — `.text`/`.rodata`/`.data` contents and `nm`, against the pre-change
   commit, compiled in one fixed working dir so only file contents differ.
3. **Never md5 the whole `.o` without a control.** On clang/COFF this is **invalid** — C/M1a's control
   (same source compiled twice) produced *different* md5s due to an embedded TimeDateStamp. On the box's
   Linux/ELF gcc it is expected to be valid, **but the same-source-twice control must be run to prove
   it** before relying on it. (Object-level analogue of the 6-1 "never md5 a packed image" rule.)

---

## 8. KVM-first

Every gate that *can* run under KVM (`-smp 6`) runs there before any flash: load/map, the `[VMAP]` dump,
heap headroom, the respawn pointer baseline, vector parity, MSG_EMBED transport, lane non-starvation.
**Never flash before a KVM pass.** Bare metal is required only for what KVM cannot show: real NVMe
throughput / boot-time delta (KVM's emulated NVMe is not the Lexar NM790), real I211 telemetry, and the
final sustained err=0 run. This mirrors every prior milestone and is not negotiable for a default-ON box.

---

## 9. Honest ceiling

C/M1b proves the box can **hold a second model and compute an embedding that matches the host golden**.
It does **not** make the embedder useful: nothing consults it, no recall decision changes, no user-visible
behaviour differs. The semantic-recall lane is **C/M2**; a console/telemetry surface is **C/M3**.

It also does not prove the embeddings are *good* — C/M0.5's 97.2% recall@1 was measured off-box on
N=36 hand-authored items, and the honest limits recorded there (anisotropy, the 66.7% near-synonym
adversarial subset) are untouched by C/M1b. And it does not close 6-5's exact-repeat recall limitation;
that closes at C/M2 at the earliest, on measured data, or not at all.

Finally: this adds ~609 MB of resident model and ~21% more boot I/O to a box that must later sustain a
**7-day 6-7 soak**. C/M1b is deploy-inert by gating, but the moment `JARVIS_EMBED` flips, the soak's
baseline changes and the soak must be re-run, not extrapolated.

---

## 10. Open questions (numbered, with recommendations)

1. **Embed context cap value.** Recommend **64 tokens** (14.7 MiB): covers a ≤172-byte control-IN query
   with ~4× headroom on the C/M1a probe lengths. 128 (28.8 MiB) is affordable if C/M2 wants to embed
   stored *answers* rather than queries — decide by what C/M2 embeds. **This must be decided before
   C/M1b-2**, since it sets the heap budget.
2. **Embed vaddr base: runtime-derived or fixed?** Recommend **runtime-derived + guard gap + `[VMAP]`
   dump** (§4.2). A fixed base silently encodes a max-Gemma assumption that a future model change breaks.
3. **Should `merr > 0` be fatal?** Recommend **fatal to the embed capability, never to boot** (§4.6).
4. **Fix `alloc_fail` (§4.3) inside C/M1b, or as a separate pre-fix commit?** Recommend **separate and
   first** — it is a pre-existing deployed-path defect on the Gemma path, and fixing it under the
   `JARVIS_EMBED` gate would leave the deployed path broken.
5. **Transport: shared page pair (recommended) vs chunked ring vs Matryoshka-truncated 256-d?** Recommend
   the **page pair** (§4.5). MRL truncation to 128/256 dims is a legitimate C/M2 *storage* optimization but
   must not silently change what parity was proven against at 1024.
6. **Does the embed model need its own tokenizer instance?** C/M1a used the deployed `tokenizer.c` with
   the qwen vocab and hit 15/15. Two models = two vocabs ⇒ almost certainly two `tokenizer_t`/`gguf_vocab_t`
   instances. I did **not** fully verify whether any tokenizer state is global — flagging it as an
   explicit C/M1b-2 verification item rather than asserting it.
7. **Is `LLAMA_MAX_SEQ_LEN` per-model or global?** It is a compile-time `#define` (`llama_model.h:83`) used
   as a clamp; the cap must therefore be applied to the embed model's `config->max_seq_len` *after* load,
   not by changing the global. Confirm no other path re-reads it.
8. **6-7 soak sequencing.** C/M1b box work conflicts with a 7-day soak. Recommend **finish C/M1b-1..3
   gated, then run 6-7 on the EMBED=0 image** (which the identity proof says is the current image), so the
   soak result stays valid for the deployed configuration.

---

## 11. Verification log — what died, what I could not verify

**Inherited claims KILLED by the code (evidence in-section):**
- "`pb_restart_entry` is `JARVIS_KM2A_SPIKE`-gated" → **false**; `JARVIS_RESPAWN` includes
  `JARVIS_ACTIONS`, which is default-ON (`jarvis_debug.h:510`). §4.1.
- "Count both cspaces for the per-page cap copies" → **false**; both allocations are in **PA's** CSpace
  (`main_x86.c:1689` uses PA's `&vka`), and PB's 4,096-slot CNode holds no model caps. §4.3.
- "PB must reserve 3 ring slots via `pb_can_log()`" (CLAUDE.md) → **stale as fact**; the function is
  `__attribute__((unused))` and its sole call site is `#if JARVIS_DBG_PB`-gated (default 0). §4.5.
- The stash comment's "pointers into this frame" rationale (`inference_server.c:1016-1017`) → **obsolete**
  after the G hoist; the pointers are `.bss`. §4.1.
- "A >4 GiB model base risks 32-bit truncation through argv" → **disproved**; `%lu`/`atol` are 64-bit
  here. §2.2.
- "An overlap is silent corruption *or* a hard fault" → **neither**; it is a counted-but-ignored map
  error that yields a partially-mapped model which still runs. §4.2.

**NOT verified — needs a box run, and must not be asserted until then:**
- Gemma's *current* `llama_state_t` heap consumption (I have the formula and PB's 128 MiB ceiling, but
  not Gemma's live figure) — so "how much of the 128 MiB is actually free" is **UNKNOWN** and is
  C/M1b-2's first measurement.
- Real NVMe throughput ⇒ the boot-time delta in seconds (§4.6).
- Whether any tokenizer/vocab state is global (open question 6).
- Whether md5-of-`.o` is deterministic under the box's gcc/ELF (§7 item 3).

**What the adversarial lens pass contributed** (hypotheses I then verified against the code myself, and
would not otherwise have reached): the **per-embed `pos`/KV reset** silent-wrong-vector hazard (§4.1e,
verified at `inference_server.c:377-381`); the **unhoisted `gguf_ctx`** caveat (§5, verified at
`inference_server.c:805`); and the observation that the **only sub-4 GiB window is 256 MiB** and
therefore unusable (§4.2). Several other lens claims were discarded as unverifiable or wrong — including
a cslot-per-page figure that disagreed with the code and a Gemma span derived from an on-screen panel
string literal rather than the measured file size.

**Pre-mortem honesty note:** the seed's L1, L2, L3, L4, L5, L6 and L7 all pointed at real areas, but
three of them were **wrong about the mechanism** (L1's gating, L3's cspace accounting, L2's failure
mode), and the single largest cycle-1 risk — the **128 MiB heap vs a 113.2 MiB default-context embed
state** (§4.3) — was **not in the seed at all**. That, plus the dead `alloc_fail` guard and the
rc-ignoring chunk loop, are the findings that justified the exercise.
