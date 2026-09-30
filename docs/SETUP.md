# Environment Setup and Phase G Run Guide

**Phase G: Real eBPF tracing — Ubuntu VM only**

---

## 1. Target Environment

| Item | Value |
|---|---|
| OS | Ubuntu 22.04 LTS (VirtualBox VM) |
| Kernel | 6.8.0-138-generic, x86_64 |
| Architecture | x86-64 |
| BPF framework | BCC (python3-bpfcc) |
| eBPF program language | C (restricted subset) |
| Loader language | Python 3 |
| User-space language | Plain C (GCC, -std=c11) |

**Development machine:** Windows (used for Phases A–F only; no live eBPF available on Windows or WSL2-Microsoft kernel)

---

## 2. One-time Ubuntu Setup

Run this **once** on the Ubuntu VM after cloning the repository:

```bash
cd live-deadlock-detector
chmod +x scripts/setup_ubuntu.sh
./scripts/setup_ubuntu.sh
```

The script installs required packages, verifies symbols, builds the smoke-test program, and runs the full 148-check regression suite.

**Manual install (if preferred):**

```bash
KERNEL=$(uname -r)
sudo apt update
sudo apt install -y \
    bpfcc-tools \
    python3-bpfcc \
    linux-headers-${KERNEL} \
    build-essential

# Verify BCC
python3 -c "from bcc import BPF; print('BCC OK')"

# Verify kernel headers
ls /usr/src/linux-headers-${KERNEL}/
```

---

## 3. Build

```bash
# All mock/unit/integration tests (no Ubuntu kernel needed on Linux)
make clean && make test

# Smoke-test target program
make -C . build/bin/simple_mutex   # or use setup_ubuntu.sh
# OR manually:
gcc -std=c11 -Wall -pthread tests/scenarios/simple_mutex.c \
    -o build/bin/simple_mutex
```

---

## 4. Live Smoke Test

Run in two terminals on the Ubuntu VM:

**Terminal 1 — target process:**
```bash
./build/bin/simple_mutex
```
Expected output:
```
simple_mutex smoke test — PID 12345
Locks: mutex_a @ 0x...  mutex_b @ 0x...
[thread 0] iter 0  counter=1
...
```

**Terminal 2 — tracer (must be root):**
```bash
sudo python3 src/tracer/tracer.py --verbose $(pgrep simple_mutex)
```
Expected stderr output (events from eBPF):
```
[tracer] Target PID=12345
[tracer] libc path: /lib/x86_64-linux-gnu/libc.so.6
[tracer] Loading eBPF program from: src/tracer/mutex_events.bpf.c
[tracer] Attached uprobe pthread_mutex_lock -> probe_mutex_lock_entry
[tracer] Attached uprobe pthread_mutex_lock -> probe_mutex_lock_entry_save
[tracer] Attached uretprobe pthread_mutex_lock -> probe_mutex_lock_return
[tracer] Attached uprobe pthread_mutex_trylock -> probe_mutex_trylock_entry
...
[tracer] 8 probe(s) attached. Streaming events to stdout...
[tracer] LOCK_REQUEST pid=12345 tid=12346 lock=0x... ts=...
[tracer] LOCK_ACQUIRED pid=12345 tid=12346 lock=0x... ts=...
[tracer] LOCK_RELEASED pid=12345 tid=12346 lock=0x... ts=...
...
```

---

## 5. Verify Event Fields

Events written to stdout are raw binary. To inspect them alongside the tracer's verbose stderr:

```bash
# Decode first N bytes of each event frame:
sudo python3 src/tracer/tracer.py --verbose $(pgrep simple_mutex) \
    2>&1 | grep "LOCK_"
```

Field layout reference: `src/common/events.h` and `docs/EVENT_SCHEMA.md §5`.

Key checks:
- `pid` must match the target process PID
- `lock_addr` must match the address printed by `simple_mutex` (`mutex_a @ ...`)
- Events arrive in order: `LOCK_REQUEST` → `LOCK_ACQUIRED` → `LOCK_RELEASED`
- `schema_version` byte == 1 on every event

---

## 6. Known Limitations

| Limitation | Details |
|---|---|
| Requires root / CAP_BPF | `sudo` is required for uprobe attachment |
| WSL2 Microsoft kernel | uprobes on user-space libc are **not supported** on the WSL2 kernel; use the VirtualBox Ubuntu VM |
| `pthread_mutex_trylock` may be absent | Some glibc versions alias it to `pthread_mutex_lock`; use `--no-trylock` if probe attachment fails |
| CFS scheduling (SCHED_OTHER) | `sched_policy=0, sched_priority=0`; priority-inversion detection scoped to `SCHED_FIFO`/`SCHED_RR` only |
| Condition variables not traced | `pthread_cond_wait` interactions are out of scope in v1 |
| `pthread_mutex_destroy` not traced | Stale lock-address reuse handled by state manager cleanup policy |
| No deadlock detection yet | Detection modules (Person 3 / Ashwin) are not yet integrated |

---

## 7. What Is and Is Not Live-Tested

| Component | Status |
|---|---|
| events.h struct layout | Verified on Linux x86_64 GCC 15.2 |
| Mock transport + collector + state + graph | **148/148 checks PASS** (`make test`) |
| eBPF C program syntax | Written to BCC uprobe API; requires Ubuntu VM to load/verify |
| Python BCC loader | Written; requires BCC + Ubuntu VM to run |
| Live probe attachment | **Requires Ubuntu VM** — not runnable on Windows/WSL2 |
| Live event field values | **Requires Ubuntu VM** — not verifiable from Windows |
| Lost-event handling | Verified in mock pipeline (IT7 in Phase E) |

Record actual Ubuntu results below after running on the VM.

---

## 8. Ubuntu Results (to be filled in after VM run)

```
Kernel version:    (fill in: uname -r)
BCC version:       (fill in: python3 -c "import bcc; print(bcc.__version__)")
GCC version:       (fill in: gcc --version)
Probes attached:   (fill in: number from tracer.py output)
Events observed:   (fill in: event types seen from simple_mutex)
Lost events:       (fill in: yes/no, count if any)
Regression suite:  148/148 PASS (already verified)
```
