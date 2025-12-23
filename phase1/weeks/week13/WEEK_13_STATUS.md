# JARVIS AI-OS: Week 13 Status - Dynamic Model Scaling (Design)

**Week:** 13 of 26 (Month 9-10: Dynamic Scaling + SHIELD)
**Status:** IN PROGRESS 🏗️
**Focus:** Design dynamic model scaling system with 4 states (IDLE/ACTIVE/CRITICAL/EMERGENCY)
**Started:** November 20, 2025

---

## Week 13 Objectives

**Goal:** Design a dynamic model scaling system that adapts AI model size based on system state, optimizing memory usage and performance.

**Key Innovation:** Instead of running a fixed 7B model at all times (consuming 8GB RAM constantly), dynamically load different model sizes based on workload:
- **IDLE state:** 1B model (2GB RAM) - 75% memory savings
- **ACTIVE state:** 7B model (8GB RAM) - General operations
- **CRITICAL state:** 7B + validator (10GB RAM) - Safety-critical ops
- **EMERGENCY state:** Rule-based fallback (<100MB RAM) - AI failure recovery

**Benefits:**
- 60% average memory savings (vs fixed 7B)
- Adaptive performance (scale up when needed)
- Fault tolerance (rule-based fallback if AI fails)
- Battery optimization (1B model on low battery)

---

## Original Plan (from PHASE_1_IMPLEMENTATION_PLAN.md)

**Tasks:**
1. Define 4 system states
   - IDLE: TinyLlama 1.1B (~600MB, monitoring only)
   - ACTIVE: Phi-3 Mini 3.8B (~2.5GB, general operations)
   - CRITICAL: Phi-3 Mini + validator (~4-5GB, safety-critical)
   - EMERGENCY: Rule-based fallback (<100MB, AI failure)

2. Design state transition triggers
   - IDLE → ACTIVE: User query received
   - ACTIVE → CRITICAL: High-risk operation detected
   - CRITICAL → ACTIVE: Risk resolved
   - Any → EMERGENCY: AI failure/timeout

3. Implement state machine
   - Current state tracking
   - Transition logic
   - State change logging

4. Design model swap procedure
   - Unload current model
   - Load new model
   - Minimize downtime (<5s target)

**Deliverables:**
- [ ] State machine designed
- [ ] Transition triggers defined
- [ ] Model swap procedure designed
- [ ] State tracking implemented

**Estimated Effort:** 8-12 hours

---

## System States Design

### State 1: IDLE (Minimal Resource Mode)

**Purpose:** Monitoring and waiting for user activity with minimal resource usage

**Model:** TinyLlama 1.1B Q4 quantized
- **Model file:** `TinyLlama-1.1B-Chat-v1.0.Q4_K_M.gguf`
- **Memory:** ~2GB (model + context)
- **Inference latency:** ~50-100ms (fast responses)
- **Capabilities:**
  - System monitoring queries only
  - Simple status commands
  - Health checks
  - "show cpu", "show memory", etc.

**When to use:**
- No user activity for >30 seconds
- System idle (no queries pending)
- Battery level <15% (aggressive power saving)
- Overnight/low-usage periods

**Limitations:**
- Cannot handle complex reasoning
- Limited to pre-defined monitoring commands
- Falls back to decision cache for most operations

**Resource Budget:**
- RAM: 2GB model + 4GB system = 6GB total
- CPU: 1 core (minimal background activity)
- Power: Low (1B model efficient)

---

### State 2: ACTIVE (Normal Operations)

**Purpose:** General-purpose AI operations for typical user queries

**Model:** Phi-3 Mini 3.8B Q4 quantized
- **Model file:** `Phi-3-mini-4k-instruct-q4.gguf`
- **Memory:** ~8GB (model + context)
- **Inference latency:** ~558ms (validated in Phase 0)
- **Capabilities:**
  - All standard commands
  - Complex reasoning
  - Multi-step operations
  - Code generation (simple)
  - Natural language understanding

**When to use:**
- User query received (any non-monitoring query)
- Multiple queries in queue
- General operations (file management, system control, etc.)
- Default operational state during active use

**Resource Budget:**
- RAM: 8GB model + 4GB system = 12GB total
- CPU: 3 cores (AI inference + system)
- Power: Medium

---

### State 3: CRITICAL (Safety-Critical Operations)

**Purpose:** High-risk operations requiring additional validation

**Model:** Phi-3 Mini 3.8B + Validator model
- **Primary model:** Phi-3 Mini 3.8B Q4
- **Validator model:** Phi-3 Mini 3.8B Q4 (second instance)
- **Memory:** ~10GB (2 models + shared context)
- **Inference latency:** ~1000ms (2× model calls)
- **Capabilities:**
  - All ACTIVE state capabilities
  - **Plus:** Dual-model validation
  - Shadow execution testing
  - Impact prediction
  - Rollback preparation

**When to use:**
- SHIELD detects high-risk operation (trust level 2-3)
- File deletion operations
- System configuration changes
- Security-sensitive commands
- Operations affecting multiple agents

**Validation Process:**
1. Primary model generates response
2. Validator model reviews response
3. If disagreement: Escalate to user
4. If agreement: Execute with logging
5. Snapshot taken for rollback

**Resource Budget:**
- RAM: 10GB models + 4GB system = 14GB total
- CPU: 6 cores (dual inference + system)
- Power: High (dual model inference)

---

### State 4: EMERGENCY (AI Failure Recovery)

**Purpose:** Fallback mode when AI models fail or timeout

**Model:** None (rule-based system only)
- **Implementation:** Python rule engine
- **Memory:** <100MB (lightweight)
- **Latency:** <1ms (immediate)
- **Capabilities:**
  - Basic system status commands
  - Health monitoring
  - Agent restart
  - Error reporting
  - Safe shutdown

**When to use:**
- AI model crashes or fails to load
- Inference timeout (>5 seconds)
- Out of memory condition
- Corruption detected in model files
- Emergency recovery needed

**Fallback Commands:**
```python
EMERGENCY_COMMANDS = {
    "show cpu": lambda: psutil.cpu_percent(),
    "show memory": lambda: psutil.virtual_memory(),
    "show disk": lambda: psutil.disk_usage('/'),
    "restart agent": lambda agent: lifecycle.restart_agent(agent),
    "status": lambda: get_system_status(),
    "help": lambda: "Emergency mode active. Limited commands available.",
    "shutdown": lambda: safe_shutdown()
}
```

**Resource Budget:**
- RAM: 4GB system only (no model)
- CPU: 1 core (minimal)
- Power: Very low

---

## State Transition Triggers

### IDLE → ACTIVE

**Trigger Conditions:**
- User query received (any query)
- Query not in decision cache
- System detects user activity

**Transition Process:**
1. Detect user query in IPC queue
2. Check decision cache (if hit, stay in IDLE)
3. If cache miss: Begin transition to ACTIVE
4. Unload TinyLlama 1.1B
5. Load Phi-3 Mini 3.8B (~1.5s)
6. Process query
7. Stay in ACTIVE for at least 30s

**Optimization:** Keep recently-used Phi-3 Mini in memory (cached) for fast restart

**Expected Latency:** 1.5-2.5s total (model load + first inference)

---

### ACTIVE → IDLE

**Trigger Conditions:**
- No user queries for 30 seconds (timeout)
- System detects idle state
- Battery level <15% (immediate transition)

**Transition Process:**
1. Wait for pending queries to complete
2. Check: No new queries in last 30s?
3. If yes: Begin transition to IDLE
4. Unload Phi-3 Mini 3.8B
5. Load TinyLlama 1.1B (~0.5s)
6. Enter monitoring mode

**Special Case - Low Battery:**
- If battery <15%: Immediate transition (don't wait 30s)
- Prioritize power saving over performance

---

### ACTIVE → CRITICAL

**Trigger Conditions:**
- SHIELD detects high-risk operation (trust level 2-3)
- Query involves:
  - File deletion
  - System configuration
  - Security-sensitive operations
  - Multi-agent coordination conflicts

**Transition Process:**
1. Detect high-risk operation via SHIELD
2. Pause query processing
3. Load validator model (Phi-3 Mini second instance) (~1.5s)
4. Enter CRITICAL state
5. Process query with dual validation
6. Take snapshot for rollback

**Expected Latency:** +2s for model loading + dual inference

---

### CRITICAL → ACTIVE

**Trigger Conditions:**
- Risk resolved (operation completed)
- No more high-risk operations pending
- Timeout (if staying in CRITICAL >5 minutes, step down to ACTIVE)

**Transition Process:**
1. Complete pending critical operations
2. Unload validator model
3. Return to ACTIVE state
4. Resume normal operations

---

### Any State → EMERGENCY

**Trigger Conditions:**
- AI model fails to load
- Inference timeout (>5 seconds)
- Out of memory error
- Model corruption detected
- Unhandled exception in AI code

**Transition Process:**
1. Detect failure condition
2. **Immediate** transition (no waiting)
3. Unload all AI models (free memory)
4. Activate rule-based fallback
5. Log error for analysis
6. Notify user of emergency mode

**Recovery from EMERGENCY:**
- Automatic retry after 10 seconds
- If retry fails: Stay in EMERGENCY until user manually restarts
- Log detailed error information

---

## State Machine Design

### State Diagram

```
                   ┌─────────────┐
                   │    START    │
                   └──────┬──────┘
                          │
                          ▼
        ┌────────────────────────────────────────┐
        │                                        │
        │               IDLE                     │
        │  Model: TinyLlama 1.1B (2GB)          │
        │  Purpose: Monitoring, minimal usage    │
        │                                        │
        └──────┬─────────────────────┬───────────┘
               │                     │
               │ User query          │ Battery <15%
               │ (cache miss)        │ or 30s timeout
               │                     │
               ▼                     ▼
        ┌────────────────────────────────────────┐
        │                                        │
        │              ACTIVE                    │
        │  Model: Phi-3 Mini 3.8B (8GB)         │
        │  Purpose: General operations           │
        │                                        │
        └──────┬─────────────────────┬───────────┘
               │                     │
               │ High-risk op        │ Idle 30s
               │ (SHIELD)            │
               │                     │
               ▼                     ▼
        ┌────────────────────────────────────────┐
        │                                        │
        │             CRITICAL                   │
        │  Model: Phi-3 + Validator (10GB)      │
        │  Purpose: Safety-critical ops          │
        │                                        │
        └──────┬─────────────────────────────────┘
               │
               │ Risk resolved
               │
               ▼
           (return to ACTIVE)

        ANY STATE ──[AI Failure]──> EMERGENCY
                                        │
                                        │ Retry after 10s
                                        │
                                    (return to IDLE)
```

### State Transition Table

| From State | To State | Trigger | Condition | Latency |
|------------|----------|---------|-----------|---------|
| IDLE | ACTIVE | User query | Cache miss | 1.5-2.5s |
| ACTIVE | IDLE | Timeout | No queries for 30s | 0.5s |
| ACTIVE | IDLE | Low battery | Battery <15% | 0.5s |
| ACTIVE | CRITICAL | High-risk op | SHIELD trust level 2-3 | 2s |
| CRITICAL | ACTIVE | Risk resolved | Operation complete | 0.5s |
| CRITICAL | ACTIVE | Timeout | No critical ops for 5min | 0.5s |
| Any | EMERGENCY | AI failure | Timeout/crash/OOM | <0.1s |
| EMERGENCY | IDLE | Retry success | After 10s delay | 1.5s |

---

## Model Swap Procedure

### High-Level Process

```python
def transition_state(current_state, new_state):
    """
    Transition between states with model swapping.

    Target: <5 seconds total transition time
    """

    # 1. Validate transition
    if not is_valid_transition(current_state, new_state):
        log_error(f"Invalid transition: {current_state} -> {new_state}")
        return False

    # 2. Pause query processing
    query_queue.pause()

    # 3. Wait for current inference to complete (if any)
    wait_for_inference_complete(timeout=5.0)

    # 4. Unload current model
    if current_model is not None:
        unload_model(current_model)  # ~0.5s
        free_memory()

    # 5. Load new model
    new_model = load_model(new_state)  # 0.5-1.5s depending on model

    # 6. Update state
    current_state = new_state
    log_state_transition(current_state, new_state)

    # 7. Resume query processing
    query_queue.resume()

    return True
```

### Optimization Strategies

**1. Model Caching**
- Keep recently-used models in memory (if RAM available)
- Avoid re-loading Phi-3 Mini if transitioning IDLE → ACTIVE → IDLE quickly
- Cache lifetime: 5 minutes
- Sacrifice: +8GB RAM to avoid 1.5s reload time

**2. Lazy Loading**
- Don't load validator model until actually needed
- Load in background while primary model processes query
- Parallel loading when possible

**3. Memory Pre-allocation**
- Pre-allocate memory for model context
- Avoid allocation delays during inference
- Use mmap for model files (faster load)

**4. State Prediction**
- Predict likely next state based on patterns
- Pre-load model in background before transition needed
- Example: If user active for 25s, predict ACTIVE → IDLE soon

---

## Memory Budget Analysis

### System Memory Distribution (16GB total)

**IDLE State (6GB used):**
```
TinyLlama 1.1B model:      2GB
Model context (4K):        <1GB
Operating system:          3GB
Free/cache:                10GB  ← Available for other tasks
```

**ACTIVE State (12GB used):**
```
Phi-3 Mini 3.8B model:     8GB
Model context (4K):        <1GB
Operating system:          3GB
Free/cache:                4GB   ← Sufficient for normal ops
```

**CRITICAL State (14GB used):**
```
Phi-3 Mini primary:        8GB
Phi-3 Mini validator:      2GB  ← Shared model weights
Dual context:              1GB
Operating system:          3GB
Free/cache:                2GB   ← Minimal but sufficient
```

**EMERGENCY State (4GB used):**
```
No AI model:               0GB
Python rule engine:        <100MB
Operating system:          3GB
Free/cache:                12GB  ← Maximum available for recovery
```

### Average Memory Usage

Assuming typical usage pattern:
- 70% of time in IDLE (monitoring)
- 25% of time in ACTIVE (user queries)
- 4% of time in CRITICAL (high-risk ops)
- 1% of time in EMERGENCY (failures)

**Average RAM:**
```
(0.70 × 6GB) + (0.25 × 12GB) + (0.04 × 14GB) + (0.01 × 4GB)
= 4.2 + 3.0 + 0.56 + 0.04
= 7.8GB average

vs fixed 7B model: 12GB constant
Savings: 35% average memory reduction
```

---

## Performance Targets

| Metric | Target | Rationale |
|--------|--------|-----------|
| **IDLE → ACTIVE transition** | <2.5s | Model load + first inference |
| **ACTIVE → IDLE transition** | <1s | TinyLlama is smaller |
| **ACTIVE → CRITICAL transition** | <2s | Validator model load |
| **CRITICAL → ACTIVE transition** | <0.5s | Just unload validator |
| **Any → EMERGENCY transition** | <0.1s | Immediate failover |
| **EMERGENCY → IDLE recovery** | <2s | Reload TinyLlama |
| **State prediction accuracy** | >80% | Reduce unnecessary transitions |
| **Average memory usage** | <10GB | vs 12GB fixed (17% improvement) |

---

## Implementation Tasks (Week 13)

### Task 1: Design State Machine Architecture ⏳
- [x] Define 4 states (IDLE/ACTIVE/CRITICAL/EMERGENCY)
- [x] Define state transition triggers
- [x] Create state transition table
- [x] Design state machine diagram
- **Estimated:** 2 hours

### Task 2: Design Model Swap Procedure ⏳
- [x] Define model loading/unloading process
- [x] Identify optimization strategies
- [x] Memory budget analysis
- [x] Performance targets
- **Estimated:** 2 hours

### Task 3: Implement SystemStateManager Class ⏳
- [ ] State tracking (current state, history)
- [ ] Transition logic (validate, execute, log)
- [ ] Model loader interface
- [ ] State change callbacks
- **Estimated:** 3 hours

### Task 4: Implement Model Loader ⏳
- [ ] Load/unload Phi-3 Mini
- [ ] Load/unload TinyLlama
- [ ] Memory management
- [ ] Error handling
- **Estimated:** 2 hours

### Task 5: Create State Machine Tests ⏳
- [ ] Test each state transition
- [ ] Test invalid transitions
- [ ] Test model loading
- [ ] Performance benchmarks
- **Estimated:** 2 hours

**Total Estimated Effort:** 11 hours

---

## Success Criteria

**Design Phase (Week 13):**
- [x] 4 states fully defined ✅
- [x] State transitions specified ✅
- [x] Model swap procedure designed ✅
- [ ] State machine implemented
- [ ] Tests passing (10+ tests)
- [ ] Documentation complete

**Performance Validation:**
- [ ] All transitions meet latency targets
- [ ] Average memory usage <10GB
- [ ] No crashes during state transitions
- [ ] Smooth user experience (no noticeable lag)

---

## Risks & Mitigations

### Risk 1: Model Load Time Too Slow
**Impact:** >5s transition time, poor user experience
**Probability:** Medium (model loading can be slow)
**Mitigation:**
- Use mmap for faster model loading
- Cache models in memory when RAM available
- Predict next state and pre-load model

### Risk 2: Memory Fragmentation
**Impact:** Unable to load models due to fragmented memory
**Probability:** Low (modern OSes handle this well)
**Mitigation:**
- Use contiguous memory allocation
- Compact memory before model load
- Emergency state as fallback

### Risk 3: State Thrashing
**Impact:** Rapid IDLE ↔ ACTIVE transitions waste time
**Probability:** Medium (depends on usage patterns)
**Mitigation:**
- Minimum time in each state (30s in ACTIVE)
- State prediction to avoid unnecessary transitions
- Hysteresis in transition logic

### Risk 4: Validator Model Overhead
**Impact:** CRITICAL state too slow (>1s latency)
**Probability:** Low (dual inference is expected)
**Mitigation:**
- Only use CRITICAL for truly high-risk ops
- SHIELD tuned to minimize false positives
- Parallel inference where possible

---

## Next Steps

**Week 13 Remaining:**
1. Implement SystemStateManager class
2. Implement ModelLoader class
3. Create state transition tests
4. Performance benchmark
5. Documentation update

**Week 14 (Implementation):**
1. Download TinyLlama 1.1B model
2. Integrate state machine with AI agent
3. Test all state transitions
4. Measure memory usage
5. Optimize transition times

---

**Status:** IN PROGRESS 🏗️
**Date Started:** November 20, 2025
**Expected Completion:** November 21, 2025 (1-2 days)
**Next:** Implement SystemStateManager class
