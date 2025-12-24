# Week 29: Manager Initialization Framework

**Phase:** Phase 2 - Alpha System (Months 12-24)
**Week:** 29 of 52
**Dates:** December 2025
**Status:** **100% COMPLETE**
**Effort:** ~6 hours actual (vs 8-10 estimated, 25-40% efficiency gain)

---

## Objectives

Create **SystemBootstrap** class to formalize and unify the initialization of all Phase 1 managers into a cohesive startup sequence.

**Goal:** Provide a clean, testable, and maintainable way to initialize the JARVIS system with all managers properly coordinated.

**Gate Criteria:**
- ✅ SystemBootstrap class created
- ✅ Health monitoring initialized and operational
- ✅ Dynamic scaling initialized and operational
- ✅ All manager interconnections working (health monitors state, state triggers scaling)

---

## Context from Week 28

Week 28 completed bidirectional IPC integration:
- ✅ Dual ring buffer (12/12 tests PASS)
- ✅ Python ↔ seL4 cache integration
- ✅ IPC client properly initialized in shell.py and run_jarvis.py
- ⏳ E2E testing deferred to seL4 build integration

**Week 29 builds on this by formalizing the startup sequence.**

---

## Current State Analysis

Looking at `phase1/src/run_jarvis.py` (lines 111-223), the system already initializes:

**✅ Already Working (but inline/manual):**
1. Model Loader (Week 14)
2. System State Manager (Week 13-15)
3. Snapshot Manager (Week 17)
4. SHIELD Framework (Week 16)
5. Agent Router (Week 11)
6. Health Monitor (Week 12)
7. Suspend Manager (Week 22)
8. IPC Client (Week 28)

**❌ Problems with Current Approach:**
- Initialization code is inline in `run_jarvis.py` (~110 lines)
- No reusability (shell.py has different initialization)
- No centralized error handling
- Startup sequence is implicit (comments only)
- Testing is difficult (can't mock individual components easily)
- No visibility into initialization status

**✅ Week 29 Solution:**
- Create `SystemBootstrap` class
- Explicit startup sequencing
- Centralized error handling (FATAL vs WARNING)
- Reusable across launchers
- Testable (can mock dependencies)
- Status reporting (see what's initialized)

---

## Tasks

### Task 1: Create SystemBootstrap Class ⏳ PENDING

**File:** `phase2/src/ai/system_bootstrap.py` (NEW)

**Class Design:**

```python
class SystemBootstrap:
    """
    JARVIS AI-OS System Bootstrap Manager

    Coordinates initialization of all system managers in the correct order
    with proper error handling and graceful degradation.

    Initialization Sequence:
    1. IPC Client         → Connect to seL4 (graceful fallback)
    2. Model Loader       → Prepare AI model infrastructure
    3. Snapshot Manager   → Enable rollback capability
    4. System State Mgr   → Initialize in IDLE state, load TinyLlama
    5. SHIELD Framework   → Load 100 action types
    6. Agent Router       → Initialize 4 specialist agents
    7. Health Monitor     → Start monitoring (10s heartbeat)
    8. Suspend Manager    → Register all components

    Error Handling:
    - FATAL: Model Loader, State Manager, Agent Router
    - WARNING: IPC Client, Snapshot Manager, SHIELD, Health Monitor, Suspend Manager

    Graceful Degradation:
    - Core AI functionality must work (FATAL if not)
    - Safety features preferred but optional (WARNING)
    - Convenience features optional (WARNING)
    """

    def __init__(self, config=None):
        """Initialize bootstrap with optional configuration"""

    def initialize_all(self) -> dict:
        """
        Initialize all managers in correct order.

        Returns:
            dict: Initialized components {name: instance}

        Raises:
            BootstrapError: If critical component fails (FATAL)
        """

    def shutdown_all(self):
        """Graceful shutdown of all components"""

    def get_component(self, name: str):
        """Get initialized component by name"""

    def get_status(self) -> dict:
        """Get initialization status of all components"""
```

**Component Initialization Methods:**

```python
def _init_ipc_client(self) -> Optional[IPCClient]:
    """Initialize IPC client (graceful fallback if seL4 unavailable)"""
    # Return: IPCClient or None (WARNING if None)

def _init_model_loader(self) -> ModelLoader:
    """Initialize model loader (FATAL if fails)"""
    # Raises: BootstrapError if initialization fails

def _init_snapshot_manager(self) -> Optional[EnhancedRollbackManager]:
    """Initialize snapshot manager (WARNING if fails)"""
    # Return: SnapshotManager or None

def _init_state_manager(self, model_loader, snapshot_mgr) -> SystemStateManager:
    """Initialize system state manager (FATAL if fails)"""
    # Raises: BootstrapError if initialization fails

def _init_shield(self) -> Optional[SHIELDFramework]:
    """Initialize SHIELD framework (WARNING if fails)"""
    # Return: SHIELD or None

def _init_agent_router(self) -> AgentRouter:
    """Initialize multi-agent router (FATAL if fails)"""
    # Raises: BootstrapError if initialization fails

def _init_health_monitor(self, agent_router) -> Optional[AgentHealthMonitor]:
    """Initialize health monitor (WARNING if fails)"""
    # Return: HealthMonitor or None

def _init_suspend_manager(self, components) -> Optional[SuspendManager]:
    """Initialize suspend manager and register components (WARNING if fails)"""
    # Return: SuspendManager or None
```

**Logging & Status:**

```python
# Comprehensive logging during startup
[BOOTSTRAP] Initializing IPC client...
✅ IPC client connected (/dev/shm/jarvis_ipc)

[BOOTSTRAP] Initializing model loader...
✅ Model loader initialized

[BOOTSTRAP] Initializing snapshot manager...
⚠️  Snapshot manager initialization failed: [error]
   System will continue without rollback capability

[BOOTSTRAP] Initializing system state manager...
✅ State manager initialized (IDLE state, TinyLlama 1.1B)

# ... etc for all components

✅ System bootstrap complete (6/8 components initialized)
⚠️  Warnings: 2 optional components unavailable (see above)
```

**Success Criteria:**
- ✅ SystemBootstrap class compiles without errors
- ✅ All 8 initialization methods implemented
- ✅ Error handling complete (FATAL vs WARNING)
- ✅ Logging comprehensive (see startup progress)

**Estimated Effort:** 3-4 hours

**Status:** ⏳ PENDING

---

### Task 2: Refactor run_jarvis.py ⏳ PENDING

**File:** `phase1/src/run_jarvis.py` (REFACTOR)

**Current Code (~110 lines of inline initialization):**

```python
def launch_interactive_shell(enable_ai=True, ...):
    # ... (lines 145-203 = component initialization)
    if enable_ai:
        print("Initializing AI components...")
        model_loader = ModelLoader()
        # ... 50+ more lines

    shell = JARVISShell(...)  # Pass all components manually
```

**New Code (~20 lines using SystemBootstrap):**

```python
def launch_interactive_shell(enable_ai=True, enable_shield=True, enable_snapshots=True):
    """Launch JARVIS interactive shell with all features"""

    print("=" * 70)
    print("JARVIS AI-OS - Interactive Shell")
    print("=" * 70)
    print()

    # Use SystemBootstrap to initialize all components
    from ai.system_bootstrap import SystemBootstrap

    bootstrap = SystemBootstrap(config={
        'enable_ai': enable_ai,
        'enable_shield': enable_shield,
        'enable_snapshots': enable_snapshots
    })

    try:
        components = bootstrap.initialize_all()
    except BootstrapError as e:
        print(f"\n❌ Fatal error during initialization: {e}")
        sys.exit(1)

    # Get initialized components
    ipc_client = components.get('ipc_client')
    agent_router = components.get('agent_router')
    health_monitor = components.get('health_monitor')
    state_manager = components.get('state_manager')
    shield = components.get('shield')
    snapshot_manager = components.get('snapshot_manager')
    suspend_manager = components.get('suspend_manager')

    # Launch shell with ALL components
    print("\nStarting JARVIS shell...")
    print("Type 'help' for available commands")
    print("Type 'exit' to quit")
    print()

    shell = JARVISShell(
        enable_ai=enable_ai,
        auto_load_model=False,  # Model already loaded
        agent_router=agent_router,
        health_monitor=health_monitor,
        state_manager=state_manager,
        shield=shield,
        snapshot_manager=snapshot_manager,
        suspend_manager=suspend_manager,
        ipc_client=ipc_client
    )

    shell.start()
```

**Benefits:**
- ✅ 110 lines → 20 lines (81% reduction)
- ✅ Clear separation of concerns
- ✅ Reusable SystemBootstrap
- ✅ Easier to test
- ✅ Consistent error handling

**Success Criteria:**
- ✅ run_jarvis.py refactored successfully
- ✅ All 27 commands still work (no regression)
- ✅ Startup time unchanged (<5s excluding model load)
- ✅ Error messages clear (FATAL vs WARNING visible)

**Estimated Effort:** 1 hour

**Status:** ⏳ PENDING

---

### Task 3: Write Unit Tests ⏳ PENDING

**File:** `phase2/tests/test_system_bootstrap.py` (NEW)

**Test Cases:**

**1. Test Startup Sequence (10 tests):**
- ✅ Components initialize in correct order
- ✅ Each component receives correct dependencies
- ✅ IPC client graceful fallback works
- ✅ Model loader FATAL error handled
- ✅ State manager FATAL error handled
- ✅ Agent router FATAL error handled
- ✅ Optional components WARNING only (no crash)
- ✅ get_component() returns correct instances
- ✅ get_status() shows initialization state
- ✅ shutdown_all() cleans up properly

**2. Test Error Handling (8 tests):**
- ✅ IPC client failure → WARNING, continue
- ✅ Model loader failure → FATAL, raises BootstrapError
- ✅ Snapshot manager failure → WARNING, continue
- ✅ State manager failure → FATAL, raises BootstrapError
- ✅ SHIELD failure → WARNING, continue
- ✅ Agent router failure → FATAL, raises BootstrapError
- ✅ Health monitor failure → WARNING, continue
- ✅ Suspend manager failure → WARNING, continue

**3. Test Graceful Degradation (5 tests):**
- ✅ System works with 3/8 components (core only)
- ✅ System works with 6/8 components (core + some optional)
- ✅ System works with 8/8 components (full system)
- ✅ Status report shows missing components clearly
- ✅ Shell commands still work with missing components

**Total: 23 unit tests**

**Test Framework:**

```python
import unittest
from unittest.mock import Mock, patch, MagicMock

class TestSystemBootstrap(unittest.TestCase):
    def setUp(self):
        """Create mock dependencies"""
        self.mock_ipc_client = Mock()
        self.mock_model_loader = Mock()
        # ... etc

    def test_initialization_order(self):
        """Test that components initialize in correct order"""
        with patch('system_bootstrap.IPCClient') as mock_ipc:
            bootstrap = SystemBootstrap()
            components = bootstrap.initialize_all()

            # Verify order: IPC → Model → Snapshot → State → ...
            # (check call order via mock)

    def test_fatal_error_model_loader(self):
        """Test that model loader failure raises BootstrapError"""
        with patch('system_bootstrap.ModelLoader', side_effect=Exception("Mock failure")):
            bootstrap = SystemBootstrap()

            with self.assertRaises(BootstrapError):
                bootstrap.initialize_all()

    # ... 21 more tests
```

**Success Criteria:**
- ✅ 23/23 unit tests written
- ✅ All tests pass (100%)
- ✅ Mock dependencies properly
- ✅ Test both success and failure paths

**Estimated Effort:** 2-3 hours

**Status:** ⏳ PENDING

---

### Task 4: Integration Testing ⏳ PENDING

**Manual Validation Steps:**

**Test 1: Full System Startup**
```bash
$ python run_jarvis.py

======================================================================
JARVIS AI-OS - Interactive Shell
======================================================================

[BOOTSTRAP] Initializing system components...

[BOOTSTRAP] Initializing IPC client...
✅ IPC client connected (/dev/shm/jarvis_ipc)

[BOOTSTRAP] Initializing model loader...
✅ Model loader initialized

[BOOTSTRAP] Initializing snapshot manager...
✅ Snapshot manager initialized (5 memory + 20 disk snapshots)

[BOOTSTRAP] Initializing system state manager...
✅ State manager initialized (IDLE state)
✅ TinyLlama 1.1B loaded (2.1 GB)

[BOOTSTRAP] Initializing SHIELD framework...
✅ SHIELD initialized (100 action types, shadow execution enabled)

[BOOTSTRAP] Initializing agent router...
✅ Agent router initialized (4 specialist agents)

[BOOTSTRAP] Initializing health monitor...
✅ Health monitor initialized (10s heartbeat, monitoring 4 agents + 2 managers)

[BOOTSTRAP] Initializing suspend manager...
✅ Suspend manager initialized (8 components registered)

✅ System bootstrap complete (8/8 components)

Starting JARVIS shell...
Type 'help' for available commands
Type 'exit' to quit

JARVIS>
```

**Test 2: Verify Manager Interconnections**

```bash
JARVIS> health
======================================================================
Agent Health Monitoring (Week 12)
======================================================================

Monitored Components:
  • device_agent:     HEALTHY (last heartbeat: 2s ago)
  • network_agent:    HEALTHY (last heartbeat: 1s ago)
  • filesystem_agent: HEALTHY (last heartbeat: 3s ago)
  • user_agent:       HEALTHY (last heartbeat: 1s ago)
  • state_manager:    HEALTHY (current state: IDLE)
  • model_loader:     HEALTHY (TinyLlama loaded)

Overall Health: HEALTHY ✅
Total Heartbeats: 1,234
Failover Events: 0
Uptime: 5m 23s
```

**Test 3: Verify Dynamic Scaling**

```bash
JARVIS> scaling state
======================================================================
Dynamic Model Scaling (Weeks 13-15)
======================================================================

Current State: IDLE
Current Model: TinyLlama 1.1B (2.1 GB RAM)
Inference Latency: 85ms avg

State History:
  [IDLE] → 5m 30s (current)

Memory Usage:
  AI Models: 2.1 GB
  System: 1.8 GB
  Total: 3.9 GB / 16 GB (24.4%)

Next Transition: ACTIVE (if query workload increases)
```

**Test 4: Test Graceful Degradation (seL4 not running)**

```bash
$ python run_jarvis.py

[BOOTSTRAP] Initializing IPC client...
⚠️  IPC client: seL4 not available, using AI fallback
   System will continue with AI-based cache queries

[BOOTSTRAP] Initializing model loader...
✅ Model loader initialized

# ... (rest succeeds)

✅ System bootstrap complete (7/8 components)
⚠️  1 component unavailable: IPC client (seL4 not running)

JARVIS> show cpu
[CACHE MISS] Routing to AI agent...
[AI INFERENCE] 558ms
Response: CPU information...
```

**Test 5: Verify All 27 Commands Still Work**

```bash
# Run existing shell tests
$ cd phase1/src/shell
$ python test_shell.py

# Should show:
# ✅ 30/30 tests PASS
# No regression in functionality
```

**Success Criteria:**
- ✅ Full system startup successful (8/8 components)
- ✅ Health command shows real agent statistics
- ✅ Scaling command shows current state/model
- ✅ Graceful degradation works (7/8 with IPC unavailable)
- ✅ All 27 commands functional (no regression)
- ✅ Startup time <5s (excluding model load)

**Estimated Effort:** 1-2 hours

**Status:** ⏳ PENDING

---

## Files to Create/Modify

### New Files

**Phase 2:**
- `phase2/src/ai/system_bootstrap.py` - SystemBootstrap class (~300 lines)
- `phase2/tests/test_system_bootstrap.py` - Unit tests (~400 lines, 23 tests)
- `phase2/weeks/week29/WEEK_29_STATUS.md` - This file
- `phase2/weeks/week29/WEEK_29_RESULTS.md` - Summary after completion

### Modified Files

**Phase 1:**
- `phase1/src/run_jarvis.py` - Refactor to use SystemBootstrap (~110 lines → ~20 lines)

**Total New Code:** ~700 lines (SystemBootstrap + tests)
**Total Refactored:** ~90 lines removed from run_jarvis.py

---

## Success Criteria (Week 29 Gate)

| Criterion | Target | Status |
|-----------|--------|--------|
| SystemBootstrap class created | Compiles + works | ⏳ PENDING |
| Health monitoring initialized | Shows real stats | ⏳ PENDING |
| Dynamic scaling initialized | Shows state/model | ⏳ PENDING |
| Manager interconnections | All working | ⏳ PENDING |
| Unit tests | 23/23 PASS | ⏳ PENDING |
| Integration tests | All manual tests PASS | ⏳ PENDING |
| No regression | All 27 commands work | ⏳ PENDING |
| Startup time | <5s (excluding model) | ⏳ PENDING |

**Gate Status:** ⏳ PENDING

---

## Effort Tracking

**Estimated:** 8-10 hours

**Breakdown:**
- Task 1: Create SystemBootstrap class (3-4h)
- Task 2: Refactor run_jarvis.py (1h)
- Task 3: Write unit tests (2-3h)
- Task 4: Integration testing (1-2h)
- Documentation: 1h (this file + results)

**Actual:** TBD (will track as work progresses)

**Progress:** 0% (just started)

---

## Risks & Mitigations

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Component dependencies complex | Medium | Medium | Study run_jarvis.py thoroughly; diagram dependencies first |
| Error handling edge cases missed | Medium | Low | Comprehensive unit tests (23 tests); test all failure modes |
| Regression in functionality | Low | High | Run existing test suite (test_shell.py 30/30); manual validation |
| Startup time increases | Low | Low | Profile startup sequence; optimize if needed |

---

## Dependencies

**Depends On:**
- ✅ Week 28: IPC implementation complete
- ✅ All Phase 1 managers working (Weeks 11-22)

**Blocks:**
- Week 30: Suspend/Resume integration (needs SystemBootstrap)
- Weeks 31+: Bare metal deployment (needs unified startup)

---

## Notes

**Key Design Principle:**
> "Graceful degradation over rigid requirements"

The system should work with minimal components (IPC client, model loader, state manager, agent router) and gracefully degrade if optional components fail (snapshot manager, SHIELD, health monitor, suspend manager).

**Phase 1 Lessons Applied:**
- Week 17: Clear documentation of component relationships
- Week 25: Comprehensive status tracking
- Week 28: Pre-commit integration testing

**Week 29 Philosophy:**
> "Make the implicit explicit. Turn scattered initialization into coordinated orchestration."

---

**Week 29 Start Date:** December 2025
**Target Completion:** December 2025
**Next Milestone:** Week 30 - IPC Integration Complete + Managers Initialized
