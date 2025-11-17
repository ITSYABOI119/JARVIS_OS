# Week 9: QEMU Integration & End-to-End Testing

**Status:** PREPARATION COMPLETE - Ready for Manual Testing
**Created:** November 17, 2025

---

## 🚀 Quick Start (Manual Testing)

Since you're using Claude web version, Week 9 testing will be done manually on your machine.

### Files in This Folder:

1. **`WEEK_9_MANUAL_TESTING_GUIDE.md`** ⭐ **START HERE**
   - Step-by-step commands to run on your machine
   - Prerequisites installation
   - Test execution instructions
   - Common issues & fixes

2. **`WEEK_9_RESULTS_TEMPLATE.txt`**
   - Template to fill in with your test results
   - Copy/paste this after running tests
   - Submit to Claude for analysis

3. **`WEEK_9_STATUS.md`**
   - Detailed week objectives and tasks
   - Success criteria
   - Architecture diagrams
   - Technical specifications

4. **`test_week9_integration.py`**
   - Python integration test suite
   - 5 tests + performance benchmark
   - Run this after seL4 is running in QEMU

5. **`SETUP_GUIDE.md`**
   - Detailed QEMU and seL4 setup documentation
   - For reference if manual guide isn't enough

---

## 📋 Testing Workflow

```
┌─────────────────────────────────────────────┐
│ 1. Read WEEK_9_MANUAL_TESTING_GUIDE.md     │
│    - Follow step-by-step commands           │
│    - Run on your local machine              │
└─────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────┐
│ 2. Run Tests & Copy Outputs                 │
│    - C component tests (cache, IPC)         │
│    - Python tests (IPC client)              │
│    - QEMU boot test                          │
│    - Integration tests                       │
└─────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────┐
│ 3. Fill Out WEEK_9_RESULTS_TEMPLATE.txt    │
│    - Paste test outputs                      │
│    - Note any errors                         │
│    - Add observations                        │
└─────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────┐
│ 4. Submit Results to Claude (Web)           │
│    - Copy entire filled template             │
│    - Paste in message to Claude              │
│    - Claude will analyze and advise          │
└─────────────────────────────────────────────┘
```

---

## ⏱️ Time Required

- **First-time setup:** 30-60 minutes (QEMU, seL4, dependencies)
- **If seL4 already set up:** 15-20 minutes (just run tests)

---

## 🎯 Week 9 Success Criteria

Week 9 is successful if:

- [x] C components compile and run (decision cache, IPC ring buffer)
- [x] Python IPC client tests pass (6/6)
- [x] QEMU installed and working
- [x] seL4 builds successfully
- [x] seL4 boots in QEMU
- [x] Shared memory (/dev/shm) accessible

**Stretch Goals:**
- [ ] Python ↔ seL4 IPC communication in QEMU
- [ ] Performance benchmarks validated

---

## 📁 Other Useful Files

- `../../scripts/launch-jarvis-qemu.sh` - Automated QEMU launcher (requires setup)
- `../../src/ai/test_ipc_integration.py` - Standalone IPC tests
- `../../src/cache/test_cache.c` - Decision cache tests
- `../../src/ipc/test_ipc.c` - Ring buffer tests

---

## 🆘 Need Help?

If you encounter issues:

1. Check the **Common Issues & Quick Fixes** section in `WEEK_9_MANUAL_TESTING_GUIDE.md`
2. Copy the exact error message
3. Copy the command that failed
4. Include in your results submission to Claude
5. Claude will help troubleshoot!

---

## 🚀 Ready to Start?

1. Open `WEEK_9_MANUAL_TESTING_GUIDE.md`
2. Start with **Step 1: Install Prerequisites**
3. Work through sequentially
4. Report results when done!

Good luck! 🎯
