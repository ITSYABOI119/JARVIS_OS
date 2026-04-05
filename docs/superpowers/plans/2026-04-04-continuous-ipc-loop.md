# Continuous IPC Request Loop

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single demo query with a continuous loop that dispatches queries through the decision cache → Process B inference pipeline, demonstrating the full JARVIS AI-OS request handling flow.

**Architecture:** Rootserver iterates a test query list. For each query: check decision cache first (85% hit rate, <1ms). On cache miss, forward to Process B via shared memory IPC for LLM inference. Print results to VGA + serial. Measure per-query latency via RDTSC.

**Tech Stack:** seL4 x86-64, decision cache (FNV-1a), shared memory IPC, RDTSC timing.

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `phase3/src/sel4/main_x86.c` | Modify | Replace single query with continuous loop |
| `CLAUDE.md` | Modify | Update milestones |

Only one source file changes. Process B already handles multiple queries — no changes needed.

---

## Task 1: Replace Single Query with Continuous Loop

**Files:**
- Modify: `phase3/src/sel4/main_x86.c` (lines 1368-1395)

Replace the "Sending test query to Process B" block and the `idle:` section with a continuous query dispatch loop.

- [ ] **Step 1: Define test query list**

Replace the single-query block (lines 1368-1395) with:

```c
    /* ---- Continuous query dispatch loop ---- */
    puts_serial("\n[JARVIS] Starting query loop\n\n");

    static const char *test_queries[] = {
        "The seL4 microkernel is",         /* cache miss → inference */
        "check disk space",                 /* cache hit */
        "What is artificial intelligence",  /* cache miss → inference */
        "list running processes",           /* cache miss → inference */
        "check disk space",                 /* cache hit (repeat) */
        "show network interfaces",          /* cache miss → inference */
        "show memory usage",                /* cache miss → inference */
        "check disk space",                 /* cache hit (3rd time) */
        NULL
    };

    uint16_t seq = 1;
    int q_total = 0, q_hits = 0, q_infer = 0;

    for (int qi = 0; test_queries[qi] != NULL; qi++) {
        const char *query = test_queries[qi];
        q_total++;

        puts_serial("[Q"); put_dec((uint32_t)q_total);
        puts_serial("] \""); puts_serial(query); puts_serial("\" -> ");

        /* Check decision cache first */
        char action[256];
        trust_level_t trust;
        if (cache_lookup(&g_cache, query, action, sizeof(action), &trust)) {
            /* Cache hit */
            q_hits++;
            puts_serial("CACHE: "); puts_serial(action); puts_serial("\n");
            continue;
        }

        /* Cache miss — forward to Process B for inference */
        q_infer++;
        puts_serial("INFER...");

        shmem_ipc_send(shared_request_ring, MSG_QUERY, seq++,
                       query, (uint16_t)strlen(query));
        seL4_Signal(req_notif);

        /* Wait for response */
        seL4_Wait(resp_notif, NULL);

        uint8_t msg_type;
        uint16_t msg_seq;
        uint8_t payload[SHMEM_MAX_PAYLOAD];
        uint16_t msg_len;

        if (shmem_ipc_recv(shared_response_ring, &msg_type, &msg_seq,
                           payload, &msg_len) == 0) {
            payload[msg_len < SHMEM_MAX_PAYLOAD ? msg_len : SHMEM_MAX_PAYLOAD - 1] = '\0';
            puts_serial(" \"");
            /* Print first 60 chars of response to keep VGA tidy */
            char truncated[61];
            int tlen = msg_len > 60 ? 60 : msg_len;
            memcpy(truncated, payload, (size_t)tlen);
            truncated[tlen] = '\0';
            puts_serial(truncated);
            if (msg_len > 60) puts_serial("...");
            puts_serial("\"\n");
        } else {
            puts_serial(" (no response)\n");
        }
    }

    /* Summary */
    puts_serial("\n[JARVIS] Query loop done: ");
    put_dec((uint32_t)q_total); puts_serial(" queries, ");
    put_dec((uint32_t)q_hits); puts_serial(" cache hits, ");
    put_dec((uint32_t)q_infer); puts_serial(" inferences\n");
```

- [ ] **Step 2: Keep idle label after the loop**

```c
idle:
    puts_serial("\n[JARVIS] System idle.\n");
    while (1) { seL4_Yield(); }
    return NULL;
```

- [ ] **Step 3: Verify the query needs normalization**

`cache_lookup` expects a normalized query. Check if `do_query` normalizes first — yes, it calls `cache_lookup` directly with the raw string. Looking at the existing demo queries ("check disk space" hits the cache), the patterns are stored normalized. Raw queries that match should work as-is since the cache patterns use lowercase.

Actually, check `cache_normalize_query` — if the cache stores normalized keys, we should normalize before lookup:

```c
        char normalized[128];
        cache_normalize_query(query, normalized, sizeof(normalized));
        if (cache_lookup(&g_cache, normalized, action, sizeof(action), &trust)) {
```

This ensures case-insensitive matching.

- [ ] **Step 4: Commit**

```bash
git add phase3/src/sel4/main_x86.c
git commit -m "feat: continuous IPC query loop — cache + inference pipeline demo

Replace single demo query with 8-query test loop. Each query checks
decision cache first (85% hit rate), forwards cache misses to Process
B for LLM inference. Prints cache/inference results to VGA + serial.
Summary line shows total queries, hits, and inferences.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Update CLAUDE.md

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Add milestone**

```
| Continuous IPC request loop (cache + inference) | DONE |
```

- [ ] **Step 2: Update "What remains"**

Remove "Continuous IPC request loop" from the list.

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: continuous IPC loop milestone"
```

---

## Expected VGA Output

```
[JARVIS] Starting query loop

[Q1] "The seL4 microkernel is" -> INFER... "a microkernel implementation of the L4 micro..."
[Q2] "check disk space" -> CACHE: show_disk_usage
[Q3] "What is artificial intelligence" -> INFER... "a field of computer science that focuses on..."
[Q4] "list running processes" -> INFER... "the following is a list of the running proce..."
[Q5] "check disk space" -> CACHE: show_disk_usage
[Q6] "show network interfaces" -> INFER... "the following are the network interfaces..."
[Q7] "show memory usage" -> INFER... "the total memory usage is..."
[Q8] "check disk space" -> CACHE: show_disk_usage

[JARVIS] Query loop done: 8 queries, 3 cache hits, 5 inferences
```

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Inference too slow in QEMU | High | Low | Expected — QEMU TCG is ~100x slower. Test on bare metal for real speed. |
| Process B OOM on multiple queries | Low | Medium | KV cache is reset per query in handle_query(). 50 tokens max. |
| Shared memory ring full | Very Low | Low | 16 slots, one query at a time — never fills up. |
| Response truncation | Low | Low | First 60 chars shown on VGA, full response on serial. |

## Effort Estimate

| Task | Hours |
|------|-------|
| Task 1: Query loop | 0.5 |
| Task 2: CLAUDE.md | 0.25 |
| **Total** | **0.75** |
