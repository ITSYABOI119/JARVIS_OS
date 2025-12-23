# How to Run JARVIS AI-OS

**Quick guide to running your JARVIS system with all Week 1-17 features.**

---

## Option 1: Interactive Shell (Recommended)

```bash
wsl
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src
python3 run_jarvis.py
```

This launches JARVIS with all features:
- AI Agent (Phi-3 Mini with dynamic scaling)
- SHIELD Safety Framework
- Shadow Execution
- Snapshots & Rollback

---

## Option 2: Run Comprehensive Tests

```bash
wsl
cd /mnt/c/Users/jluca/Documents/JARVIS_OS
./run_all_tests_wsl.sh
```

Runs all 27 tests (Phase 0 + Phase 1), takes 25-35 minutes.

---

## Option 3: Check Dependencies Only

```bash
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src
python3 run_jarvis.py --check-deps
```

Verifies all components are installed (models, Python packages, unshare).

---

## What You'll See

When you run the interactive shell:

1. **Startup**: Models load (TinyLlama in IDLE state, ~2-3 seconds)
2. **JARVIS prompt**: `JARVIS>` where you can type commands
3. **Built-in commands**:
   - `help` - Show all commands
   - `status` - System status
   - `shield` - Safety statistics
   - `snapshots` - Snapshot manager status
   - `exit` - Quit

4. **AI queries**: Any non-command text goes to the AI

---

## Current Features (Weeks 1-17)

✅ **Decision Cache**: 200 pre-compiled patterns, 85.7% hit rate
✅ **AI Agent**: Phi-3 Mini 3.8B (558ms GPU inference)
✅ **Multi-Agent**: 4 specialist agents (100% routing accuracy)
✅ **Dynamic Scaling**: TinyLlama (idle) ↔ Phi-3 (active/critical)
✅ **SHIELD Safety**: 100 action types, 100% harmful block rate
✅ **Shadow Execution**: Real Linux namespace isolation (2.3ms)
✅ **Snapshots**: Hybrid memory+disk with <0.5ms rollback

---

## Performance Benchmarks

| Feature | Performance |
|---------|------------|
| IPC latency | 54μs |
| Cache hit rate | 85.7% |
| AI inference (GPU) | 558ms |
| Shadow execution | 2.3ms |
| Memory rollback | <0.5ms |
| Snapshot creation | ~100ms |

All targets met or exceeded ✅

---

## Troubleshooting

**"Model not found"** → Models are in `/mnt/c/Users/jluca/Documents/JARVIS_OS/models/`
**"Slow inference"** → GPU recommended (CUDA). CPU works but slower (~1.5s)
**"unshare error"** → Shadow execution automatically handles WSL limitations
**"Shell freezes"** → First query loads model (2-3s), subsequent queries are fast

---

For more details, see:
- `QUICK_START.md` - Comprehensive guide
- `QUICK_START_TESTING.md` - Testing guide
- `phase1/PHASE_1_PROGRESS_TRACKER.md` - Implementation status

Generated: November 25, 2025
