/*
 * src/collector/transport_mock.c
 *
 * Transport layer implementation for the Live Deadlock Detector.
 *
 * This file provides:
 *   - the concrete ldd_transport_read / ldd_transport_lost_count /
 *     ldd_transport_set_mock_source implementations
 *   - ldd_mock_sequence_install(): helper for tests that need to replay
 *     a fixed array of pre-built ldd_raw_event_t values
 *
 * In production the BCC Python tracer replaces this file with a version
 * that reads from a file descriptor (src/collector/transport_fd.c, Phase G).
 * The collector never calls transport internals directly; it only uses the
 * three functions declared in transport.h.
 *
 * Ownership: Developer A (Person 2 — collector)
 */

#include "transport.h"

#include <stddef.h>   /* NULL  */
#include <stdint.h>
#include <string.h>   /* memcpy */

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

/* Active mock read function, or NULL when no mock is installed. */
static int (*g_mock_read)(ldd_raw_event_t *) = NULL;

/* Simulated lost-event counter.  Tests can manipulate this directly via
 * ldd_mock_set_lost_count(); it is exposed here as a module-internal API
 * rather than through the public transport.h header. */
static uint64_t g_lost_count = 0;

/* -------------------------------------------------------------------------
 * Public transport interface (declared in transport.h)
 * ------------------------------------------------------------------------- */

int ldd_transport_read(ldd_raw_event_t *out)
{
    if (out == NULL) {
        return -1;
    }

    if (g_mock_read != NULL) {
        return g_mock_read(out);
    }

    /* No real transport and no mock installed — nothing available. */
    return 1;
}

uint64_t ldd_transport_lost_count(void)
{
    uint64_t count = g_lost_count;
    g_lost_count = 0;
    return count;
}

void ldd_transport_set_mock_source(int (*mock_read)(ldd_raw_event_t *))
{
    g_mock_read = mock_read;
}

/* -------------------------------------------------------------------------
 * Test-only helpers (not declared in transport.h)
 *
 * These are available to test translation units that include this .c file
 * directly or link against it. They must not be called from production code.
 * ------------------------------------------------------------------------- */

/*
 * ldd_mock_set_lost_count() — inject a simulated event-loss count.
 *
 * The next call to ldd_transport_lost_count() will return this value.
 * Used by tests that verify the collector's EVENT_LOST_EVENTS handling.
 */
void ldd_mock_set_lost_count(uint64_t n)
{
    g_lost_count = n;
}

/* -------------------------------------------------------------------------
 * Sequence mock helpers
 *
 * ldd_mock_sequence_install() installs a mock that replays a caller-
 * supplied array of ldd_raw_event_t values in order, then returns 1
 * (no more events) for every subsequent call.
 *
 * The array must remain valid for the lifetime of the mock session.
 * Call ldd_transport_set_mock_source(NULL) to uninstall.
 * ------------------------------------------------------------------------- */

/* State for the sequence mock. */
static const ldd_raw_event_t *g_seq_events  = NULL;
static int                    g_seq_count   = 0;
static int                    g_seq_pos     = 0;

static int sequence_mock_read(ldd_raw_event_t *out)
{
    if (g_seq_pos >= g_seq_count) {
        return 1; /* end of sequence */
    }
    *out = g_seq_events[g_seq_pos];
    g_seq_pos++;
    return 0;
}

/*
 * ldd_mock_sequence_install() — replay events[] in order.
 *
 * count: number of elements in events[].
 * events: pointer to array of pre-built ldd_raw_event_t; caller owns it.
 *
 * After installing, each call to ldd_transport_read() returns the next
 * element. Returns 1 once all elements are consumed.
 */
void ldd_mock_sequence_install(const ldd_raw_event_t *events, int count)
{
    g_seq_events = events;
    g_seq_count  = count;
    g_seq_pos    = 0;
    ldd_transport_set_mock_source(sequence_mock_read);
}

/*
 * ldd_mock_sequence_pos() — return how many events have been consumed.
 * Useful for assertions in tests.
 */
int ldd_mock_sequence_pos(void)
{
    return g_seq_pos;
}
