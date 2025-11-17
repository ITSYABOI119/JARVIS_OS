# Phase 1 Technical Debt Tracker

**Last Updated:** November 15, 2025
**Total Debt Items:** 2
**Critical Items:** 1 (stdin for Week 5+ AI testing)

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

(None yet)

---

## Debt Metrics

| Week | New Debt | Resolved | Total Outstanding |
|------|----------|----------|-------------------|
| Week 1 | 0 | 0 | 0 |
| Week 2 | 2 | 0 | 2 |
| Week 3 | TBD | TBD | TBD |
| Week 4 | TBD | 1 (stdin) | TBD |

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
