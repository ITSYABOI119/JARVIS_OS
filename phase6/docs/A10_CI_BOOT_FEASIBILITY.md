# A10 — feasibility spike: can a GitHub runner build and boot what ships?

**Date:** 2026-08-09 · **Repo at:** `ed6e4cc` · **Kind:** measurement only. No feature was built, no
CI job was committed, the box was never booted and never written to.

> **This is a SPIKE.** The deliverable is a measured verdict. Every number below came off a real
> GitHub-hosted runner or a read-only `ssh jarvis` inspection; nothing is estimated unless the line
> says so. The throwaway workflows that produced them lived on branch `spike/a10-q1` and were
> deleted with it — the run IDs are recorded so the evidence stays addressable.

---

## Verdict — **GO**, with one named prerequisite

| band | fired | why |
|---|---|---|
| **GO** | **YES** | Q1 executes (a full boot, not a CPUID reading) · Q2 reproducible, and far cheaper than assumed · Q3 end-to-end well inside the ~30 min band and stable across runs |
| GO, REDUCED SCOPE | no | booting is feasible, so the link-only fallback is not needed — and §5 below shows it would save almost nothing anyway |
| REPORT, DO NOT DECIDE | no | not slow, not flaky |
| NO-GO | no | — |

**The prerequisite, and it is small:** A10 cannot be implemented today because the JARVIS build
tree's customization is **not in this repo and not in any commit** — it is uncommitted working-tree
state on the box (§Q2). Capturing it is a few-KB commit. That capture is a precondition for *any*
CI build tier, boot or link.

**The honest ceiling of this verdict:** it says a runner *can* build the kernel and *can* execute
the shipped image. It does **not** say a CI boot would reproduce the deployed configuration — two
measured runner-side degradations mean it would not, without extra work (§Q1c).

---

## Q1 — execution environment

### Q1a — the runner (run `31310593705`, 31 s)

| fact | measured |
|---|---|
| `/dev/kvm` | **PRESENT** — `crw-rw---- root:kvm`, but `runner` is **not in the `kvm` group** ⇒ `KVM_READABLE=no`, `KVM_WRITABLE=no` |
| fix | `sudo chmod 666 /dev/kvm` — one line, passwordless sudo is available. Verified: `KVM_NOW_WRITABLE=yes` |
| host CPU | AMD EPYC 7763, flags `avx avx2 f16c fma svm xsave xsavec xsaveopt`, `kvm_amd` loaded, **nested=1** |
| resources | 4 vCPU · 15 GiB RAM · 145 GiB disk, 88 GiB free (no separate `/mnt`) |
| QEMU | **not preinstalled**; `qemu-system-x86` + `qemu-user` install in **19–23 s**. Version **8.2.2** |
| TCG `-cpu max` advertises | `avx=true avx2=true xsave=true xsaveopt=true fma=true f16c=true`, **`xsavec=false`** |
| TCG *executes* AVX2 | **yes** — a static `-mavx2` binary (`VPADDD ymm`) returned the correct result under `qemu-x86_64 -cpu max` |
| sparse 11 GiB image | `truncate` = **0 s**, apparent 11 G, **on-disk 0** — Q3's disk worry is free |

### Q1b — the decisive test: an actual boot (run `31310800988`)

Booted the **box-built** kernel + rootserver (md5 `d22affe8…` / `6addc515…`, both verified
byte-identical after transfer) under three configurations. **Stated plainly: these are box-built
binaries, not CI-built** — which is exactly what Q1 asks, since the question is whether the runner
can *execute* the image.

| cfg | accel / cpu | serial | outcome |
|---|---|---|---|
| **A** | KVM `-cpu host` | **120,108 B** | **FULL BOOT.** `Self-Test: 5/5 PASS`, PCI scan, NVMe IDENTIFY `"QEMU NVMe Ctrl"`, Process B spawned from CPIO, shared memory + SCTX + embed region mapped, workload loop to **q=15,800** with **158 `[STATS]` windows in 150 s** |
| **B** | TCG `-cpu max` | 8,963 B | **BOOTS.** `Self-Test: 5/5 PASS`, reached `Starting continuous workload`, **1** `[SNAP]` in 300 s, then `[PA] Waiting for PB... 3000K polls`. Functional, ~2 orders of magnitude too slow to be a gate |
| **C** | the **committed** `qemu_test.sh` Nehalem fallback, verbatim | 2,521 B | **DOES NOT BOOT** — `XSAVE not supported` → `seL4 called fail … boot_sys.c:723` → `halting...` |

All three exited 124 (the 75–300 s `timeout`), which is expected: the JARVIS workload loop has no
exit condition. Exit 124 is not a failure signal here.

**Conclusion: KVM is the only viable accelerator.** TCG `-cpu max` is *correct* but unusably slow;
the committed Nehalem line cannot boot the deployed kernel at all.

### Q1c — two runner-side degradations a CI boot must account for

Both measured, both present in **A and B alike**, so they are properties of the runner VM, not of
the accelerator:

1. **`untyped_count = 105`** on the runner at `-m 8G`, against **181** on the box at 32 GiB. The
   consequence is concrete and visible: `M3: done-notification alloc/copy failed — degrading to
   serial` → **`M3: started 0 workers, pb_n_threads=1`**. The threadpool does **not** engage. A CI
   boot at 8 GiB therefore tests an `NN=6`-configured but single-worker system. (`-m 12G` is
   available on a 15 GiB runner and is the obvious first thing to try; not measured here.)
2. **With no model file, PB crash-loops to the bound.** FAT32 init fails (`err=1`) → PB gets
   base/size 0 → `[FAULT] label=5 … addr=0x50` → 5 restarts → **`[FATAL] PB crash-loop bound hit —
   degraded: cache-only, PB dispatch STOPPED`**. This is the crash-loop bound **working**, and the
   box then served cache-only to q=15,800 with `err` frozen at 8 — correct degradation, not a
   defect. But note it is a *different* path from `g_model_bad`: the model was never read, so
   `g_model_bad` never latches, `PB_DISPATCH_OK()` stays true, and the `8e4c5be` lane guard does not
   apply. **A naive CI assertion of `FATAL=0` would fail on a no-model boot.**

---

## Q2 — reproducing the build

### What `$HOME/sel4-x86` actually is (read-only `ssh jarvis` inspection)

- Created by `repo init -u https://github.com/seL4/sel4test-manifest.git` + `repo sync`; the
  manifest repo is at `a205b6cf` (2026-03-20).
- **10 projects, and `repo manifest -r` emits a fully pinned 2 KB manifest** — kernel
  `ebbda2af…`, sel4test `808ff09…`, musllibc `b0005f86…`, etc. Reproducibility of the *upstream*
  half is therefore a solved problem, today, with a committed 2 KB file.
- Tree is **260 MB** on disk (`.repo` 69 MB, `jbuild` 68 MB) — not the "2–3 GB" the setup doc's
  download figure implies.
- Toolchain: **gcc 13.3.0 on Ubuntu 24.04 — the same as the `ubuntu-24.04` runner image.**
- `jbuild` was configured once by hand via `init-build.sh`; `build_jarvis_x86.sh` only ever runs
  `ninja` in it and **hard-fails** if either `$SEL4_DIR` or `$SEL4_DIR/jbuild` is missing
  (`:91`, `:1032`). It cannot bootstrap.

### `projects/jarvis-x86` — the part that is not in this repo

A **second clone of `seL4/sel4test.git` at the same `808ff09`**, then mutated **in the working tree
and never committed**:

```
90 changed paths:  72 deleted · 5 modified · 13 untracked
77 files changed, 9,400 insertions(+), 17,386 deletions(-)
0 commits on top of upstream        no remote for the delta
```

**The prompt's premise is confirmed, and is worse than stated** — the delta is not merely absent
from this repo, it is absent from *any* git history anywhere, so it is unrecoverable from anything
but that disk. **But it is also far smaller than 27,000 lines suggests.** Broken down:

| part | reproducible? | how |
|---|---|---|
| all **13 untracked** entries (`src/{ai,crypto,drivers,ipc,net}/`, `inference_server.c`, `jarvis_debug.h`, `avx2_probe.h`, `smp_probe.h`, `jarvis_ui_tokens.h`, `embed_gate_pairs.h`, `apps/jarvis-inference/`, `apps/jarvis-input/`) | **YES** | copied from `phase3/src` by `build_jarvis_x86.sh` (42 `copy_file` calls); `apps/jarvis-input/CMakeLists.txt` is *written verbatim* by the script |
| `apps/sel4test-driver/src/main.c` (M) | **YES** | the script copies `main_x86.c` → `main.c` |
| `apps/sel4test-driver/CMakeLists.txt` (M) | **YES** | the script sed-patches the AVX2 flags in |
| `settings.cmake` — the IOMMU line (M) | **YES** | the script sed-patches `KernelIOMMU ON`→`OFF` (`:1044`) |
| `kernel/src/arch/x86/multiboot.S` | **YES** | the script inserts an MB2 framebuffer-request tag, idempotently, every build (`:1063`) — note this patches the **kernel source**, so a CI build must go through the script, not raw `init-build.sh` |
| **`CMakeLists.txt`, `easy-settings.cmake`, `settings.cmake` (CNode + morecore)** | **NO** | hand-made |
| **the 72-file deletion list** | **NO** | mechanical, but nothing records it |
| **`apps/jarvis-inference/CMakeLists.txt`** | **NO** | a skeleton the script only sed-patches |

**The irreducible, unreproducible delta is 19 insertions / 14 deletions across three cmake files,
plus a deletion list.** A few KB.

What those 33 lines do — all three matter:

```
CMakeLists.txt        KernelRootCNodeSizeBits 13 -> 22 ; project rename ; drop C++/ARM
easy-settings.cmake   + KernelRootCNodeSizeBits 22 ; + LibSel4MuslcSysMorecoreBytes 128 MB
settings.cmake        + KernelRootCNodeSizeBits 22   (the IOMMU line is scripted)
```

### The measurement (runs `31311547607`, `31311734013`, `31312018007`)

The first attempt failed for two reasons, **both mine**, and both are recorded because each is a
reusable lesson rather than noise:

- `ninja` died at step 29/264 on **`ModuleNotFoundError: No module named 'lxml'`**.
  `SEL4_X86_QEMU_SETUP.md` lists `protobuf`, `grpcio-tools`, `pyelftools` — **not `lxml`.** That is
  a real gap in the setup doc.
- the config gate failed **2 of 7**: `CONFIG_MAX_NUM_NODES=6` (the flag is **`-DNUM_NODES`**, as
  `easy-settings.cmake` documents and `build_jarvis_x86.sh` uses — I passed `KernelMaxNumNodes`)
  and `IOMMU disabled` (sel4test FORCE-sets it ON for non-simulation x86; `-D` **cannot** override
  it, which is precisely why the build script sed-patches the file).

Corrected, on a clean runner:

| step | seconds |
|---|---|
| dependency install (apt + pip, incl. `lxml`) | **29** |
| `repo init` + **pinned** `repo sync` | **6** (tree 119 MB) |
| cmake configure | **3** |
| **cold build** (kernel + all seL4 libs) | **21** |
| warm no-op rebuild | **0** |
| **total, cold, from nothing** | **≈ 59 s** |

- build dir 73 MB · kernel image 1,332,112 B
- **config gate: 7/7 PASS** — `MAX_NUM_NODES=6`, `SMP_SUPPORT=1`, `XSAVE=1`, `XSAVE_FEATURE_SET=7`,
  FXSAVE disabled, IOMMU disabled, `FASTPATH=1`
- CI kernel md5 `53182e3b…` (7/7 build) / `cdabb9da…` (8/8 build, CNode 22) vs box `d22affe8…` —
  **different bytes, identical config.** Bit-identity was never expected (embedded paths, build IDs)
  and is not claimed; config identity is the property that matters, and it holds.

### Cache key — and whether the config gate suffices as the staleness guard

**Recommendation: do not cache.** Sync is 6 s and a cold build is 21 s; restoring a 119 MB source
cache plus a 73 MB build cache would cost about as much as regenerating both, while introducing the
one failure mode the prompt correctly identifies as worse than having no job. Cache the apt/pip
layer at most.

**Does the build script's config-verification gate suffice as the guard? No — and the distinction
is load-bearing.** It reads **seven kernel *config* invariants** out of `gen_config.h`. A cached
build with the *right config* but *stale source* passes all seven. That is not hypothetical: the
C/M4 `undefined reference to route_veto_centroids_ok` was exactly a stale-source failure, caught
only because a rename happened to change a symbol. **If a cache is ever added, its key must cover
the JARVIS source tree hash, not just the flags** — the config gate catches a wrong-config cache,
never a stale-source one.

---

## Q3 — end-to-end cost

### The cost — answered, and stable across runs

Measured twice, independently (runs `31311734013` and `31312018007`):

| step | run 1 | run 2 |
|---|---|---|
| dependency install | 29 s | 43 s |
| pinned `repo sync` | 6 s | 8 s |
| cmake configure | 3 s | 12 s |
| **cold build** | **21 s** | **21 s** |
| warm no-op rebuild | 0 s | 0 s |
| **build subtotal, from nothing** | **59 s** | **84 s** |

Add a boot: Q1's config A ran the real image for 150 s and produced 158 `[STATS]` windows, so a
marker-only smoke needs **~60–90 s**. The sparse 11 GiB NVMe image costs **0 s and 0 bytes**.

> **End-to-end ≈ 2–4 minutes.** The `≤ ~30 min` GO band is met with an order of magnitude to spare.
> **Stability:** the cold build was **21 s in both runs**, and the two Q3 boots were **byte-identical
> (md5 `3ce263c9…`)**. Variance sits entirely in apt/pip and cmake, i.e. in network-bound setup, not
> in the build.

### The hybrid-boot leg — **INCONCLUSIVE, with the cause identified**

Q3 also attempted something neither Q1 nor Q2 could do alone: boot the **CI-built kernel** with the
**box-built rootserver**, to see whether a runner-built kernel is functionally equivalent to the
deployed one. It is **reported as INCONCLUSIVE, not PASS and not NO** (prompt rule 5).

Progress was real and much further than the first attempt: all six nodes started
(`Starting node #1 … #5`), and it reached **`Booting all finished, dropped to user space`**. Then:

```
Caught cap fault in send phase at address 0
while trying to handle:
unknown syscall 0xffffffffffffffe3
in thread 0xffffff8238005800 "rootserver" at address 0x4209f5
```

`0xffff…ffe3` is a **debug syscall** — `seL4_DebugPutChar`, which the rootserver calls almost
immediately (CLAUDE.md: *"`seL4_DebugPutChar()` works for TX without device frame mapping"*). The
box's kernel has `KernelDebugBuild=ON` and `KernelPrinting=ON`; my configure line passed neither, so
the CI kernel does not implement it and the first print faults.

**Two things follow, and the second is the one that matters.**

1. **A ninth invariant.** The build script's config gate checks seven things and **would not have
   caught this**: a kernel that satisfies all seven can still lack the syscall the rootserver needs
   on its first instruction. If A10 proceeds, `KernelDebugBuild` / `KernelPrinting` belong in that
   gate. (This is the same shape as the cache-key finding above — the gate validates *config*, and
   there are true statements about a build it cannot see.)
2. **This is an artifact of the hybrid, not an A10 blocker.** A real A10 job builds the kernel and
   the rootserver **from one configured tree with one set of flags**, so a kernel/rootserver config
   mismatch cannot arise by construction. I was deliberately mixing a CI-built half with a box-built
   half; that is a configuration A10 would never run. The leg was a bonus check, and what it
   actually diagnosed is a gap in the config gate.

**What Q3 therefore does and does not establish.** It establishes the **cost** and its **stability**
— which is what the verdict band turns on — from two independent runs. It does **not** establish
that a CI-built kernel boots the deployed rootserver; that remains untested, and the first thing the
follow-up should do is build both halves together and re-run this leg.

---

## §5 — the link tier

**The prompt's framing is inverted by the measurement, so the fallback is not the bargain it looks
like.** Linking Process A requires `libsel4` (generated), `muslc`, `sel4runtime`, `seL4_libs`,
`util_libs`, `sel4platsupport` and the elfloader — that is, essentially the whole Q2 build. Once
that exists, **booting costs a `truncate` (0 s, 0 bytes on disk, measured) plus one QEMU run.**

So the boot tier is the build tier **plus ~60–90 s**, and it catches strictly more: the whole
link/undefined-reference/CMake-breakage class *and* the runtime class (a rootserver that
ELF-loads but faults on startup, a kernel config that cannot bring up userspace — the latter is
exactly what the CNode-13 failure below turned out to be). **There is no meaningful reduced-scope
saving in stopping at link, and both tiers need the same prerequisite capture.** If the link tier
is affordable, so is the boot tier.

---

## Corrections to the prompt

Reported because §7 asks for them, and two are load-bearing.

1. **"…which `CLAUDE.md` records the committed TCG sim cannot run [AVX2]."** The *substance* is
   true and is now **proven** (config C: `XSAVE not supported` → `boot_sys failed`), but the
   **citation is wrong** — CLAUDE.md says nothing of the sort (checked with a capable search plus a
   positive control). The claim lives at **`jarvis-strategist/SKILL.md:273` and `:333`**, a file the
   current handoff already flags as carrying ~10 stale claims. The committed *evidence* is
   `qemu_test.sh`'s own fallback line. The wording is also imprecise: it never reaches AVX2 — the
   **kernel** dies at boot on XSAVE, before userspace exists.
2. **"`MODEL_FAIL_PROBE=1` removes the model-file requirement."** True that the ~2.9 GB *read* is
   skipped, but **incomplete in a way that would break a Q3 job.** The probe fires at `p == 1000`
   *inside* the frame-allocation loop (`main_x86.c:5586`), which is reached only if the file was
   **found on FAT32** and `n_pages > 1000` — i.e. the image still needs a FAT32 volume holding
   `GEMMA2B.GUF` (the 8.3 name is load-bearing) that is **larger than ~3.9 MiB**. A *missing* file
   is a different path entirely, and it is the messy one (Q1c item 2).
3. **"`$HOME/sel4-x86` is not reproducible from this repo."** Confirmed, and worse than stated (no
   commit, no remote) — but much smaller than the raw diff implies (§Q2).
4. **Internal tension:** §5 says "No box time is required or permitted", while §3 twice permits
   read-only ssh for inspection and even `scp`-ing fixtures. I read §5 as "no box *session*" and
   used read-only ssh only — no builds, no writes, nothing booted. Worth disambiguating next time.

---

## Side finding, independent of A10

**The JARVIS build tree's customization exists in exactly one place on earth** — as *uncommitted
working-tree state* in a clone with no remote for it. Not in this repo, not in any commit, not
pushed. A disk failure on the box loses the ability to reproduce the deployed build, and the loss
would be silent until someone next tried. Capturing it is the same few-KB commit A10 needs anyway,
so the prerequisite pays for itself whether or not A10 is ever built.

---

## Implementation sketch (for the follow-up prompt, if A10 proceeds)

**Step 0 — the prerequisite, and it stands alone.** Commit, under something like
`phase3/sel4-tree/`: the pinned manifest (`repo manifest -r` output, 2 KB); a patch or literal
copies of the three cmake files; the 72-path deletion list; `apps/jarvis-inference/CMakeLists.txt`.
Add a script that reconstructs `$HOME/sel4-x86` from those plus `repo sync`. **Verify it by
rebuilding on the box and comparing the resulting rootserver to a known-good md5** — the capture is
only worth what its verification proves.

**Step 1 — the job.** `workflow_dispatch` first, not on `push`; promote only once it has been quiet
for a while.

```
deps (apt+pip incl. lxml)        ~30 s
repo init + pinned sync           ~6 s
reconstruct projects/jarvis-x86   ~5 s
build_jarvis_x86.sh (kernel+libs+apps)   ~60-120 s   [apps term estimated: 81 .c / 31k LOC]
sudo chmod 666 /dev/kvm            ~0 s
truncate 11G + mkfs.vfat + dummy GEMMA2B.GUF (>3.9 MiB)   ~5 s
boot to marker (KVM -cpu host)    ~60-90 s
                                  ~= 3-5 min
```

**Step 2 — what to assert.** Build with `JARVIS_MODEL_FAIL_PROBE=1` so the degraded path is the
*clean* one (`g_model_bad` latches → dispatch refused → **no** crash-loop, **no** `[FATAL]`), and
assert on:

- `Self-Test: 5/5 PASS`
- `[MODEL-BAD] … why=frame-alloc`
- `[SNAP] … model=UNUSABLE`
- `err=0`
- `[CTRL-IN-STATS] … idx=<n>` — the durable index count `9c772f8` made observable

**Do not assert `FATAL=0` without the probe build**, and do not assert on `[CTRL-RECALL]`,
`[PANEL]`, `[ROUTE]`, `[VETO]` or `[CTRL-EPI]`: those are plain `puts_serial` and capture nothing at
`JARVIS_DBG_BOOT_LOG=0`. Serial *is* fully available in CI, unlike on the box — but assertions
written against the durable set stay portable between the two.

**Step 3 — configure flags, including the two that bit Q3.** The full set the deployed kernel needs:

```
-DPLATFORM=x86_64 -DSIMULATION=OFF -DSMP=ON -DNUM_NODES=6
-DKernelFPU=XSAVE -DKernelXSave=XSAVEOPT -DKernelXSaveFeatureSet=7 -DKernelXSaveSize=832
-DKernelFastpath=ON -DKernelDebugBuild=ON -DKernelPrinting=ON
```

`NUM_NODES` (not `KernelMaxNumNodes`) and the two debug flags are each a measured trap. IOMMU and
the CNode raise come from the cmake delta, not from `-D`.

**Step 4 — extend the config gate by two.** `build_jarvis_x86.sh`'s gate checks seven invariants and
would have passed the kernel that could not run the rootserver. Add `CONFIG_PRINTING` and
`CONFIG_DEBUG_BUILD`. Cheap, and it closes a hole this spike found by falling into it.

**Step 5 — known gaps to decide on, not silently inherit.** The 8 GiB untyped shortfall (try
`-m 12G`) and the resulting 0-worker threadpool; whether a CI boot is allowed to differ from the
deployed configuration in that respect, and if so, say so in the job's own output.

---

## Evidence

| run | what |
|---|---|
| `31310593705` | Q1a environment probe |
| `31310800988` | Q1b decisive boot, 3 configs (serial logs uploaded as artifacts) |
| `31311547607` | Q2 first attempt — the `lxml` + flag findings |
| `31311957175` | a workflow I corrupted with a stray `0x08` byte: **0 jobs, failure** — the `79b8815` parse-time signature, reproduced and caught the same minute |
| `31311734013` | Q2b corrected build (7/7 gate) + Q3 first attempt |
| `31312018007` | Q3 corrected |

Branch `spike/a10-q1` carried the throwaway workflows and the two box-built fixtures; it was deleted
after the numbers were taken. **No CI job was added, and no `CLAUDE.md` row is invalidated by a
spike** — the rows near this area all assert that `main_x86.c` is *never host-compiled*, which
remains true; booting is not host compilation.
