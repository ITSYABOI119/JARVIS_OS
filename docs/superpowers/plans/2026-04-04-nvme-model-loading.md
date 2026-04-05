# NVMe Full Model Loading — Read 770MB GGUF into Process B

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Read the 770MB Llama 3.2 1B Q4_K_M GGUF model from the NVMe FAT32 partition into allocated frames, map them into Process B's vspace at 0x60000000, and run inference — replacing the embedded .rodata approach.

**Architecture:** Rootserver allocates ~197K frames via `vka_alloc_frame`, reads model data cluster-by-cluster using `fat32_read_file` (which calls the NVMe bounce-buffer callback), then maps all frames into Process B before spawn. Process B receives model_vaddr + model_size as argv[3] + argv[4] and calls `gguf_open_memory()` — same API, zero inference code changes.

**Tech Stack:** seL4 x86-64, NVMe polled I/O, FAT32 cluster chain, `vka_alloc_frame` + `vspace_map_pages` for frame management.

---

## Key Insight: fat32_read_file Already Exists

`fat32.c` has `fat32_read_file(fs, first_cluster, file_size, buf)` that follows the cluster chain and calls the read callback for each cluster. It expects `buf` to be a contiguous buffer large enough for the entire file.

**Problem:** We can't allocate a single 770MB contiguous buffer on seL4. Frames are 4KB each.

**Solution:** Don't use `fat32_read_file`. Instead, manually follow the cluster chain (using `fat32_next_cluster` — currently static in fat32.c, needs to be exported) and read one cluster at a time into individually allocated frames.

**Alternative solution:** Allocate frames, map them contiguously into the rootserver's vspace (vspace_map_pages handles this), then call `fat32_read_file` with the contiguous virtual address. The virtual mapping creates the illusion of a contiguous buffer even though the physical frames are scattered. This is simpler and reuses existing code.

**Chosen approach:** Alternative — map frames contiguously first, read with `fat32_read_file`, then remap to Process B.

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `phase3/src/sel4/main_x86.c` | Modify | Allocate frames, map contiguously, read model, remap to Process B, expand argv |
| `phase3/src/sel4/inference_server.c` | No changes | Already accepts argv[3]/argv[4] for model_vaddr/model_size |
| `phase3/src/drivers/fat32.c` | No changes | `fat32_read_file` works as-is with contiguous virtual buffer |
| `phase3/src/drivers/nvme.c` | No changes | `nvme_read_sectors` works as-is |
| `phase3/scripts/build_jarvis_x86.sh` | No changes | Already syncs all needed files |

---

## Task 1: Allocate Model Frames and Map Contiguously in Rootserver

**Files:**
- Modify: `phase3/src/sel4/main_x86.c`

Replace the current GGUF-magic-check block (after `fat32_find_file` succeeds) with full model loading.

- [ ] **Step 1: Add model frame allocation after MODEL.GUF is found**

After `fat32_find_file` returns success with `model_cluster` and `model_size`, add:

```c
/* Allocate frames for the model */
uint32_t n_pages = (model_size + 4095) / 4096;
puts_serial("[JARVIS] Allocating ");
put_dec(n_pages); puts_serial(" frames for model (");
put_dec(model_size >> 20); puts_serial("MB)...\n");

/* We need to store frame caps for later mapping to Process B.
 * 197K caps × 4 bytes = 788KB — use a static array. */
#define MODEL_MAX_PAGES (200 * 1024)  /* 200K pages = 800MB max */
static seL4_CPtr model_frame_caps[MODEL_MAX_PAGES];
if (n_pages > MODEL_MAX_PAGES) {
    puts_serial("[JARVIS] Model too large\n");
    goto skip_model;
}

/* Allocate all frames and map them contiguously into rootserver vspace.
 * vspace_new_pages would be simpler but doesn't give us individual caps.
 * Instead: alloc frames, then map as a batch. */
int alloc_ok = 1;
for (uint32_t p = 0; p < n_pages; p++) {
    vka_object_t frame;
    int err = vka_alloc_frame(&vka, seL4_PageBits, &frame);
    if (err) {
        puts_serial("[JARVIS] Frame alloc failed at page ");
        put_dec(p); puts_serial("\n");
        alloc_ok = 0;
        n_pages = p;  /* Use what we got */
        break;
    }
    model_frame_caps[p] = frame.cptr;

    /* Progress every 50K pages (~200MB) */
    if (p > 0 && p % 50000 == 0) {
        put_dec(p * 4 / 1024); puts_serial("MB allocated\n");
    }
}
puts_serial("[JARVIS] Allocated "); put_dec(n_pages);
puts_serial(" frames\n");
```

- [ ] **Step 2: Map frames contiguously into rootserver vspace**

```c
/* Map all model frames contiguously into our vspace.
 * vspace_map_pages takes an array of caps and maps them contiguously. */
void *model_vaddr_local = vspace_map_pages(&vspace,
    model_frame_caps, NULL, seL4_AllRights, n_pages, seL4_PageBits, 1);

if (!model_vaddr_local) {
    puts_serial("[JARVIS] Failed to map model frames in rootserver\n");
    goto skip_model;
}
puts_serial("[JARVIS] Model mapped at rootserver vaddr ");
put_hex((uint32_t)(uintptr_t)model_vaddr_local); puts_serial("\n");
```

Note: `cacheable=1` here — we need writeable cache-coherent access to fill the buffer. The NVMe bounce buffer (dma[4]) is the only page that needs to be DMA-capable.

- [ ] **Step 3: Read model via fat32_read_file into the contiguous mapping**

```c
/* Read entire model from FAT32 into the contiguous virtual mapping.
 * fat32_read_file follows the cluster chain, calling fat32_nvme_read
 * for each cluster. Each cluster = 8 sectors × 512B = 4KB.
 * 770MB / 4KB = ~197K read callbacks. */
puts_serial("[JARVIS] Reading model from NVMe FAT32...\n");
int read_err = fat32_read_file(&fs, model_cluster, model_size,
                                model_vaddr_local);
if (read_err != 0) {
    puts_serial("[JARVIS] Model read failed: err=");
    put_dec((uint32_t)(-read_err)); puts_serial("\n");
    goto skip_model;
}

/* Verify GGUF magic at start of loaded data */
uint32_t magic = 0;
memcpy(&magic, model_vaddr_local, 4);
if (magic != 0x46554747) {
    puts_serial("[JARVIS] GGUF magic mismatch after load: ");
    put_hex(magic); puts_serial("\n");
    goto skip_model;
}
puts_serial("[JARVIS] Model loaded: "); put_dec(model_size >> 20);
puts_serial("MB, GGUF magic OK\n");
```

- [ ] **Step 4: Add `skip_model` label and flow control**

The existing NVMe/FAT32 code is deeply nested. Add a `skip_model:` label just before the Process B spawn section, and a variable to track whether model loading succeeded:

```c
static int nvme_model_loaded = 0;
static uint32_t nvme_model_size = 0;
static uint32_t nvme_model_n_pages = 0;
/* ... after successful read ... */
nvme_model_loaded = 1;
nvme_model_size = model_size;
nvme_model_n_pages = n_pages;

skip_model:  /* Jump here on any model loading failure */
```

- [ ] **Step 5: Commit**

```bash
git add phase3/src/sel4/main_x86.c
git commit -m "feat: allocate frames + read 770MB model from NVMe FAT32 into rootserver"
```

---

## Task 2: Map Model Frames into Process B and Expand argv

**Files:**
- Modify: `phase3/src/sel4/main_x86.c` (spawn_inference_process function)

- [ ] **Step 1: Re-add model parameters to spawn_inference_process**

Change signature back to accept model info:

```c
static int spawn_inference_process(seL4_CPtr *req_notif_out, seL4_CPtr *resp_notif_out,
                                    seL4_CPtr *model_caps, uint32_t model_n_pages,
                                    uint32_t model_size)
```

- [ ] **Step 2: Map model frames into Process B after shared memory mapping**

After the `"Shared memory mapped in Process B"` line, add model frame mapping:

```c
/* Map NVMe-loaded model frames into Process B at MODEL_VADDR_B */
if (model_caps && model_n_pages > 0) {
    puts_serial("[JARVIS] Mapping "); put_dec(model_n_pages);
    puts_serial(" model frames into Process B...\n");

    seL4_CPtr pd_b = inference_process.pd.cptr;
    int map_ok = 0, map_err = 0;
    for (uint32_t p = 0; p < model_n_pages; p++) {
        cspacepath_t src, dest;
        vka_cspace_make_path(&vka, model_caps[p], &src);
        int err = vka_cspace_alloc_path(&vka, &dest);
        if (err) { map_err++; continue; }
        err = vka_cnode_copy(&dest, &src, seL4_AllRights);
        if (err) { map_err++; continue; }
        err = map_frame_direct(dest.capPtr, pd_b,
            MODEL_VADDR_B + (size_t)p * 4096, seL4_AllRights);
        if (err) { map_err++; continue; }
        map_ok++;

        if (p > 0 && p % 50000 == 0) {
            put_dec(map_ok * 4 / 1024); puts_serial("MB mapped\n");
        }
    }
    puts_serial("[JARVIS] Model mapped: "); put_dec(map_ok);
    puts_serial("/"); put_dec(model_n_pages);
    puts_serial(" pages ("); put_dec(map_ok * 4 / 1024);
    puts_serial("MB)\n");
}
```

- [ ] **Step 3: Expand argv from 3 to 5**

```c
char arg0[32], arg1[32], arg2[32], arg3[32], arg4[32];
snprintf(arg0, sizeof(arg0), "%lu", (unsigned long)remote_req_notif);
snprintf(arg1, sizeof(arg1), "%lu", (unsigned long)remote_resp_notif);
snprintf(arg2, sizeof(arg2), "%lu", (unsigned long)remote_vaddr);
snprintf(arg3, sizeof(arg3), "%lu", (unsigned long)(model_n_pages > 0 ? MODEL_VADDR_B : 0));
snprintf(arg4, sizeof(arg4), "%lu", (unsigned long)model_size);
char *argv[] = { arg0, arg1, arg2, arg3, arg4 };

error = sel4utils_spawn_process_v(&inference_process, &vka, &vspace, 5, argv, 1);
```

- [ ] **Step 4: Update the call site in main_continued**

```c
if (spawn_inference_process(&req_notif, &resp_notif,
                             nvme_model_loaded ? model_frame_caps : NULL,
                             nvme_model_loaded ? nvme_model_n_pages : 0,
                             nvme_model_loaded ? nvme_model_size : 0) != 0) {
```

- [ ] **Step 5: Commit**

```bash
git add phase3/src/sel4/main_x86.c
git commit -m "feat: map model frames into Process B + expand argv to 5 args"
```

---

## Task 3: Add Progress Reporting to fat32_read_file

**Files:**
- Modify: `phase3/src/sel4/main_x86.c` (the fat32_nvme_read callback)

Reading 770MB via the bounce buffer means ~197K NVMe read commands. Add progress output.

- [ ] **Step 1: Add a read counter to the bounce buffer callback**

```c
static uint64_t g_nvme_read_count = 0;

static int fat32_nvme_read(uint64_t lba, uint32_t count, void *buf)
{
    if (!g_nvme_ptr || !g_nvme_bounce_vaddr) return -1;
    uint8_t *out = (uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        int err = nvme_read_sectors(g_nvme_ptr, lba + i, 1,
                                     g_nvme_bounce_vaddr, g_nvme_bounce_paddr);
        if (err) return err;
        memcpy(out + (size_t)i * 512, g_nvme_bounce_vaddr, 512);
        g_nvme_read_count++;

        /* Progress every 100K sectors (~50MB) */
        if (g_nvme_read_count % 100000 == 0) {
            puts_serial("  ");
            put_dec((uint32_t)(g_nvme_read_count * 512 / (1024 * 1024)));
            puts_serial("MB read\n");
        }
    }
    return 0;
}
```

- [ ] **Step 2: Reset counter before model read**

```c
g_nvme_read_count = 0;
puts_serial("[JARVIS] Reading model from NVMe FAT32...\n");
int read_err = fat32_read_file(&fs, model_cluster, model_size, model_vaddr_local);
```

- [ ] **Step 3: Commit**

```bash
git add phase3/src/sel4/main_x86.c
git commit -m "feat: progress reporting during 770MB model read from NVMe"
```

---

## Task 4: Test in QEMU

**Files:**
- No code changes — testing only

- [ ] **Step 1: Create FAT32 image with model**

```bash
dd if=/dev/zero of=/tmp/jarvis_data.img bs=1M count=800
mkfs.fat -F 32 -n JARVIS_DATA /tmp/jarvis_data.img
sudo mkdir -p /tmp/mnt_j && sudo mount /tmp/jarvis_data.img /tmp/mnt_j
sudo cp ~/Desktop/JARVIS_OS/phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf /tmp/mnt_j/MODEL.GUF
sudo umount /tmp/mnt_j
```

- [ ] **Step 2: Build and boot QEMU with NVMe**

```bash
cd ~/Desktop/JARVIS_OS && ./phase3/scripts/build_jarvis_x86.sh
cd ~/sel4-x86/jbuild && qemu-system-x86_64 \
  -cpu Nehalem,-vme,+pdpe1gb,+syscall,+lm,enforce \
  -nographic -serial mon:stdio -m 8G \
  -kernel images/kernel-x86_64-pc99 \
  -initrd images/sel4test-driver-image-x86_64-pc99 \
  -drive file=/tmp/jarvis_data.img,if=none,id=nvme0,format=raw \
  -device nvme,serial=JARVIS_DATA,drive=nvme0
```

- [ ] **Step 3: Verify output**

Expected sequence:
```
[JARVIS] NVMe IDENTIFY: "QEMU NVMe Ctrl" serial="JARVIS_DATA" ...
[JARVIS] FAT32 init OK: 8 sec/cluster, 512 B/sec
[JARVIS] Model file: cluster=3 size=770MB
[JARVIS] Allocating 197376 frames for model (770MB)...
  200MB allocated
  400MB allocated
  600MB allocated
[JARVIS] Allocated 197376 frames
[JARVIS] Model mapped at rootserver vaddr 0x????????
[JARVIS] Reading model from NVMe FAT32...
  50MB read
  100MB read
  ...
  750MB read
[JARVIS] Model loaded: 770MB, GGUF magic OK
[JARVIS] Spawning inference process...
[JARVIS] Mapping 197376 model frames into Process B...
  200MB mapped
  400MB mapped
  600MB mapped
[JARVIS] Model mapped: 197376/197376 pages (770MB)
[JARVIS] Process B started
[Process B] Model source: NVMe at 1536M (770MB)
[Process B] Model loaded: 16 layers, 128256 vocab
[Process B] Tokenizer ready: 128256 tokens
[Process B] Ready for inference requests
[JARVIS] Process B ready
[JARVIS] Query: "The seL4 microkernel is"
[JARVIS] Response: "a microkernel implementation..."
```

- [ ] **Step 4: Commit docs update**

```bash
git add CLAUDE.md
git commit -m "docs: NVMe model loading verified — 770MB GGUF read from FAT32 in QEMU"
```

---

## Task 5: Update CLAUDE.md and Documentation

**Files:**
- Modify: `CLAUDE.md`
- Modify: `phase3/docs/BARE_METAL_BOOT_GUIDE.md`

- [ ] **Step 1: Update CLAUDE.md milestones**

Add:
```
| NVMe full model loading (770MB GGUF from FAT32) | DONE |
| Runtime inference without embedded model | DONE |
```

Update "What remains":
```
- NVMe on bare metal (Lexar NM790 may need HMB setup)
- Intel I211 NIC driver (PCI 8086:1539)
- Continuous IPC request loop
- 30-day stability test on x86
```

- [ ] **Step 2: Update boot guide**

Add section on NVMe model loading:
- How to create FAT32 partition on NVMe
- How to copy model file
- Expected boot output with NVMe model loading

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md phase3/docs/BARE_METAL_BOOT_GUIDE.md
git commit -m "docs: NVMe runtime model loading — no more embedded binary"
```

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| 197K frame allocations exhaust CNode | Low | High | CNode=22 = 4M slots; 197K + existing ~10K = well under limit |
| vspace_map_pages fails for 197K pages | Medium | High | May need to map in batches; test and see |
| fat32_read_file reads wrong clusters | Low | Medium | GGUF magic check at end verifies data integrity |
| QEMU NVMe too slow for 770MB | Medium | Low | ~197K commands × ~5μs ≈ 1 second; acceptable |
| Frame mapping into Process B too slow | Low | Low | 197K cap copies × ~1μs ≈ 0.2 seconds |
| 8GB QEMU RAM insufficient | Low | High | 770MB model + 2GB seL4 + 1GB rootserver = ~4GB; 8GB is enough |
| Bare metal: Lexar NM790 needs HMB | High | Medium | QEMU first; add HMB as separate task if bare metal fails |

## Effort Estimate

| Task | Hours | Notes |
|------|-------|-------|
| Task 1: Frame alloc + read | 2-3 | Main complexity: contiguous mapping |
| Task 2: Process B mapping | 1-2 | Same pattern as GRUB module attempt |
| Task 3: Progress reporting | 0.5 | Simple counter |
| Task 4: QEMU test | 1-2 | Debug cycle |
| Task 5: Documentation | 0.5 | |
| **Total** | **5-8** | ~1 session |

## Key Decision: Contiguous Virtual Mapping

The plan uses `vspace_map_pages` to create a contiguous virtual mapping of all 197K frames in the rootserver. This lets `fat32_read_file` write to a single contiguous buffer. The physical frames are scattered but the virtual address space is contiguous.

After the read completes, we duplicate the frame caps and map them into Process B at MODEL_VADDR_B (0x60000000). Process B sees them as contiguous too.

This avoids modifying fat32.c and keeps the read path simple.
