#!/usr/bin/env python3
"""
src/tracer/tracer.py

BCC Python loader for the Live Deadlock Detector eBPF tracer.

Loads the eBPF C program (mutex_events.bpf.c), attaches uprobes to
pthread_mutex_lock, pthread_mutex_trylock, and pthread_mutex_unlock in the
target process, then reads events from the BPF perf buffer and writes them to
stdout as framed binary structs for consumption by the C collector.

Frame format (matches src/collector/transport_fd.c):
    [uint16_t payload_len_le][uint8_t payload[payload_len]]

Usage:
    sudo python3 src/tracer/tracer.py <pid>

    Pipe to the collector:
    sudo python3 src/tracer/tracer.py <pid> | ./build/bin/ldd_collector

Requirements (Ubuntu 22.04, kernel >= 5.x):
    sudo apt install -y bpfcc-tools python3-bpfcc linux-headers-$(uname -r)

Privileges:
    Must run as root or with CAP_BPF + CAP_SYS_ADMIN.

Notes:
    - This file does NOT implement deadlock detection or remediation.
    - It only produces events matching the schema in src/common/events.h.
    - All detection is performed by Developer B's modules (Phases not yet
      implemented at time of Phase G).
    - Loss counts are reported to stderr and via the standard
      EVENT_LOST_EVENTS mechanism (see docs/EVENT_SCHEMA.md §4.6).
"""

import sys
import os
import struct
import signal
import ctypes
import argparse

# ─────────────────────────────────────────────────────────────────────────────
# Argument parsing
# ─────────────────────────────────────────────────────────────────────────────

def parse_args():
    p = argparse.ArgumentParser(
        description="BCC-based mutex event tracer for live-deadlock-detector")
    p.add_argument("pid", type=int,
                   help="PID of the target process to trace")
    p.add_argument("--libc", default=None,
                   help="Path to libc shared object (default: auto-detect)")
    p.add_argument("--no-trylock", action="store_true",
                   help="Skip pthread_mutex_trylock probes (use if symbol absent)")
    p.add_argument("--no-exit", action="store_true",
                   help="Skip pthread_exit probes (use if symbol absent)")
    p.add_argument("--verbose", "-v", action="store_true",
                   help="Print decoded event summaries to stderr")
    return p.parse_args()


# ─────────────────────────────────────────────────────────────────────────────
# Schema constants — must match src/common/events.h
# ─────────────────────────────────────────────────────────────────────────────

LDD_SCHEMA_VERSION        = 1
EVENT_LOCK_REQUEST        = 1
EVENT_LOCK_ACQUIRED       = 2
EVENT_LOCK_ACQUIRE_FAILED = 3
EVENT_LOCK_RELEASED       = 4
EVENT_THREAD_EXIT         = 5
EVENT_LOST_EVENTS         = 6

LDD_SCHED_UNAVAILABLE = -1
LDD_PRIO_UNAVAILABLE  = -1

_EVENT_NAMES = {
    EVENT_LOCK_REQUEST:        "LOCK_REQUEST",
    EVENT_LOCK_ACQUIRED:       "LOCK_ACQUIRED",
    EVENT_LOCK_ACQUIRE_FAILED: "LOCK_ACQUIRE_FAILED",
    EVENT_LOCK_RELEASED:       "LOCK_RELEASED",
    EVENT_THREAD_EXIT:         "THREAD_EXIT",
}


# ─────────────────────────────────────────────────────────────────────────────
# Output framing
# ─────────────────────────────────────────────────────────────────────────────

_stdout_bin = None

def _write_event(data: bytes) -> None:
    """Write one framed event to stdout: [uint16_t len_le][payload]."""
    if len(data) > 64:
        print(f"[tracer] WARNING: event too large ({len(data)} bytes), dropping",
              file=sys.stderr)
        return
    frame = struct.pack("<H", len(data)) + data
    _stdout_bin.write(frame)
    _stdout_bin.flush()


def _write_lost_events(lost_count: int) -> None:
    """Synthesise and write an EVENT_LOST_EVENTS frame to stdout."""
    # struct ldd_lost_events: schema_version(1) event_type(1) lost_count(8) ts(8)
    # Total: 18 bytes, packed
    payload = struct.pack("<BBQq",
                          LDD_SCHEMA_VERSION,
                          EVENT_LOST_EVENTS,
                          lost_count,
                          0)  # timestamp_ns = 0 (user-space; no kernel clock here)
    _write_event(payload)
    print(f"[tracer] WARNING: {lost_count} event(s) lost from perf buffer",
          file=sys.stderr)


# ─────────────────────────────────────────────────────────────────────────────
# Perf buffer callback
# ─────────────────────────────────────────────────────────────────────────────

_verbose = False

def _on_event(cpu, data, size):
    """Called by BCC for each event from the perf buffer."""
    raw = bytes(ctypes.string_at(data, size))
    if len(raw) < 2:
        print("[tracer] WARNING: received event shorter than 2 bytes", file=sys.stderr)
        return

    event_type = raw[1]

    if _verbose:
        schema_ver = raw[0]
        name = _EVENT_NAMES.get(event_type, f"UNKNOWN({event_type})")
        if len(raw) >= 28 and event_type in (EVENT_LOCK_REQUEST, EVENT_LOCK_ACQUIRED,
                                              EVENT_LOCK_ACQUIRE_FAILED,
                                              EVENT_LOCK_RELEASED, EVENT_THREAD_EXIT):
            # Parse common header: B B I I Q Q 2s  (packed, 28 bytes)
            sv, et, pid, tid, ts_ns, lock_addr = struct.unpack_from("<BBIIQQ", raw, 0)
            print(f"[tracer] {name} pid={pid} tid={tid} "
                  f"lock=0x{lock_addr:016x} ts={ts_ns}",
                  file=sys.stderr)
        else:
            print(f"[tracer] {name} ({len(raw)} bytes)", file=sys.stderr)

    _write_event(raw)


def _on_lost(lost_count):
    """Called by BCC when perf buffer overflows."""
    _write_lost_events(lost_count)


# ─────────────────────────────────────────────────────────────────────────────
# libc path detection
# ─────────────────────────────────────────────────────────────────────────────

def _find_libc(pid: int) -> str:
    """Read /proc/<pid>/maps to find the libc path in the target process."""
    maps_path = f"/proc/{pid}/maps"
    try:
        with open(maps_path) as f:
            for line in f:
                if "libc" in line and ".so" in line and "r-xp" in line:
                    parts = line.strip().split()
                    if len(parts) >= 6:
                        path = parts[5]
                        if os.path.exists(path):
                            return path
    except (IOError, OSError) as e:
        print(f"[tracer] WARNING: could not read {maps_path}: {e}", file=sys.stderr)
    # Fallback
    for candidate in [
        "/lib/x86_64-linux-gnu/libc.so.6",
        "/lib/x86_64-linux-gnu/libpthread.so.0",
        "/usr/lib/x86_64-linux-gnu/libc.so.6",
    ]:
        if os.path.exists(candidate):
            return candidate
    raise RuntimeError("Could not locate libc for target process")


def _symbol_exists(lib_path: str, symbol: str) -> bool:
    """Return True if symbol is exported from the shared library."""
    try:
        import subprocess
        result = subprocess.run(
            ["nm", "-D", lib_path],
            capture_output=True, text=True, timeout=5)
        return symbol in result.stdout
    except Exception:
        # If nm is unavailable, assume symbol exists; attach will fail loudly.
        return True


# ─────────────────────────────────────────────────────────────────────────────
# BPF program source path
# ─────────────────────────────────────────────────────────────────────────────

def _bpf_source_path() -> str:
    """Return the path to mutex_events.bpf.c relative to this script."""
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, "mutex_events.bpf.c")
    if not os.path.exists(path):
        raise FileNotFoundError(f"eBPF source not found: {path}")
    return path


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main():
    global _stdout_bin, _verbose

    args = parse_args()
    _verbose = args.verbose

    # Use binary stdout for framed output.
    _stdout_bin = sys.stdout.buffer

    # ── Check privileges ──────────────────────────────────────────────────
    if os.geteuid() != 0:
        print("[tracer] ERROR: must run as root (or with CAP_BPF + CAP_SYS_ADMIN)",
              file=sys.stderr)
        sys.exit(1)

    # ── Verify target process ─────────────────────────────────────────────
    if not os.path.exists(f"/proc/{args.pid}"):
        print(f"[tracer] ERROR: process {args.pid} not found", file=sys.stderr)
        sys.exit(1)

    # ── Locate libc ───────────────────────────────────────────────────────
    try:
        libc_path = args.libc if args.libc else _find_libc(args.pid)
    except RuntimeError as e:
        print(f"[tracer] ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"[tracer] Target PID={args.pid}", file=sys.stderr)
    print(f"[tracer] libc path: {libc_path}", file=sys.stderr)

    # ── Import BCC ────────────────────────────────────────────────────────
    try:
        from bcc import BPF
    except ImportError:
        print("[tracer] ERROR: python3-bpfcc not installed.\n"
              "  Install with: sudo apt install -y bpfcc-tools python3-bpfcc "
              "linux-headers-$(uname -r)",
              file=sys.stderr)
        sys.exit(1)

    # ── Load eBPF program ─────────────────────────────────────────────────
    bpf_src = _bpf_source_path()
    print(f"[tracer] Loading eBPF program from: {bpf_src}", file=sys.stderr)
    try:
        b = BPF(src_file=bpf_src)
    except Exception as e:
        print(f"[tracer] ERROR loading eBPF program: {e}", file=sys.stderr)
        sys.exit(1)

    # ── Attach uprobes ────────────────────────────────────────────────────
    attached = []

    def attach_uprobe_safe(fn_name, sym, lib, pid, retprobe=False):
        """Attach a uprobe; print a warning on failure instead of crashing."""
        try:
            if retprobe:
                b.attach_uretprobe(name=lib, sym=sym, fn_name=fn_name, pid=pid)
            else:
                b.attach_uprobe(name=lib, sym=sym, fn_name=fn_name, pid=pid)
            attached.append((fn_name, sym, "uretprobe" if retprobe else "uprobe"))
            print(f"[tracer] Attached {'uretprobe' if retprobe else 'uprobe'} "
                  f"{sym} -> {fn_name}", file=sys.stderr)
        except Exception as e:
            print(f"[tracer] WARNING: could not attach "
                  f"{'uretprobe' if retprobe else 'uprobe'} {sym}: {e}",
                  file=sys.stderr)

    # pthread_mutex_lock — entry (saves lock_addr) + return
    attach_uprobe_safe("probe_mutex_lock_entry",      "pthread_mutex_lock",
                       libc_path, args.pid)
    attach_uprobe_safe("probe_mutex_lock_entry_save", "pthread_mutex_lock",
                       libc_path, args.pid)
    attach_uprobe_safe("probe_mutex_lock_return",     "pthread_mutex_lock",
                       libc_path, args.pid, retprobe=True)

    # pthread_mutex_trylock — entry + return (optional)
    if not args.no_trylock and _symbol_exists(libc_path, "pthread_mutex_trylock"):
        attach_uprobe_safe("probe_mutex_trylock_entry",      "pthread_mutex_trylock",
                           libc_path, args.pid)
        attach_uprobe_safe("probe_mutex_trylock_entry_save", "pthread_mutex_trylock",
                           libc_path, args.pid)
        attach_uprobe_safe("probe_mutex_trylock_return",     "pthread_mutex_trylock",
                           libc_path, args.pid, retprobe=True)
    else:
        print("[tracer] Skipping pthread_mutex_trylock probes "
              "(symbol absent or --no-trylock)", file=sys.stderr)

    # pthread_mutex_unlock — entry only
    attach_uprobe_safe("probe_mutex_unlock_entry", "pthread_mutex_unlock",
                       libc_path, args.pid)

    # pthread_exit — entry (optional)
    if not args.no_exit and _symbol_exists(libc_path, "pthread_exit"):
        attach_uprobe_safe("probe_thread_exit", "pthread_exit",
                           libc_path, args.pid)
    else:
        print("[tracer] Skipping pthread_exit probe "
              "(symbol absent or --no-exit)", file=sys.stderr)

    if not attached:
        print("[tracer] ERROR: no probes attached; aborting", file=sys.stderr)
        sys.exit(1)

    print(f"[tracer] {len(attached)} probe(s) attached. "
          f"Streaming events to stdout...", file=sys.stderr)

    # ── Open perf buffer ──────────────────────────────────────────────────
    b["mutex_events"].open_perf_buffer(_on_event, lost_cb=_on_lost)

    # ── Signal handler ────────────────────────────────────────────────────
    _running = [True]

    def _sighandler(sig, frame):
        _running[0] = False
        print("\n[tracer] Shutting down...", file=sys.stderr)

    signal.signal(signal.SIGINT,  _sighandler)
    signal.signal(signal.SIGTERM, _sighandler)

    # ── Event loop ────────────────────────────────────────────────────────
    while _running[0]:
        try:
            b.perf_buffer_poll(timeout=100)
        except KeyboardInterrupt:
            break
        # Check if the target process is still alive
        if not os.path.exists(f"/proc/{args.pid}"):
            print(f"[tracer] Target process {args.pid} has exited",
                  file=sys.stderr)
            break

    print("[tracer] Done.", file=sys.stderr)


if __name__ == "__main__":
    main()
