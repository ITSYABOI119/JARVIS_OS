# GGUF Model Loading via GRUB Multiboot Module

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load Llama 3.2 1B Q4_K_M GGUF (771MB) into Process B on bare-metal seL4 without an NVMe driver, using GRUB's multiboot module mechanism.

**Architecture:** GRUB loads the GGUF file from the USB FAT32 partition into RAM as multiboot module #1. seL4 kernel decomposes that physical memory into untypeds. Process A (rootserver) finds the model's untypeds by scanning for the right physical address range, retypes them to frames, maps them contiguously into Process B's vspace, and tells Process B the address via IPC. Process B calls `gguf_open_memory()` — same API as the embedded path, zero changes to inference code.

**Tech Stack:** seL4 x86-64, GRUB2 multiboot, C11, `vka_alloc_frame_at()` for physical→capability conversion.

---

## Background: How seL4 Handles GRUB Modules

When GRUB loads multiple modules:
- Module 0 = rootserver image (kernel loads this into userspace)
- Module 1+ = additional files (kernel leaves their physical memory as RAM untypeds)

seL4 does NOT expose module addresses through bootinfo headers. The module's physical memory simply appears in `seL4_BootInfo.untypedList[]` as regular RAM untypeds. To find the model, we must identify the correct untypeds by physical address.

**Key API already in use:**
- `vka_alloc_frame_at(&vka, seL4_PageBits, paddr, &frame)` — converts a physical address to a frame cap (used for VGA at 0xB8000 in main_x86.c:833)
- `map_frame_direct(frame.cptr, pd, vaddr, rights)` — maps a frame into a vspace at a specific virtual address (main_x86.c:629-665)
- `gguf_open_memory(ptr, size)` — parses GGUF from a memory buffer (inference_server.c:157)

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `phase3/firmware/grub/grub.cfg` | Modify | Add `module /boot/model.gguf` line |
| `phase3/scripts/reflash_usb.sh` | Modify | Copy model file to USB |
| `phase3/src/sel4/main_x86.c` | Modify | Find model untypeds, map into Process B, send address via IPC |
| `phase3/src/sel4/inference_server.c` | Modify | Accept model address from argv, load model from external memory |
| `phase3/src/ipc/shmem_ipc.h` | Modify | Add `MSG_MODEL_LOADED` message type (0x10) |

No new files needed — all changes are modifications to existing code.

---

## Task 1: Discover Model Physical Address Layout

**Files:**
- Modify: `phase3/src/sel4/main_x86.c:700-714` (untyped dump section)
- Modify: `phase3/firmware/grub/grub.cfg:25-31`

This task adds a GRUB module line and expands the untyped diagnostic dump so we can identify where GRUB places the model in physical memory.

- [ ] **Step 1: Add model module line to grub.cfg**

In `phase3/firmware/grub/grub.cfg`, add the model as module #1 in ALL menu entries:

```
menuentry "JARVIS seL4 x86-64" {
    echo 'Loading seL4 kernel...'
    multiboot /boot/kernel-x86_64-pc99
    echo 'Loading JARVIS rootserver...'
    module /boot/jarvis-x86-image-x86_64-pc99
    echo 'Loading GGUF model (771 MB)...'
    module /boot/model.gguf
    boot
}
```

Repeat for the UEFI entry (using `module2` instead of `module`) and the serial-only entry.

- [ ] **Step 2: Expand untyped dump to show ALL untypeds**

In `main_x86.c`, find the untyped iteration loop (line 700) that currently prints only the first 10. Change the limit from 10 to `ut_count` (print all), and add a filter to highlight RAM untypeds > 1MB:

```c
    /* Dump ALL untypeds — look for model */
    int ut_count = simple_get_untyped_count(&simple);
    puts_serial("[JARVIS] Untyped regions ("); put_dec((uint32_t)ut_count);
    puts_serial(" total):\n");
    size_t total_bytes = 0;
    size_t model_candidate_paddr = 0;
    size_t model_candidate_size = 0;
    for (int i = 0; i < ut_count; i++) {
        size_t sz = 0;
        uintptr_t paddr = 0;
        bool device = false;
        simple_get_nth_untyped(&simple, i, &sz, &paddr, &device);
        if (!device && sz >= (1 << 20)) {
            /* Print large RAM untypeds (>= 1MB) — model will be among these */
            puts_serial("  ut["); put_dec((uint32_t)i);
            puts_serial("]: "); put_dec((uint32_t)(sz >> 20));
            puts_serial("MB @ "); put_hex((uint32_t)(paddr >> 20));
            puts_serial("M\n");
        }
        if (!device) total_bytes += sz;
    }
    puts_serial("[JARVIS] Total RAM: ");
    put_dec((uint32_t)(total_bytes >> 20)); puts_serial(" MB\n");
```

- [ ] **Step 3: Update reflash script to copy model**

In `phase3/scripts/reflash_usb.sh`, after the line that copies grub.cfg, add:

```bash
# Copy model file if present
MODEL="$JARVIS/phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf"
if [ -f "$MODEL" ]; then
    echo "Copying model ($(du -h "$MODEL" | cut -f1))..."
    cp "$MODEL" /mnt/usb/boot/model.gguf
else
    echo "WARNING: Model not found at $MODEL — skipping"
fi
```

- [ ] **Step 4: Build, reflash, boot — capture untyped dump**

```bash
# On JARVIS PC:
cd ~/Desktop/JARVIS_OS
./phase3/scripts/build_jarvis_x86.sh
sudo bash phase3/scripts/reflash_usb.sh /dev/sdX
# Reboot, read VGA output, note large RAM untypeds
```

Take a photo or write down the physical addresses and sizes of all large RAM untypeds. Compare with a boot WITHOUT the model module — the difference reveals the model's physical range.

- [ ] **Step 5: Commit**

```bash
git add phase3/firmware/grub/grub.cfg phase3/src/sel4/main_x86.c phase3/scripts/reflash_usb.sh
git commit -m "diag: expand untyped dump + GRUB model module for physical address discovery"
```

---

## Task 2: Implement Model Frame Mapping in Process A

**Files:**
- Modify: `phase3/src/sel4/main_x86.c` (spawn_inference_process function)

After Task 1, we know the model's physical address range from the untyped dump. This task adds the code to find, retype, and map those frames into Process B.

- [ ] **Step 1: Add model mapping constants**

Near the top of main_x86.c, after the SHMEM_VADDR defines (line 668):

```c
/* Model loading via GRUB multiboot module.
 * GRUB loads model.gguf into RAM. seL4 exposes it as untypeds.
 * We map those frames into Process B's vspace. */
#define MODEL_VADDR_B   0x60000000UL  /* Virtual address in Process B */
#define MODEL_MAX_PAGES (800 * 1024 * 1024 / 4096)  /* 800MB / 4KB = 204800 pages max */
```

- [ ] **Step 2: Add model discovery function**

After `map_frame_direct()` (line 665), add a function to find the model in the untyped list. The model is the largest contiguous RAM region that isn't being used by the allocman/vspace system. In practice, GRUB places modules after the rootserver image, so the model will be at a predictable address.

```c
/* Find the GRUB-loaded model in bootinfo untypeds.
 * Returns number of contiguous pages found, or 0 if not found.
 * model_paddr_out: physical start address of model.
 *
 * Strategy: find all large RAM untypeds that form a contiguous
 * physical region of ~771MB. GRUB places modules sequentially
 * in physical memory after the rootserver image. */
static int find_model_untypeds(uintptr_t *model_paddr_out,
                                size_t *model_size_out)
{
    int ut_count = simple_get_untyped_count(&simple);

    /* Collect all RAM untypeds sorted by physical address.
     * We only care about untypeds > 64KB (model is 771MB). */
    struct { uintptr_t paddr; size_t size; } candidates[256];
    int n_cand = 0;

    for (int i = 0; i < ut_count && n_cand < 256; i++) {
        size_t sz = 0;
        uintptr_t paddr = 0;
        bool device = false;
        simple_get_nth_untyped(&simple, i, &sz, &paddr, &device);
        if (!device && sz >= (1 << 16)) {  /* >= 64KB */
            candidates[n_cand].paddr = paddr;
            candidates[n_cand].size = sz;
            n_cand++;
        }
    }

    /* Find contiguous group totaling >= 700MB.
     * GRUB module memory appears as adjacent buddy-system untypeds. */
    for (int start = 0; start < n_cand; start++) {
        uintptr_t region_start = candidates[start].paddr;
        uintptr_t region_end = region_start + candidates[start].size;
        size_t total = candidates[start].size;

        for (int j = start + 1; j < n_cand; j++) {
            if (candidates[j].paddr == region_end) {
                region_end += candidates[j].size;
                total += candidates[j].size;
            } else {
                break;  /* Gap — not contiguous */
            }
        }

        if (total >= 700 * 1024 * 1024) {  /* >= 700MB — found the model */
            *model_paddr_out = region_start;
            *model_size_out = total;
            return 0;
        }
    }

    return -1;  /* Not found */
}
```

**Note:** This heuristic works because 771MB is uniquely large among the GRUB modules. No other contiguous RAM region of this size will exist unless you have > 700MB of rootserver. Adjust the 700MB threshold if needed after Task 1 data.

- [ ] **Step 3: Add model frame mapping into Process B**

In `spawn_inference_process()`, after the shared memory mapping (line ~790), add model mapping:

```c
    /* ---- Map GRUB-loaded model into Process B ---- */
    uintptr_t model_paddr = 0;
    size_t model_size = 0;
    int model_found = find_model_untypeds(&model_paddr, &model_size);

    size_t model_pages = 0;
    if (model_found == 0) {
        puts_serial("[JARVIS] Model found: ");
        put_dec((uint32_t)(model_size >> 20)); puts_serial("MB @ paddr ");
        put_hex((uint32_t)(model_paddr >> 20)); puts_serial("M\n");

        model_pages = model_size / 4096;
        puts_serial("[JARVIS] Mapping "); put_dec((uint32_t)model_pages);
        puts_serial(" frames into Process B...\n");

        seL4_CPtr pd_b = inference_process.pd.cptr;
        int map_errors = 0;
        for (size_t pg = 0; pg < model_pages; pg++) {
            vka_object_t frame;
            uintptr_t frame_paddr = model_paddr + pg * 4096;
            int err = vka_alloc_frame_at(&vka, seL4_PageBits, frame_paddr, &frame);
            if (err) {
                if (map_errors == 0) {
                    puts_serial("[JARVIS] Model frame alloc failed at page ");
                    put_dec((uint32_t)pg); puts_serial(", paddr ");
                    put_hex((uint32_t)(frame_paddr >> 12)); puts_serial("000\n");
                }
                map_errors++;
                continue;
            }

            /* Duplicate cap for Process B */
            cspacepath_t src, dest;
            vka_cspace_make_path(&vka, frame.cptr, &src);
            err = vka_cspace_alloc_path(&vka, &dest);
            if (err) { map_errors++; continue; }
            err = vka_cnode_copy(&dest, &src, seL4_AllRights);
            if (err) { map_errors++; continue; }

            err = map_frame_direct(dest.capPtr, pd_b,
                MODEL_VADDR_B + pg * 4096, seL4_AllRights);
            if (err) {
                if (map_errors < 3) {
                    puts_serial("[JARVIS] Model frame map failed at page ");
                    put_dec((uint32_t)pg); puts_serial("\n");
                }
                map_errors++;
            }

            /* Progress every 10000 pages (~40MB) */
            if (pg > 0 && pg % 10000 == 0) {
                puts_serial("  "); put_dec((uint32_t)(pg * 4 / 1024));
                puts_serial("MB mapped\n");
            }
        }

        if (map_errors > 0) {
            puts_serial("[JARVIS] Model mapping: ");
            put_dec((uint32_t)map_errors); puts_serial(" errors out of ");
            put_dec((uint32_t)model_pages); puts_serial(" pages\n");
        } else {
            puts_serial("[JARVIS] Model mapped: ");
            put_dec((uint32_t)(model_pages * 4 / 1024));
            puts_serial("MB at Process B vaddr ");
            put_hex((uint32_t)MODEL_VADDR_B); puts_serial("\n");
        }
    } else {
        puts_serial("[JARVIS] No GRUB model module detected — Process B will idle\n");
    }
```

- [ ] **Step 4: Pass model address to Process B via argv**

Currently Process B gets 3 argv: `req_notif`, `resp_notif`, `shmem_vaddr`. Add two more: `model_vaddr` and `model_size`. In the argv construction section (around line 800):

```c
    /* Build argv for Process B: [req_notif, resp_notif, shmem_vaddr, model_vaddr, model_size] */
    char arg0[24], arg1[24], arg2[24], arg3[24], arg4[24];
    snprintf(arg0, sizeof(arg0), "%lu", (unsigned long)remote_req_notif);
    snprintf(arg1, sizeof(arg1), "%lu", (unsigned long)remote_resp_notif);
    snprintf(arg2, sizeof(arg2), "%lu", (unsigned long)SHMEM_VADDR_B);
    snprintf(arg3, sizeof(arg3), "%lu", (unsigned long)(model_found == 0 ? MODEL_VADDR_B : 0));
    snprintf(arg4, sizeof(arg4), "%lu", (unsigned long)(model_found == 0 ? model_size : 0));
    char *argv_remote[] = { arg0, arg1, arg2, arg3, arg4 };
    int remote_argc = 5;
```

- [ ] **Step 5: Commit**

```bash
git add phase3/src/sel4/main_x86.c
git commit -m "feat: map GRUB-loaded model into Process B vspace via frame_at"
```

---

## Task 3: Accept External Model in Process B

**Files:**
- Modify: `phase3/src/sel4/inference_server.c`

Process B currently loads the model from embedded .rodata (`JARVIS_HAS_MODEL`). Add a second path: accept model address + size from argv.

- [ ] **Step 1: Parse model argv in Process B**

In `inference_server.c main()`, after parsing the existing 3 args (line 129-131), add:

```c
    /* Parse model location (passed by Process A if GRUB module loaded) */
    uintptr_t model_vaddr = 0;
    size_t model_size = 0;
    if (argc >= 5) {
        model_vaddr = (uintptr_t)atol(argv[3]);
        model_size = (size_t)atol(argv[4]);
    }
```

- [ ] **Step 2: Add external model loading path**

Replace the `#ifdef JARVIS_HAS_MODEL` block (lines 150-265) with logic that handles three cases:

```c
    /* ---- Model loading ---- */
    const void *model_data = NULL;
    size_t model_data_size = 0;

#ifdef JARVIS_HAS_MODEL
    /* Path 1: Embedded model (QEMU builds with objcopy) */
    model_data = _binary_model_gguf_start;
    model_data_size = (size_t)(_binary_model_gguf_end - _binary_model_gguf_start);
    puts_serial("[Process B] Model source: embedded .rodata\n");
#endif

    if (!model_data && model_vaddr != 0 && model_size > 0) {
        /* Path 2: GRUB multiboot module (mapped by Process A) */
        model_data = (const void *)model_vaddr;
        model_data_size = model_size;
        puts_serial("[Process B] Model source: GRUB module at vaddr ");
        put_dec((uint32_t)(model_vaddr >> 20)); puts_serial("M, ");
        put_dec((uint32_t)(model_size >> 20)); puts_serial("MB\n");
    }

    if (!model_data) {
        /* Path 3: No model available */
        puts_serial("[Process B] No model available — idle mode\n");
        shmem_ipc_send(response_ring, MSG_HEARTBEAT_ACK, 0, NULL, 0);
        seL4_Signal(resp_notif);
        goto idle;
    }

    /* Common model loading path (works for both embedded and GRUB module) */
    puts_serial("[Process B] Model: ");
    put_dec((uint32_t)(model_data_size / (1024*1024)));
    puts_serial(" MB\n");

    gguf_ctx_t gguf_ctx;
    int err = gguf_open_memory(&gguf_ctx, model_data, model_data_size);
    if (err) {
        puts_serial("[Process B] GGUF parse failed\n");
        goto idle;
    }

    qmodel_t qm;
    err = qmodel_load(&qm, &gguf_ctx, model_data);
    /* ... rest of existing model loading code (tokenizer, state alloc, etc.) ... */
```

The key insight: `gguf_open_memory()` and `qmodel_load()` only need a pointer and size. They don't care whether the bytes come from .rodata or GRUB-mapped frames. Zero changes to the parsing or inference code.

- [ ] **Step 3: Keep the existing IPC loop and cleanup unchanged**

The IPC loop (lines 214-251) and cleanup code work the same regardless of model source. No changes needed.

- [ ] **Step 4: Commit**

```bash
git add phase3/src/sel4/inference_server.c
git commit -m "feat: accept GRUB-loaded model via argv (model_vaddr + model_size)"
```

---

## Task 4: Test in QEMU

**Files:**
- No file changes — testing only

QEMU can simulate the GRUB multiboot module approach.

- [ ] **Step 1: Create a QEMU launch script with model module**

```bash
# In ~/sel4-x86/jbuild:
qemu-system-x86_64 \
    -cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce \
    -nographic -serial mon:stdio -m 8G \
    -kernel images/kernel-x86_64-pc99 \
    -initrd "images/jarvis-x86-image-x86_64-pc99 path/to/model.gguf"
```

Note: QEMU `-initrd` with space-separated paths loads multiple multiboot modules. Module 0 = rootserver, module 1 = model.

- [ ] **Step 2: Verify untyped dump shows model**

Boot in QEMU. Check VGA/serial output for:
```
[JARVIS] Model found: 771MB @ paddr XXXM
[JARVIS] Mapping 197376 frames into Process B...
  40MB mapped
  80MB mapped
  ...
[JARVIS] Model mapped: 771MB at Process B vaddr 0x60000000
```

- [ ] **Step 3: Verify Process B loads model**

Check for:
```
[Process B] Model source: GRUB module at vaddr 1536M, 771MB
[Process B] Model: 771 MB
[Process B] Model loaded: 16 layers, 128256 vocab
[Process B] Tokenizer ready: 128256 tokens
[Process B] Ready for inference requests
```

- [ ] **Step 4: Verify inference works**

Check for Process A test query response:
```
[JARVIS] Response: "a microkernel implementation..."
```

If this works, the approach is validated. Proceed to bare metal.

---

## Task 5: Test on Bare Metal

**Files:**
- Modify: `phase3/scripts/reflash_usb.sh` (already done in Task 1)

- [ ] **Step 1: Copy model to USB**

```bash
# On JARVIS PC:
sudo bash phase3/scripts/reflash_usb.sh /dev/sdX
# Script now copies model.gguf automatically
```

Verify USB contents:
```
/boot/kernel-x86_64-pc99         (~9 MB)
/boot/jarvis-x86-image-x86_64-pc99  (~10 MB)
/boot/model.gguf                 (~771 MB)
/boot/grub/grub.cfg
```

- [ ] **Step 2: Boot and verify**

Insert USB, boot from USB, select "JARVIS seL4 x86-64" from GRUB menu. Watch VGA output for:
1. "Loading GGUF model (771 MB)..." in GRUB
2. "Model found: 771MB @ paddr ..." from Process A
3. "Model loaded: 16 layers" from Process B
4. Inference response text

- [ ] **Step 3: Debug if needed**

If model not found: check untyped dump, compare with QEMU addresses.
If frame mapping fails: CNode may be too small (currently 22 = 4M slots; 197K frames + existing caps should fit).
If GGUF parse fails: model data may not be page-aligned or contiguous mapping has gaps.

- [ ] **Step 4: Commit and document**

```bash
git add -A
git commit -m "feat: GRUB multiboot model loading verified on bare metal"
```

---

## Task 6: Update Documentation

**Files:**
- Modify: `CLAUDE.md`
- Modify: `phase3/docs/BARE_METAL_BOOT_GUIDE.md`

- [ ] **Step 1: Update CLAUDE.md milestone table**

Add:
```
| GGUF model loaded via GRUB multiboot module | DONE |
| End-to-end inference on bare metal (no NVMe) | DONE |
```

- [ ] **Step 2: Update boot guide with model loading**

Add section to BARE_METAL_BOOT_GUIDE.md explaining:
- Model file must be on USB as `/boot/model.gguf`
- GRUB loads it into RAM at boot (~10-30 seconds depending on USB speed)
- Process A maps it into Process B's memory
- Expected output on VGA

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md phase3/docs/BARE_METAL_BOOT_GUIDE.md
git commit -m "docs: GRUB multiboot model loading guide"
```

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| GRUB can't load 771MB module from FAT32 | Low | High | Multiboot spec allows modules up to available RAM; FAT32 supports 4GB files |
| Model untypeds not contiguous | Medium | Medium | Adjust `find_model_untypeds` heuristic; or hardcode physical address from Task 1 |
| 197K frame allocs exhaust CNode | Low | High | CNode=22 = 4M slots; 197K << 4M; monitor with `put_dec(vka_cspace_alloc_count)` |
| Frame mapping takes too long | Medium | Low | ~197K syscalls at ~1μs each ≈ 0.2s; acceptable |
| Model not page-aligned | Very Low | High | Multiboot spec guarantees page alignment |
| QEMU and bare-metal have different untyped layouts | High | Medium | Task 1 discovers addresses experimentally; Task 2 uses heuristic |

## Effort Estimate

| Task | Hours | Notes |
|------|-------|-------|
| Task 1: Discover layout | 2-3 | Mostly boot + observe |
| Task 2: Frame mapping | 3-4 | Bulk of the code |
| Task 3: Process B changes | 1-2 | Simple refactor |
| Task 4: QEMU test | 1-2 | Debug cycle |
| Task 5: Bare metal test | 2-3 | Debug cycle |
| Task 6: Documentation | 1 | |
| **Total** | **10-15** | ~2 sessions |
