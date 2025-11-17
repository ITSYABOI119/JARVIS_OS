# Phase 1 Technical Debt Tracker

**Last Updated:** November 17, 2025
**Total Debt Items:** 2
**Critical Items:** 0 (IPC SHM_SIZE bug resolved in Week 9)

---

## Active Debt

### DEBT #1: stdin Implementation ⚠️ PRIORITY: MEDIUM (downgraded)
**Source:** Week 2 - seL4 Serial Console
**Issue:** Interactive stdin requires I/O port capabilities in seL4
**Root Cause:** Direct port I/O (`inb`) from user space triggers cap fault
**Attempts (Week 4):**
- ❌ sys_readv override → conflicts with libsel4muslcsys
- ❌ Direct serial I/O → requires I/O capabilities
- 📋 Proper solution needs seL4 I/O capability setup

**Impact:**
- Cannot test interactive shell commands in Phase 1 PoC
- Deferred to Phase 2 (real hardware + proper driver framework)
- Demo shell sufficient for Phase 1 validation

**Mitigation:**
- ✅ Demo shell proves command processing logic works
- ✅ Decision cache validated with automated commands
- ✅ IPC can be tested programmatically (no user input needed)

**Resolution Plan (Updated Week 4):**
- **When:** Week 5+ or defer to Phase 2
- **How:** Either:
  1. Implement I/O capability allocation in rootserver (complex)
  2. Use seL4 serial driver with proper capability setup
  3. Defer to Phase 2 driver framework
- **Effort:** 10-16 hours (more complex than initially estimated)
- **Decision:** Defer to Phase 2 - not blocking Phase 1 PoC goals

**Acceptance Criteria (Phase 2):**
- Interactive stdin works in seL4 environment
- Can type commands at jarvis> prompt
- Proper I/O capabilities configured

---

### DEBT #2: Custom Build System Integration 📊 PRIORITY: MEDIUM
**Source:** Week 2 - seL4 Serial Console
**Issue:** Created custom CMakeLists.txt but currently using hello-world tutorial framework
**Impact:**
- Build works but not via our custom build system
- Executable named "hello-world" not "jarvis-sel4"
- Cannot easily add new source files to build

**Mitigation:**
- ✅ Current build process works and is repeatable
- ✅ Custom CMakeLists.txt exists for reference
- ✅ Can manually add files to tutorial CMakeLists.txt

**Resolution Plan:**
- **When:** Week 7-8 (when adding multiple source files for AI agent)
- **How:** Integrate custom CMakeLists.txt with seL4 SDK properly
- **Effort:** 4-6 hours estimated
- **Alternatives:**
  - Keep using tutorial framework (works fine)
  - Gradually migrate to standalone build

**Acceptance Criteria:**
- Can build with custom CMakeLists.txt
- Executable named "jarvis-sel4"
- Easy to add new .c/.h files

**Deferred Rationale:** Not blocking any current work, tutorial framework sufficient for now

---

## Resolved Debt

### RESOLVED: IPC SHM_SIZE Bug ✅ Week 9
**Source:** Week 9 - Python IPC Integration Testing
**Issue:** IPC send operations failing with "data out of range" error
**Root Cause:** Shared memory size (SHM_SIZE) was only 4KB but ring buffer needed 277KB
  - Ring buffer: 1024 slots × 276 bytes/message = 282,624 bytes needed
  - Allocated: Only 4096 bytes (4KB)
  - Writing beyond 4KB caused buffer overflow

**Impact:**
- Python IPC client tests failing (2/6 → 6/6 after fix)
- Could not send messages to seL4
- Blocked Week 9 integration testing

**Resolution (Week 9):**
- **Fix:** Increased SHM_SIZE from 4096 to 283648 bytes (~277KB)
- **Location:** phase1/src/ai/ipc_client.py line 41
- **Additional fixes:**
  - ctypes payload handling: `ctypes.memmove()` for byte copy
  - Struct serialization: `ctypes.string_at()` for converting to bytes
- **Result:** All 6/6 IPC integration tests passing

**Commits:**
- 16ceade: Fix SHM_SIZE to accommodate full ring buffer (4KB → 277KB)
- d17b1ec: Fix IPC payload copy using ctypes.memmove()
- 82caffe: Fix ctypes struct serialization using string_at()

**Lessons Learned:**
- Always validate buffer size calculations (1024 × 276 = 282,624, not 4096)
- ctypes arrays require special handling (memmove, not direct assignment)
- Debug logging critical for isolating issues across Python/C boundary

---

## Debt Metrics

| Week | New Debt | Resolved | Total Outstanding |
|------|----------|----------|-------------------|
| Week 1 | 0 | 0 | 0 |
| Week 2 | 2 | 0 | 2 |
| Week 3 | 0 | 0 | 2 |
| Week 4 | 0 | 0 | 2 |
| Week 5-8 | 0 | 0 | 2 |
| Week 9 | 1 (SHM_SIZE) | 1 (SHM_SIZE) | 2 |

**Target:** Resolve all HIGH priority debt by Week 8 (before AI integration)

---

## Debt Review Schedule

- **Week 4:** Resolve DEBT #1 (stdin) - CRITICAL for Week 5+
- **Week 7-8:** Consider DEBT #2 (build system) if needed
- **Week 10:** Review any new debt from Weeks 3-9
- **Week 20:** Final debt cleanup before integration phase

---

## Notes

- Technical debt is EXPECTED in proof-of-concept work
- Key is to TRACK it explicitly and resolve before it blocks progress
- Week 2 identified both items early (proactive risk management ✅)
- No debt items block Week 3 (Decision Cache)
