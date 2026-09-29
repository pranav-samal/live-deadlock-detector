# =============================================================================
# Makefile — Unified Live Deadlock & Priority-Inversion Detector
# =============================================================================
#
# Targets
# -------
#   all               Compile every test binary (default)
#   build             Alias for all
#   test              Run all unit and integration tests; exit 1 if any fail
#   test-unit         Run only the three unit test binaries
#   test-integration  Run only the integration test binary
#   clean             Remove the build directory and generated binaries
#   help              Print this target list
#
# Environment
# -----------
#   Requires GCC and GNU Make on Linux (or WSL).
#   Tested with: GCC 15.2.0, GNU Make 4.4.1 (Ubuntu via WSL).
#   Do NOT use MinGW on Windows — it misreports packed struct sizes.
#   See docs/EVENT_SCHEMA.md §5 for details.
#
# Scope
# -----
#   Builds and runs Phases B–E: mock transport, collector, lock state
#   manager, graph manager, and mock pipeline integration test.
#   Phase G (eBPF/BCC tracer) requires the Ubuntu VM and is NOT built here.
#   This Makefile validates the mock pipeline only, not live eBPF tracing.
# =============================================================================

# --- Toolchain ---------------------------------------------------------------

CC     := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror
# -Isrc   makes #include paths relative to src/
# -Itests makes tests/helpers/* reachable without path prefixes
INCS   := -Isrc -Itests

# --- Build directory ---------------------------------------------------------
# Use a name that cannot clash with a phony target alias.

BINDIR := build/bin

# --- Source files shared between test binaries -------------------------------

TRANSPORT_SRC := src/collector/transport_mock.c
COLLECTOR_SRC := src/collector/collector.c
STATE_SRC     := src/state/lock_state.c
GRAPH_SRC     := src/graph/graph.c

# collector.c calls state + graph; link them together for every test that
# uses the full collector pipeline.
PIPELINE_SRCS := $(TRANSPORT_SRC) $(COLLECTOR_SRC) $(STATE_SRC) $(GRAPH_SRC)

# --- Test binary paths -------------------------------------------------------

B_BIN := $(BINDIR)/test_collector    # Phase B — collector unit tests
C_BIN := $(BINDIR)/test_state        # Phase C — lock state unit tests
D_BIN := $(BINDIR)/test_graph        # Phase D — graph manager unit tests
E_BIN := $(BINDIR)/test_mock_pipeline # Phase E — mock pipeline integration

ALL_BINS := $(B_BIN) $(C_BIN) $(D_BIN) $(E_BIN)

# =============================================================================
# Phony targets
# =============================================================================

.PHONY: all build test test-unit test-integration clean help

all: $(ALL_BINS)
	@echo ""
	@echo "Build complete. Run 'make test' to execute all tests."

# 'build' as a separate alias avoids the directory/target name clash by
# having it depend on the real default target rather than re-listing recipes.
build: all

# --- Directory creation rule -------------------------------------------------

$(BINDIR):
	mkdir -p $(BINDIR)

# =============================================================================
# Compile rules
# =============================================================================

# Phase B: test_collector
# Needs the full pipeline (collector calls state + graph after Phase C/D wiring)
$(B_BIN): tests/unit/test_collector.c $(PIPELINE_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCS) $^ -o $@

# Phase C: test_state
# State manager has no dependency on collector or graph
$(C_BIN): tests/unit/test_state.c $(STATE_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCS) $^ -o $@

# Phase D: test_graph
# Graph manager has no dependency on state or collector
$(D_BIN): tests/unit/test_graph.c $(GRAPH_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCS) $^ -o $@

# Phase E: mock pipeline integration (full stack)
$(E_BIN): tests/integration/test_mock_pipeline.c $(PIPELINE_SRCS) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCS) $^ -o $@

# =============================================================================
# Test runners
# =============================================================================

test-unit: $(B_BIN) $(C_BIN) $(D_BIN)
	@echo ""; echo ">>> $(B_BIN)"; $(B_BIN)
	@echo ""; echo ">>> $(C_BIN)"; $(C_BIN)
	@echo ""; echo ">>> $(D_BIN)"; $(D_BIN)
	@echo ""; echo "All unit tests passed."

test-integration: $(E_BIN)
	@echo ""; echo ">>> $(E_BIN)"; $(E_BIN)
	@echo ""; echo "All integration tests passed."

test: test-unit test-integration
	@echo ""
	@echo "================================================"
	@echo " All tests passed."
	@echo " NOTE: mock pipeline only — NOT live eBPF/BCC."
	@echo "================================================"

# =============================================================================
# Clean
# =============================================================================

clean:
	rm -rf build
	@echo "Removed build/. Source files preserved."

# =============================================================================
# Help
# =============================================================================

help:
	@echo ""
	@echo "Targets:"
	@echo "  all / build       Build all test binaries -> $(BINDIR)/"
	@echo "  test              Run all unit + integration tests"
	@echo "  test-unit         Run Phase B, C, D unit tests only"
	@echo "  test-integration  Run Phase E integration test only"
	@echo "  clean             Remove build/ directory"
	@echo "  help              Show this message"
	@echo ""
	@echo "Platform: Linux/WSL with GCC (not MinGW)."
	@echo "Phase G eBPF tracer is not built here."
	@echo ""
