#!/usr/bin/env bash
# =============================================================================
# scripts/setup_ubuntu.sh
#
# Environment setup and verification for Phase G on Ubuntu 22.04.
# Run this script ONCE on the Ubuntu VM before using the tracer.
#
# Usage:
#   chmod +x scripts/setup_ubuntu.sh
#   ./scripts/setup_ubuntu.sh
#
# What it does:
#   1. Prints current environment (kernel, arch, GCC, Python).
#   2. Installs BCC, kernel headers, and build tools if missing.
#   3. Verifies BCC Python import.
#   4. Checks libc symbol availability for probes.
#   5. Builds the smoke-test program and the mock-pipeline test binary.
#   6. Runs the existing 148-check regression suite.
#
# Does NOT run the live eBPF tracer (that requires a target PID).
# =============================================================================

set -e
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

PASS=0
FAIL=0

check() {
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then
        echo "  OK   $desc"
        PASS=$((PASS + 1))
    else
        echo "  MISS $desc"
        FAIL=$((FAIL + 1))
    fi
}

echo "============================================================"
echo " Live Deadlock Detector — Ubuntu environment check / setup"
echo "============================================================"
echo ""

# ── 1. Environment info ──────────────────────────────────────────────────────
echo "--- Environment ---"
echo "Kernel:  $(uname -r)"
echo "Arch:    $(uname -m)"
echo "GCC:     $(gcc --version 2>/dev/null | head -1 || echo 'NOT FOUND')"
echo "Python3: $(python3 --version 2>/dev/null || echo 'NOT FOUND')"
echo "Make:    $(make --version 2>/dev/null | head -1 || echo 'NOT FOUND')"
echo ""

# ── 2. Install missing packages ──────────────────────────────────────────────
echo "--- Checking / installing packages ---"
KERNEL=$(uname -r)
NEEDED_PKGS=""

dpkg -l bpfcc-tools        >/dev/null 2>&1 || NEEDED_PKGS="$NEEDED_PKGS bpfcc-tools"
dpkg -l python3-bpfcc      >/dev/null 2>&1 || NEEDED_PKGS="$NEEDED_PKGS python3-bpfcc"
dpkg -l linux-headers-$KERNEL >/dev/null 2>&1 || NEEDED_PKGS="$NEEDED_PKGS linux-headers-$KERNEL"
dpkg -l build-essential    >/dev/null 2>&1 || NEEDED_PKGS="$NEEDED_PKGS build-essential"

if [ -n "$NEEDED_PKGS" ]; then
    echo "  Installing:$NEEDED_PKGS"
    sudo apt-get update -qq
    sudo apt-get install -y $NEEDED_PKGS
else
    echo "  All required packages already installed."
fi
echo ""

# ── 3. Verify BCC ────────────────────────────────────────────────────────────
echo "--- Verifying BCC ---"
check "python3 bcc import" python3 -c "from bcc import BPF"
check "kernel headers present" test -d "/usr/src/linux-headers-$KERNEL"
echo ""

# ── 4. Verify libc symbols ───────────────────────────────────────────────────
echo "--- Checking libc probe symbols ---"
LIBC_PATH=$(ldconfig -p 2>/dev/null | grep "libc.so.6" | grep "x86-64" | \
            awk '{print $NF}' | head -1)
if [ -z "$LIBC_PATH" ]; then
    LIBC_PATH="/lib/x86_64-linux-gnu/libc.so.6"
fi
echo "  libc: $LIBC_PATH"

check "pthread_mutex_lock symbol"    nm -D "$LIBC_PATH" | grep -q "pthread_mutex_lock"
check "pthread_mutex_unlock symbol"  nm -D "$LIBC_PATH" | grep -q "pthread_mutex_unlock"
check "pthread_mutex_trylock symbol" nm -D "$LIBC_PATH" | grep -q "pthread_mutex_trylock"
check "pthread_exit symbol"          nm -D "$LIBC_PATH" | grep -q "pthread_exit"
echo ""

# ── 5. Check BPF permissions ─────────────────────────────────────────────────
echo "--- BPF permissions ---"
PARANOIA=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "unknown")
echo "  perf_event_paranoid = $PARANOIA"
if [ "$PARANOIA" -gt 2 ] 2>/dev/null; then
    echo "  WARNING: perf_event_paranoid=$PARANOIA may block non-root eBPF."
    echo "  The tracer must be run as root (sudo)."
fi
echo ""

# ── 6. Build smoke-test program ──────────────────────────────────────────────
echo "--- Building smoke-test program ---"
mkdir -p build/bin
gcc -std=c11 -Wall -pthread \
    tests/scenarios/simple_mutex.c \
    -o build/bin/simple_mutex
echo "  Built: build/bin/simple_mutex"
echo ""

# ── 7. Run existing regression suite ─────────────────────────────────────────
echo "--- Running regression suite (make test) ---"
make clean >/dev/null
make test
echo ""

# ── 8. Summary ───────────────────────────────────────────────────────────────
echo "============================================================"
echo " Setup checks: $PASS OK  $FAIL MISSING"
if [ $FAIL -gt 0 ]; then
    echo " Some checks failed. See output above."
    exit 1
else
    echo " All checks passed. Ready to run the tracer."
    echo ""
    echo " Live smoke test (run as two terminals):"
    echo "   Terminal 1:  ./build/bin/simple_mutex"
    echo "   Terminal 2:  sudo python3 src/tracer/tracer.py --verbose \$(pgrep simple_mutex)"
    echo ""
    echo " Pipe to collector (not yet wired in Phase G):"
    echo "   sudo python3 src/tracer/tracer.py \$(pgrep simple_mutex) | \\"
    echo "        ./build/bin/ldd_collector"
fi
echo "============================================================"
