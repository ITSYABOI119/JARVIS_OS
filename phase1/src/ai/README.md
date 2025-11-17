# JARVIS AI-OS: AI Agent Module

**Phase 1 Week 5**: AI Agent Bootstrap

This module contains the AI decision engine for JARVIS AI-OS.

---

## Components

### 1. `agent.py` - Main AI Agent

**Purpose:** Load and run Phi-3 Mini 3.8B model for decision making

**Features:**
- Loads Phi-3 Mini with GPU acceleration (RTX 2070)
- Processes user queries and generates responses
- Target: <600ms inference time (Phase 0 validated: 558ms)
- Statistics tracking (queries, latency, tokens)

**Usage:**
```bash
# Standalone test
python agent.py

# Import in Python
from agent import JARVISAgent

agent = JARVISAgent()
agent.load_model()
result = agent.process_query("What is the CPU usage?")
print(result['response'])
```

### 2. `ipc_client.py` - IPC Client

**Purpose:** Connect Python AI agent to seL4 kernel via IPC ring buffer

**Features:**
- Mirrors C ring buffer structure (ctypes)
- Send/receive messages to/from seL4
- Matches `phase1/src/ipc/ring_buffer.h` interface

**Usage:**
```bash
# Standalone test
python ipc_client.py

# Import in Python
from ipc_client import IPCClient, MSG_COMMAND

client = IPCClient()
client.connect()
client.send_message(MSG_COMMAND, "help")
```

**NOTE:** Week 5 uses mock IPC for testing. Full seL4 shared memory integration in Week 6.

### 3. `test_agent.py` - Test Suite

**Purpose:** Comprehensive testing of AI agent and IPC client

**Tests:**
1. Model loading (<5s)
2. Basic inference
3. Performance validation (<600ms)
4. IPC client functionality
5. Integration (Week 6)

**Usage:**
```bash
python test_agent.py
```

---

## Configuration

### Model

**Path:** `C:/Users/jluca/Documents/JARVIS_OS/models/Phi-3-mini-4k-instruct-q4.gguf`
**Size:** 2.23 GB
**Quantization:** Q4 (4-bit)

### GPU Configuration

```python
PHI3_CONFIG = {
    'n_ctx': 2048,          # Context window
    'n_gpu_layers': 35,     # Offload all layers to GPU
    'n_threads': 4,         # CPU threads
    'verbose': False        # Quiet loading
}
```

### Inference Parameters

```python
INFERENCE_CONFIG = {
    'max_tokens': 50,       # Short command responses
    'temperature': 0.7,     # Moderate creativity
    'top_p': 0.9,           # Nucleus sampling
    'stop': ["\\n"],        # Stop at newline
    'echo': False           # Don't echo prompt
}
```

---

## Performance Targets

| Metric | Target | Phase 0 Result |
|--------|--------|----------------|
| Model load time | <5s | ~2-3s |
| AI inference (GPU) | <600ms | 558ms ✅ |
| AI inference (CPU) | <1500ms | ~1500ms |
| Round-trip (query → response) | <3s | TBD Week 6 |
| Memory usage | <4GB | TBD |

---

## Dependencies

**Python packages:**
```bash
pip install llama-cpp-python
```

**Hardware:**
- NVIDIA GPU (RTX 2070 or better) recommended
- 8GB+ RAM
- 4GB+ VRAM

---

## Architecture

```
User Query (seL4)
    ↓ (IPC ring buffer)
ipc_client.py
    ↓
agent.py (Phi-3 Mini)
    ↓
AI Response
    ↓ (IPC ring buffer)
seL4 Kernel
```

---

## Week 5 Deliverables

- [x] `agent.py` created
- [x] `ipc_client.py` created
- [x] `test_agent.py` created
- [ ] Model loads successfully (<5s)
- [ ] Inference works (<600ms GPU target)
- [ ] IPC client functional
- [ ] All tests pass

---

## Next Steps (Week 6)

1. **Integrate with seL4:**
   - Connect `ipc_client.py` to actual shared memory from seL4 QEMU
   - Test real IPC communication (not mock)

2. **Query Processing Pipeline:**
   - Query normalization
   - Cache integration
   - Command generation

3. **End-to-End Testing:**
   - seL4 QEMU ↔ AI agent via IPC
   - Measure round-trip latency
   - Verify <3s uncached query target

---

## Files

```
phase1/src/ai/
├── __init__.py          # Module exports
├── README.md            # This file
├── agent.py             # Main AI agent (Phi-3 Mini)
├── ipc_client.py        # IPC client for seL4
└── test_agent.py        # Test suite
```

---

**Week 5 Status:** IN PROGRESS
**Last Updated:** November 16, 2025
