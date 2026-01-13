# Week 34 Status: Python<->seL4 UART IPC Testing

**Status:** COMPLETE (January 13, 2026)
**Dates:** January 11-13, 2026
**Goal:** Validate end-to-end UART IPC and benchmark cache hit rate + RTT.

---

## Definition of Done

- Python<->seL4 IPC works end-to-end (heartbeat, stats, query).
- Bench runs >=200 queries with >80% hit rate and p95 RTT <25 ms.
- Protocol stream is binary-only by default (UART_PROTO_LOG=0).

---

## Commands We Run (Reproducible)

```cmd
# Test mode (single heartbeat/stats/query)
cd C:\Users\jluca\Documents\JARVIS_OS\phase2\src\ai
python uart_ipc_client.py --port COM5 --raw-log C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\python_uart.log --raw-bin C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\python_uart.bin
```

```cmd
# Bench 200 (documented in plan update)
cd C:\Users\jluca\Documents\JARVIS_OS\phase2\src\ai
python uart_ipc_client.py --port COM5 --bench 200 --pace-ms 15 --bench-log C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\uart_bench_200.csv
```

```cmd
# Bench 500 (latest artifact set)
cd C:\Users\jluca\Documents\JARVIS_OS\phase2\src\ai
python uart_ipc_client.py --port COM5 --bench 500 --pace-ms 15 --bench-log C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\uart_bench_500.csv
```

---

## Results Summary

### 200-Query Bench (recorded in plan update)
- Success: 100% (timeouts: 0)
- Hit rate: 100%
- RTT median 7.06 ms, p95 8.12 ms
- Note: CSV/log artifacts are not present in `phase2/logs`

### 500-Query Bench (from `phase2/logs/uart_bench_500.csv`)
- Total: 500, Success: 500 (timeouts: 0)
- Hit rate: 100% (500 hits, 0 misses)
- Retries: 0x491, 1x9
- RTT (ms): min 5.83, avg 16.35, median 7.09, p95 8.19, p99 513.82

---

## Artifacts (Evidence)

- `phase2/logs/uart_bench_500.csv`
- `phase2/logs/python_uart_bench_500.log`
- `phase2/logs/python_uart_bench_500.bin`
- `phase2/docs/PHASE_2_IMPLEMENTATION_PLAN.md` (Jan 13, 2026 update for 200-query bench)
- `phase2/weeks/week34/WEEK_34_RESULTS.md`

---

## Reliability Notes

- Binary stream remains clean (no ASCII protocol tags observed in the raw log).
- CRC mismatches observed in binary stream parsing (7) and invalid-length headers (2);
  host length-guard + resync prevents parser stalls and cascading timeouts.

---

## Success Criteria

| Criterion | Target | Status |
|-----------|--------|--------|
| Python<->seL4 query received | Frame parsed correctly | PASS |
| seL4<->Python response received | Valid response frame | PASS |
| Cache hit rate | >80% | PASS (100%) |
| Round-trip latency | p95 <25ms | PASS (8.19ms) |
| CRC errors handled | No cascading failures | PASS |
| Heartbeat stable | No retries in test mode | PASS |

---

## Next Week Handoff (Week 35)

- Begin SD/EMMC storage driver bring-up.
- Capture serial console logs during controller init and card identification.

---

*Week 34 completed January 13, 2026*
*Previous: Week 33 (UART RX Enable)*
*Next: Week 35 (SD/EMMC Driver)*
