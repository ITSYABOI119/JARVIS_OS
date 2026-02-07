# JARVIS OS - Quick Start Testing Guide (WSL)

**Last Updated:** November 25, 2025
**For:** Phase 0-1 comprehensive testing in WSL

---

## TL;DR - Run All Tests Now

```bash
# 1. Open WSL terminal
wsl

# 2. Navigate to project directory
cd /mnt/c/Users/jluca/Documents/JARVIS_OS

# 3. Fix line endings (Windows -> Unix) - REQUIRED for scripts to run
sed -i 's/\r$//' setup_wsl_dependencies.sh run_all_tests_wsl.sh

# 4. Make scripts executable
chmod +x setup_wsl_dependencies.sh run_all_tests_wsl.sh

# 5. Install dependencies (first time only)
./setup_wsl_dependencies.sh

# 6. Run all tests (25-35 min)
./run_all_tests_wsl.sh

# 7. Save results to file
./run_all_tests_wsl.sh | tee test_results_$(date +%Y%m%d_%H%M%S).log
```

---

## What Gets Tested?

### Phase 0: Validation Experiments (10 tests)
- ✅ AI inference benchmarks (Phi-3 GPU performance)
- ✅ Multi-agent orchestration and conflict resolution
- ✅ SHIELD safety framework and adversarial tests
- ✅ Power management (optional - requires ACPI)

### Phase 1: C Components (2 tests)
- ✅ Decision cache (hash table, 200 patterns, 85.7% hit rate)
- ✅ IPC ring buffer (lock-free SPSC, 54μs latency)

### Phase 1: Python Components (15 tests)
- ✅ **Weeks 5-9:** AI agent, query pipeline, shell, IPC integration
- ✅ **Weeks 11-12:** Multi-agent architecture, health monitoring, fault tolerance
- ✅ **Weeks 13-15:** Dynamic model scaling, inference benchmarks
- ✅ **Week 16:** SHIELD safety framework (100 test scenarios)

**Total:** 27 tests (24 required + 3 optional)

---

## Prerequisites

### ✅ Already Verified

| Component | Status | Notes |
|-----------|--------|-------|
| WSL Ubuntu | ✅ Running | Version 2, 24.04 LTS |
| GPU | ✅ RTX 2070 | 8GB VRAM, CUDA 13.0 |
| Python | ✅ 3.12.3 | Installed |
| GCC | ✅ Available | `/usr/bin/gcc` |
| Models | ✅ Present | TinyLlama 638MB, Phi-3 2.3GB |

### ⚠️ Need Verification

| Package | Required? | Purpose | Install Command |
|---------|-----------|---------|-----------------|
| llama-cpp-python | **CRITICAL** | AI inference | See setup script |
| psutil | **Required** | Resource monitoring | `pip3 install psutil` |
| numpy | **Required** | Array operations | `pip3 install numpy` |
| pynvml | Optional | GPU monitoring | `pip3 install pynvml` |
| huggingface_hub | Optional | Model download | `pip3 install huggingface_hub` |

**Run `./setup_wsl_dependencies.sh` to install all missing packages.**

---

## File Locations

### Test Scripts
- **Master test script:** `./run_all_tests_wsl.sh` (runs all 27 tests)
- **Setup script:** `./setup_wsl_dependencies.sh` (installs dependencies)
- **Test plan:** `./phase1/COMPREHENSIVE_TEST_PLAN.md` (detailed documentation)

### Source Code
```
phase0/experiments/          # Phase 0 validation experiments (10 files)
phase1/src/cache/            # Decision cache C code + test
phase1/src/ipc/              # IPC ring buffer C code + test
phase1/src/ai/               # Python AI components (15 test files)
phase1/src/shell/            # Python shell + test
```

### Models
```
models/
├── tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf    # 638 MB (IDLE state)
└── Phi-3-mini-4k-instruct-q4.gguf          # 2.3 GB (ACTIVE/CRITICAL)
```

---

## Expected Results

### Performance Targets

| Metric | Target | Typical (RTX 2070) | Pass? |
|--------|--------|---------------------|-------|
| IPC latency | <100μs | 54μs | ✅ |
| TinyLlama inference | <100ms | 50-80ms | ✅ |
| Phi-3 inference | <600ms | 400-600ms | ✅ |
| Decision cache hit rate | >80% | 85.7% | ✅ |
| SHIELD harmful block | >90% | 100% | ✅ |
| SHIELD false positive | <5% | 0% | ✅ |
| Multi-agent routing | 100% | 100% | ✅ |

### Pass Criteria

**Minimum for "GO to Week 17":**
- ✅ 90% of required tests pass (22/24)
- ✅ All gate criteria met (see table above)
- ✅ No critical regressions from Phase 0
- ✅ Models load and run inference successfully

**Ideal outcome:**
- ✅ 100% of required tests pass (24/24)
- ✅ Optional tests pass (3/3)
- ✅ Performance within expected ranges

---

## Test Execution Flow

### Phase 1: Prerequisites (automated in script)
```bash
# Checks:
1. GPU availability (nvidia-smi)
2. Models present (*.gguf files)
3. Python packages (llama-cpp, psutil, numpy)
4. GCC compiler (for C tests)
```

### Phase 2: C Component Tests (~1-2 min)
```bash
# Tests decision cache and IPC ring buffer
cd phase1/src/cache
gcc -O2 decision_cache.c cache_patterns.c test_cache.c -o test_cache -lm
./test_cache

cd ../ipc
gcc -O2 ring_buffer.c test_ipc.c -o test_ipc -lm
./test_ipc
```

**Expected output:**
```
Decision Cache Test Suite
=========================
[PASS] Hash function generates consistent hashes
[PASS] Query normalization works correctly
[PASS] Cache lookup finds correct entries
[PASS] Cache hit rate: 85.7% (target: >80%)
...
ALL TESTS PASSED
```

### Phase 3: Python Tests - Basic (~5-7 min)
```bash
# AI agent, query pipeline, shell, IPC
cd phase1/src
python3 ai/test_agent.py              # Loads Phi-3, runs inference
python3 ai/test_query_pipeline.py     # Tests query normalization
python3 shell/test_shell.py           # Tests shell commands (30 tests)
python3 ai/test_ipc_integration.py    # Tests IPC client (6 tests)
python3 ai/test_ipc_cache_lookup.py   # Tests IPC + cache
```

**Expected output:**
```
AI Agent Test Suite
===================
Loading Phi-3 Mini (3.8B Q4)...
[OK] Model loaded successfully
[OK] Inference test passed (558ms)
...
ALL 6 TESTS PASSED
```

### Phase 4: Python Tests - Multi-Agent (~5-7 min)
```bash
# Multi-agent architecture, health monitoring, fault tolerance
python3 ai/test_multi_agent.py
python3 ai/test_conflict_resolution.py
python3 ai/test_routing_accuracy.py
python3 ai/test_health_monitoring.py
python3 ai/test_lifecycle.py
python3 ai/test_fault_tolerance_integration.py
```

**Expected output:**
```
Multi-Agent Test Suite
======================
Starting 4 specialist agents...
[OK] DeviceAgent started
[OK] NetworkAgent started
[OK] FileSystemAgent started
[OK] UserAgent started
[PASS] Routing accuracy: 100% (50/50)
...
ALL 15 TESTS PASSED
```

### Phase 5: Python Tests - Scaling & SHIELD (~7-10 min)
```bash
# Dynamic model scaling and SHIELD safety framework
python3 ai/test_dynamic_scaling.py         # 25 tests
python3 ai/test_inference_benchmark.py     # Performance comparison
python3 ai/test_shield_accuracy.py         # 100 test scenarios
python3 ai/test_shield_integration.py      # Integration test
```

**Expected output:**
```
Dynamic Scaling Test Suite
===========================
[PASS] IDLE → ACTIVE transition (1.3s)
[PASS] ACTIVE → CRITICAL transition (2.8s)
[PASS] Memory usage IDLE: 2.3GB (target: <4GB)
[PASS] Memory usage CRITICAL: 4.6GB (target: <10GB)
...
ALL 25 TESTS PASSED

SHIELD Accuracy Test Suite
===========================
Testing 100 scenarios...
[PASS] Safe operations: 0% false positive (0/50)
[PASS] Harmful operations: 100% blocked (20/20)
Overall accuracy: 87%
ALL TESTS PASSED
```

### Phase 6: Phase 0 Validation (~10-15 min)
```bash
# Validates no regressions from Phase 0
cd phase0/experiments
python3 ai_inference_benchmark.py
python3 benchmark_phi3.py
python3 multi_agent_orchestration.py
python3 conflict_resolution_tests.py
python3 shield_safety_framework.py
python3 adversarial_safety_tests.py
python3 adversarial_safety_tests_simple.py
```

**Expected output:**
```
AI Inference Benchmark
======================
Model: Phi-3 Mini 3.8B Q4
GPU: NVIDIA GeForce RTX 2070 (8GB)
Inference time: 558ms (target: <600ms)
[PASS] Performance target met
...
```

---

## Troubleshooting

### Issue: Script Cannot Execute (CRITICAL - Fix This First!)
```
-bash: ./run_all_tests_wsl.sh: cannot execute: required file not found
-bash: ./setup_wsl_dependencies.sh: cannot execute: required file not found
```

**Cause:** Scripts have Windows line endings (CRLF) instead of Unix line endings (LF)

**Solution:**
```bash
# Fix line endings for all shell scripts
cd /mnt/c/Users/jluca/Documents/JARVIS_OS
sed -i 's/\r$//' setup_wsl_dependencies.sh run_all_tests_wsl.sh

# Or use the helper script:
sed -i 's/\r$//' fix_line_endings.sh
chmod +x fix_line_endings.sh
./fix_line_endings.sh  # Fixes all .sh files in current directory

# Then make executable
chmod +x setup_wsl_dependencies.sh run_all_tests_wsl.sh

# Now they should run
./setup_wsl_dependencies.sh
```

**Prevention:** Always run `sed -i 's/\r$//' script.sh` after creating shell scripts on Windows.

### Issue: GPU Not Detected
```
[WARNING] No GPU detected - tests will be slower
```

**Solution:**
```bash
# Check GPU in WSL
nvidia-smi

# If not visible, ensure WSL 2 with GPU support:
# https://docs.nvidia.com/cuda/wsl-user-guide/index.html
```

### Issue: Model Not Found
```
Error: Failed to load model: /mnt/c/Users/.../models/Phi-3-mini-4k-instruct-q4.gguf
```

**Solution:**
```bash
# Verify models exist
ls -lh /mnt/c/Users/jluca/Documents/JARVIS_OS/models/*.gguf

# If missing, download:
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase0/experiments
python3 download_phi3.py
```

### Issue: Package Missing
```
ModuleNotFoundError: No module named 'llama_cpp'
```

**Solution:**
```bash
# Run setup script
cd /mnt/c/Users/jluca/Documents/JARVIS_OS
./setup_wsl_dependencies.sh

# Or install manually:
pip3 install llama-cpp-python psutil numpy
```

### Issue: Tests Pass But Slow
```
Inference time: 2500ms (expected <600ms)
```

**Possible causes:**
1. GPU not being used (check nvidia-smi for Python process)
2. CPU mode fallback (verify CUDA availability)
3. Model not quantized (should be Q4, not FP16/FP32)

**Solution:**
```bash
# Verify GPU usage during test
watch -n 1 nvidia-smi  # In separate terminal
# Then run test - should see Python process using GPU

# Check model quantization
ls -lh models/*.gguf
# Should see Q4 in filename and 638MB/2.3GB sizes
```

---

## After Testing

### If All Tests Pass ✅

**Congratulations!** Phase 0-1 validation complete. You're ready for:
- **Week 17:** seL4 QEMU integration (planned next)
- **Commit progress:** All 16 weeks tested and validated
- **Documentation:** Update PHASE_1_PROGRESS_TRACKER.md

### If Some Tests Fail ❌

**Don't panic.** Review the error logs:

1. **Check test output** for specific failures
2. **Review prerequisites** (GPU, models, packages)
3. **Check logs** in `/tmp/jarvis_test.log`
4. **Run individual tests** to isolate issues
5. **See COMPREHENSIVE_TEST_PLAN.md** for detailed troubleshooting

**Common failure points:**
- Model loading (GPU memory, path issues)
- Package imports (missing dependencies)
- Performance timeouts (CPU fallback)

---

## Next Steps

### Option 1: Run Full Test Suite
```bash
# This is the recommended first-time approach
cd /mnt/c/Users/jluca/Documents/JARVIS_OS
./run_all_tests_wsl.sh | tee test_results_$(date +%Y%m%d_%H%M%S).log
```

**Estimated time:** 25-35 minutes
**Output:** Detailed pass/fail for all 27 tests

### Option 2: Run Individual Test Categories
```bash
# Just C tests (1-2 min)
cd phase1/src/cache && gcc -O2 *.c -o test_cache -lm && ./test_cache
cd ../ipc && gcc -O2 ring_buffer.c test_ipc.c -o test_ipc -lm && ./test_ipc

# Just Python tests (15-20 min)
cd phase1/src
for test in ai/test_*.py shell/test_*.py; do python3 $test; done

# Just Phase 0 validation (10-15 min)
cd phase0/experiments
for exp in *benchmark*.py *tests*.py; do python3 $exp; done
```

### Option 3: Interactive Testing
```bash
# Run tests one by one, review output
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src

# Week 5-7: AI Agent
python3 ai/test_agent.py
# Review output, press Enter to continue...

python3 ai/test_query_pipeline.py
# Continue through all tests...
```

---

## Summary

This quick start guide provides:
- ✅ **One-command testing** (`./run_all_tests_wsl.sh`)
- ✅ **Automated setup** (`./setup_wsl_dependencies.sh`)
- ✅ **Clear success criteria** (gate metrics, pass rates)
- ✅ **Comprehensive troubleshooting** (common issues + solutions)

**For detailed documentation, see:**
- `COMPREHENSIVE_TEST_PLAN.md` - Full test plan (all categories, expected outcomes)
- `phase1/PHASE_1_PROGRESS_TRACKER.md` - Weekly progress (Weeks 1-16)
- `phase0/PHASE_0_FINAL_REPORT.md` - Phase 0 validation results

**Ready to test?** Run:
```bash
wsl
cd /mnt/c/Users/jluca/Documents/JARVIS_OS
./setup_wsl_dependencies.sh
./run_all_tests_wsl.sh
```

---

**Document Status:** Complete - Ready for execution
**Last Updated:** November 25, 2025
**Estimated Runtime:** 25-35 minutes (full suite with RTX 2070)
