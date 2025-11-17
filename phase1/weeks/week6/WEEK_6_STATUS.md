# Phase 1 Week 6 Status: Query Processing Pipeline

**Date Started:** November 16, 2025
**Date Completed:** TBD
**Status:** IN PROGRESS
**Focus:** Query normalization, cache integration, command parsing
**Time Invested:** 0/16 hours

---

## Week 6 Objectives

**Goal:** Implement end-to-end query processing pipeline from user input to kernel commands.

**Key Deliverables:**
1. Query normalization (lowercase, whitespace, keywords)
2. Decision cache integration (check cache before AI)
3. Command parser (AI response → kernel command)
4. End-to-end query flow working
5. Latency targets met (<2s cached, <3s uncached)

---

## Progress Tracker

### Task 1: Implement Query Processor
**Status:** ⏳ NOT STARTED
**Estimated Time:** 4-6 hours

**Steps:**
- [ ] Create query_processor.py module
- [ ] Implement query normalization
  - Lowercase conversion
  - Whitespace collapse
  - Trim leading/trailing spaces
  - Keyword extraction
- [ ] Implement hash generation (FNV-1a, match C cache)
- [ ] Add unit tests for normalization

**Success Criteria:**
- [ ] "  SHOW   CPU  " → "show cpu"
- [ ] Hash matches C implementation
- [ ] 10+ normalization test cases pass

---

### Task 2: Cache Integration
**Status:** ⏳ NOT STARTED
**Estimated Time:** 3-4 hours

**Steps:**
- [ ] Connect query processor to decision cache
- [ ] Implement cache lookup logic
- [ ] Implement cache miss handling (fallback to AI)
- [ ] Implement cache update on new queries
- [ ] Add cache statistics tracking

**Success Criteria:**
- [ ] Cache hit returns command in <100ms
- [ ] Cache miss falls back to AI inference
- [ ] Cache hit rate >80% with Week 3 patterns
- [ ] Statistics track hits/misses correctly

---

### Task 3: Command Parser
**Status:** ⏳ NOT STARTED
**Estimated Time:** 3-4 hours

**Steps:**
- [ ] Design command format (JSON or structured)
- [ ] Implement AI response parser
- [ ] Extract command type and parameters
- [ ] Validate command structure
- [ ] Map to IPC message types

**Success Criteria:**
- [ ] Parse "show CPU stats" → READ_CPU_STATS command
- [ ] Extract parameters correctly
- [ ] Invalid commands rejected with error
- [ ] 10+ parsing test cases pass

---

### Task 4: Update AI Agent
**Status:** ⏳ NOT STARTED
**Estimated Time:** 2-3 hours

**Steps:**
- [ ] Integrate query_processor into agent.py
- [ ] Update process_query() method
- [ ] Add cache-first logic
- [ ] Update statistics tracking
- [ ] Test modified agent

**Success Criteria:**
- [ ] Agent uses cache before AI
- [ ] Cache hits avoid AI inference
- [ ] Statistics show cache performance
- [ ] All existing tests still pass

---

### Task 5: End-to-End Testing
**Status:** ⏳ NOT STARTED
**Estimated Time:** 2-3 hours

**Steps:**
- [ ] Create test_query_pipeline.py
- [ ] Test query normalization (10+ cases)
- [ ] Test cache hit/miss behavior
- [ ] Test command parsing
- [ ] Measure end-to-end latency
- [ ] Validate with Week 3 cache patterns

**Success Criteria:**
- [ ] 10/10 normalization tests pass
- [ ] Cache hit rate >80%
- [ ] Cached queries <100ms
- [ ] Uncached queries <600ms (AI only)
- [ ] All integration tests pass

---

### Task 6: Documentation
**Status:** ⏳ NOT STARTED
**Estimated Time:** 1 hour

**Updates:**
- [ ] This status document (WEEK_6_STATUS.md)
- [ ] PHASE_1_PROGRESS_TRACKER.md
- [ ] Code comments in query_processor.py
- [ ] Update README.md in src/ai/

---

## Technical Specifications

### Query Normalization

**Input:** Raw user query (any case, any whitespace)
**Output:** Normalized query (lowercase, single spaces, trimmed)

**Algorithm:**
```python
def normalize_query(query: str) -> str:
    # 1. Convert to lowercase
    normalized = query.lower()

    # 2. Collapse multiple spaces to single space
    normalized = ' '.join(normalized.split())

    # 3. Trim leading/trailing whitespace
    normalized = normalized.strip()

    return normalized
```

**Examples:**
- "  SHOW   CPU  " → "show cpu"
- "What's\t\tthe\nmemory?" → "what's the memory?"
- "   help   " → "help"

### Cache Integration Flow

```
User Query
    ↓
Normalize Query
    ↓
Hash Normalized Query (FNV-1a)
    ↓
Lookup in Decision Cache
    ↓
  ┌─────────────────┐
  │  Cache Hit?     │
  └─────┬───────┬───┘
        │       │
    YES │       │ NO
        │       │
        ↓       ↓
   Return    AI Inference
   Cached        ↓
   Command   Parse Response
        │         ↓
        │    Generate Command
        │         ↓
        │    Update Cache
        │         │
        └────┬────┘
             ↓
        Return Command
```

### Command Format

**Structured Command (JSON):**
```json
{
    "command": "READ_CPU_STATS",
    "parameters": {},
    "trust_level": 0
}
```

**With Parameters:**
```json
{
    "command": "KILL_PROCESS",
    "parameters": {
        "pid": 1234
    },
    "trust_level": 2
}
```

---

## Performance Targets

| Metric | Target | Measured | Status |
|--------|--------|----------|--------|
| Query normalization | <1ms | TBD | ⏳ |
| Cache lookup | <0.1ms | TBD | ⏳ |
| Cache hit rate | >80% | TBD | ⏳ |
| Cached query (total) | <100ms | TBD | ⏳ |
| Uncached query (total) | <600ms | TBD | ⏳ |
| Command parsing | <1ms | TBD | ⏳ |

---

## Dependencies

**Week 3 Components:**
- Decision cache (C implementation)
- 103 pre-loaded patterns
- FNV-1a hash function

**Week 5 Components:**
- AI agent (agent.py)
- Phi-3 Mini model
- IPC client (mock implementation)

**New Requirements:**
- Python hash function matching C FNV-1a
- Command structure definition
- Integration tests

---

## Issues / Blockers

**Current Blockers:** None

**Potential Risks:**
1. **Hash mismatch (Python vs C)** - May need ctypes wrapper
   - Mitigation: Use identical FNV-1a implementation
   - Fallback: Python-only cache for Week 6, C integration Week 7

2. **Command format ambiguity** - AI may return inconsistent format
   - Mitigation: Strict prompts, format validation
   - Fallback: Multiple parsers (JSON, plain text, regex)

3. **Cache miss performance** - Decision cache in C, agent in Python
   - Mitigation: IPC for cache queries (Week 7)
   - Fallback: Python-side cache copy for Week 6

---

## Next Steps (Week 7)

1. **seL4 IPC Integration**
   - Connect AI agent to actual seL4 shared memory
   - Replace mock IPC with real implementation
   - Test message passing with seL4 QEMU

2. **Shell Interface**
   - Create text-based REPL
   - Connect to AI agent
   - Interactive command testing

3. **Command Execution**
   - Implement kernel handlers for commands
   - Execute commands in seL4
   - Return results to user

---

## Timeline

| Day | Planned Tasks | Planned Hours | Actual Tasks | Actual Hours | Status |
|-----|---------------|---------------|--------------|--------------|--------|
| Day 1 | Query processor, cache integration | 6-8 | TBD | 0 | ⏳ |
| Day 2 | Command parser, agent updates | 4-6 | TBD | 0 | ⏳ |
| Day 3 | Testing, documentation | 2-4 | TBD | 0 | ⏳ |
| **Total** | | **12-18** | | **0** | ⏳ |

---

## Week 6 Summary

**Status:** ⏳ **IN PROGRESS**

**Completion:** 0/6 tasks complete

**Files Created:**
- (To be filled during execution)

**Code Metrics:**
- (To be filled during execution)

**Lessons Learned:**
- (To be filled during execution)

---

**Week 6 Milestone:** Query Processing Pipeline Functional
**Confidence:** TBD
**Date Started:** November 16, 2025
**Next:** Week 7 - Shell Interface & seL4 IPC Integration
