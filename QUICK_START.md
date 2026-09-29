# Quick Start Guide - Person 3 & 4

**5-Minute Setup and Test**

---

## Step 1: Verify Prerequisites

Open PowerShell and check:

```powershell
gcc --version
```

If GCC is not installed, install MinGW from: https://www.mingw-w64.org/

---

## Step 2: Navigate to Tests Directory

```powershell
cd "d:\Operating System\CP\live-deadlock-detector\tests"
```

---

## Step 3: Run Build Script

```powershell
.\build_and_test.ps1
```

**Expected Output:**
```
==========================================
  Building Person 3 & 4 Detection Modules
==========================================

Compiling...
Build successful!

==========================================
  Running Tests
==========================================

--- Deadlock Detector Tests ---
✓ All tests passed!

--- Priority Inversion Detector Tests ---
✓ All tests passed!

--- Remediation Module Tests ---
✓ All remediation tests passed!

All tests passed!
```

---

## Step 4: Explore Test Outputs

The demos at the end show:

1. **Deadlock Detection:**
   - Cycle detection
   - Thread and lock identification

2. **Priority Inversion:**
   - RT thread priorities
   - Policy detection

3. **Remediation:**
   - Lock ordering advice
   - Priority boost recommendations

---

## What Just Happened?

✅ Compiled 8 C source files  
✅ Ran 20+ unit tests  
✅ Verified all detection algorithms  
✅ Demonstrated remediation modules  

---

## File Structure

```
tests/
├── build_and_test.ps1       ← You ran this
├── test_deadlock_detector.c  ← 6 tests
├── test_pi_detector.c        ← 8 tests
├── test_remediation.c        ← 6 tests
└── fixtures/
    └── test_fixtures.h       ← Fake test data
```

---

## Next: Read Full Documentation

See `ASHWIN_README.md` for:
- Complete implementation details
- Architecture explanation
- Integration plan with Pranav
- Mid-sem demo guide

---

## Troubleshooting

**Problem:** GCC not found  
**Solution:** Install MinGW or add GCC to PATH

**Problem:** Build fails  
**Solution:** Check that you're in the `tests/` directory

**Problem:** Tests fail  
**Solution:** This shouldn't happen - all tests are designed to pass. Check if files were modified.

---

**That's it! You're ready for the mid-sem presentation.**
