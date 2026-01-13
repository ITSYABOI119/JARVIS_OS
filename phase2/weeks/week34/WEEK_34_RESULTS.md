# Week 34 Results: Python<->seL4 UART IPC Testing

**Status:** COMPLETE
**Date:** January 13, 2026
**Goal:** Validate end-to-end UART IPC and benchmark cache hit rate + RTT.

---

## Commands Run

```cmd
cd C:\Users\jluca\Documents\JARVIS_OS\phase2\src\ai
python uart_ipc_client.py --port COM5 --raw-log C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\python_uart.log --raw-bin C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\python_uart.bin
```

```cmd
cd C:\Users\jluca\Documents\JARVIS_OS\phase2\src\ai
python uart_ipc_client.py --port COM5 --bench 500 --pace-ms 15 --bench-log C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\uart_bench_500.csv
```

---

## Benchmark Summary (500 queries)

| Metric | Result |
|--------|--------|
| Total | 500 |
| Success | 500 (100%) |
| Timeouts | 0 |
| Hit rate | 100% (500 hits, 0 misses) |
| Retries | 0x491, 1x9 |
| RTT min | 5.83 ms |
| RTT avg | 16.35 ms |
| RTT median | 7.09 ms |
| RTT p95 | 8.19 ms |
| RTT p99 | 513.82 ms |

**Outliers:** RTT spikes align with single-retry rows (indices 55, 72, 336, 395, 452).

---

## Error Handling Evidence (Raw RX Stream)

Parsed from `phase2/logs/python_uart_bench_500.bin`:
- Frames parsed: 500
- CRC mismatches: 7
- Invalid-length headers: 2
- Truncated frames: 0

**Behavior:** Length-guard + resync prevents parser stalls or cascading failures.

---

## Artifacts

- `phase2/logs/uart_bench_500.csv`
- `phase2/logs/python_uart_bench_500.log`
- `phase2/logs/python_uart_bench_500.bin`

