# How to Run JARVIS Phase 0 Experiments

## Quick Start in VS Code

### Current Setup:
- **Planning docs**: `C:\Users\jluca\Documents\JARVIS_OS`
- **Experiments**: `\\wsl.localhost\Ubuntu\home\itsme\jarvis-phase0`
- **Model**: `\\wsl.localhost\Ubuntu\home\itsme\jarvis-phase0\models\mistral-7b-instruct-v0.2.Q8_0.gguf`

---

## Experiments 1-3: ✅ COMPLETED (Python - No dependencies)

### Run in WSL Terminal:
```bash
cd ~/jarvis-phase0

# Experiment 1: Mock Microkernel
python3 mock_kernel.py

# Experiment 2: Decision Cache
python3 decision_cache.py

# Experiment 3: Dynamic Model Scaling
python3 dynamic_scaling.py
```

**Results:**
- ✅ Experiment 1: Interrupt 0.17ms, IPC 84μs - **PASS**
- ✅ Experiment 2: 40,000x speedup, 78.6% hit rate - **PASS**
- ✅ Experiment 3: 44% memory savings - **PASS**

---

## Experiment 4: AI Inference Benchmark (Needs Setup)

### Option A: Run on Windows (Recommended for GPU)

**1. Install llama-cpp-python (PowerShell):**
```powershell
# With CUDA support for RTX 2070
pip install llama-cpp-python --extra-index-url https://abetlen.github.io/llama-cpp-python/whl/cu121
```

**2. Copy model from WSL to Windows (IMPORTANT for GPU performance):**
```powershell
# Create models folder
mkdir C:\Users\jluca\Documents\JARVIS_OS\jarvis-phase0\models

# Copy model file (7.17GB, takes ~1 minute)
copy "\\wsl.localhost\Ubuntu\home\itsme\jarvis-phase0\models\mistral-7b-instruct-v0.2.Q8_0.gguf" "C:\Users\jluca\Documents\JARVIS_OS\jarvis-phase0\models\"
```

**Note:** Running from WSL path causes dual-loading (WSL cache + GPU VRAM = 16GB total). Windows-native path eliminates this overhead and reduces load time from 74s to ~10-15s.

**3. Run:**
```powershell
cd C:\Users\jluca\Documents\JARVIS_OS\jarvis-phase0
python ai_inference_benchmark.py
```

### Option B: Run in WSL (May not detect GPU)

**1. Install dependencies:**
```bash
cd ~/jarvis-phase0
sudo apt update
sudo apt install -y python3-pip
pip3 install llama-cpp-python
```

**2. Copy script:**
```bash
cp /mnt/c/Users/jluca/Documents/JARVIS_OS/jarvis-phase0/ai_inference_benchmark.py ~/jarvis-phase0/
```

**3. Run:**
```bash
python3 ai_inference_benchmark.py
```

---

## Expected Results (Experiment 4)

**On your RTX 2070 with Mistral 7B INT8:**
- Simple query: 150-300ms
- Complex query: 300-500ms
- **Target:** <500ms for complex queries
- **Status:** Should PASS ✅

---

## Quick Command Reference

### Check Python:
```bash
python --version   # Windows
python3 --version  # WSL
```

### Check GPU:
```bash
nvidia-smi  # Windows
```

### Navigate to experiments:
```bash
# WSL
cd ~/jarvis-phase0

# Windows
cd C:\Users\jluca\Documents\JARVIS_OS\jarvis-phase0
```

---

## VS Code Terminal Setup

**Option 1: Use WSL terminal** (for experiments 1-3)
1. Open VS Code
2. Terminal → New Terminal
3. Select "WSL (Ubuntu)"
4. `cd ~/jarvis-phase0`

**Option 2: Use PowerShell** (for experiment 4 with GPU)
1. Open VS Code
2. Terminal → New Terminal
3. Select "PowerShell"
4. `cd C:\Users\jluca\Documents\JARVIS_OS\jarvis-phase0`

---

## Troubleshooting

### "Module not found" errors:
```bash
# WSL
pip3 install <module>

# Windows
pip install <module>
```

### GPU not detected:
```powershell
# Check CUDA
nvidia-smi

# Reinstall with CUDA
pip uninstall llama-cpp-python
pip install llama-cpp-python --extra-index-url https://abetlen.github.io/llama-cpp-python/whl/cu121
```

### Model not found:
```bash
# Check model location
ls -lh ~/jarvis-phase0/models/

# Should see: mistral-7b-instruct-v0.2.Q8_0.gguf (7.2G)
```

---

## What's Next?

After completing all experiments, run:
```bash
python3 create_summary_report.py
```

This will generate a complete report of all validation results!
