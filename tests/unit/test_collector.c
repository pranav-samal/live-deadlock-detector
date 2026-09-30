/*
 * tests/unit/test_collector.c
 *
 * Phase B unit tests for the transport interface and event collector.
 *
 * Build (Linux / WSL — authoritative target):
 *   gcc -std=c11 -Wall -Wextra -I../../src \
 *       test_collector.c \
 *       ../../src/collector/transport_mock.c \
 *       ../../src/collector/collector.c \
 *       -o test_collector && ./test_collector
 *
 * Build (MinGW on Windows — compile check only; do not rely on sizes):
 *   gcc -std=c11 -Wall -Wextra -I../../src \
 *       test_collector.c \
 *       ../../src/collector/transport_mock.c \
 *       ../../src/collector/collector.c \
 *       -o test_collector.exe && test_collector.exe
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Reach into the module source tree via -I../../src */
#include "collector/transport.h"
#include "collector/collector.h"
#include "common/events.h"

/* -------------------------------------------------------------------------
 * Minimal test framework
 * ------------------------------------------------------------------------- */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                           \
    do {                                                            \
        if (cond) {                                                 \
            printf("  PASS  %s\n", name);                          \
            g_pass++;                                               \
        } else {                                                    \
            printf("  FAIL  %s  (line %d)\n", name, __LINE__);     \
            g_fail++;                                               \
        }                                                           \
    } while (0)

/* Test-only function declared here (defined in transport_mock.c). */
void ldd_mock_set_lost_count(uint64_t n);
void ldd_mock_sequence_install(const ldd_raw_event_t *events, int count);
int  ldd_mock_sequence_pos(void);

/* -------------------------------------------------------------------------
 * Helpers to build valid raw events
 * ------------------------------------------------------------------------- */

/*
 * fill_raw_lock_request() — build a minimal valid LOCK_REQUEST raw event.
 * We copy struct bytes directly so the test is layout-accurate on the
 * current platform. Tests that check actual field values should be run
 * on Linux.
 */
static void fill_raw_lock_request(ldd_raw_event_t *raw,
                                   uint32_t pid, uint32_t tid,
                                   uint64_t lock_addr)
{
    struct ldd_lock_request_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.hdr.schema_version = LDD_SCHEMA_VERSION;
    ev.hdr.event_type     = EVENT_LOCK_REQUEST;
    ev.hdr.pid            = pid;
    ev.hdr.tid            = tid;
    ev.hdr.timestamp_ns   = 1000000ULL;
    ev.hdr.lock_addr      = lock_addr;
    ev.is_trylock         = 0;
    ev.sched_policy       = LDD_SCHED_UNAVAILABLE;
    ev.sched_priority     = LDD_PRIO_UNAVAILABLE;

    memset(raw, 0, sizeof(*raw));
    memcpy(raw->buf, &ev, sizeof(ev));
    raw->len = (uint16_t)sizeof(ev);
}

static void fill_raw_lock_acquired(ldd_raw_event_t *raw,
                                    uint32_t pid, uint32_t tid,
                                    uint64_t lock_addr)
{
    struct ldd_lock_acquired_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.hdr.schema_version = LDD_SCHEMA_VERSION;
    ev.hdr.event_type     = EVENT_LOCK_ACQUIRED;
    ev.hdr.pid            = pid;
    ev.hdr.tid            = tid;
    ev.hdr.lock_addr      = lock_addr;
    ev.sched_policy       = LDD_SCHED_UNAVAILABLE;
    ev.sched_priority     = LDD_PRIO_UNAVAILABLE;

    memset(raw, 0, sizeof(*raw));
    memcpy(raw->buf, &ev, sizeof(ev));
    raw->len = (uint16_t)sizeof(ev);
}

static void fill_raw_lock_released(ldd_raw_event_t *raw,
                                    uint32_t pid, uint32_t tid,
                                    uint64_t lock_addr)
{
    ldd_lock_released_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.schema_version = LDD_SCHEMA_VERSION;
    ev.event_type     = EVENT_LOCK_RELEASED;
    ev.pid            = pid;
    ev.tid            = tid;
    ev.lock_addr      = lock_addr;

    memset(raw, 0, sizeof(*raw));
    memcpy(raw->buf, &ev, sizeof(ev));
    raw->len = (uint16_t)sizeof(ev);
}

static void fill_raw_lost_events(ldd_raw_event_t *raw, uint64_t lost_count)
{
    struct ldd_lost_events ev;
    memset(&ev, 0, sizeof(ev));
    ev.schema_version = LDD_SCHEMA_VERSION;
    ev.event_type     = EVENT_LOST_EVENTS;
    ev.lost_count     = lost_count;
    ev.timestamp_ns   = 0;

    memset(raw, 0, sizeof(*raw));
    memcpy(raw->buf, &ev, sizeof(ev));
    raw->len = (uint16_t)sizeof(ev);
}

/* -------------------------------------------------------------------------
 * T1: Transport — no mock installed returns 1 (no event)
 * ------------------------------------------------------------------------- */
static void test_transport_no_mock(void)
{
    printf("\nT1: transport — no mock installed\n");
    ldd_transport_set_mock_source(NULL);

    ldd_raw_event_t raw;
    int rc = ldd_transport_read(&raw);
    CHECK(rc == 1, "returns 1 when no mock and no real transport");
}

/* -------------------------------------------------------------------------
 * T2: Transport — mock delivers one valid event
 * ------------------------------------------------------------------------- */
static void test_transport_mock_delivers_event(void)
{
    printf("\nT2: transport — mock delivers one event\n");

    ldd_raw_event_t events[1];
    fill_raw_lock_request(&events[0], 1000, 1001, 0xAAAABBBBUL);
    ldd_mock_sequence_install(events, 1);

    ldd_raw_event_t out;
    int rc = ldd_transport_read(&out);
    CHECK(rc == 0, "first read returns 0 (event available)");
    CHECK(out.len == events[0].len, "len matches");
    CHECK(out.buf[1] == EVENT_LOCK_REQUEST, "event_type byte correct");

    rc = ldd_transport_read(&out);
    CHECK(rc == 1, "second read returns 1 (end of sequence)");

    ldd_transport_set_mock_source(NULL);
}

/* -------------------------------------------------------------------------
 * T3: Transport — NULL out pointer returns -1
 * ------------------------------------------------------------------------- */
static void test_transport_null_out(void)
{
    printf("\nT3: transport — NULL out pointer\n");
    ldd_transport_set_mock_source(NULL);
    int rc = ldd_transport_read(NULL);
    CHECK(rc == -1, "returns -1 for NULL out pointer");
}

/* -------------------------------------------------------------------------
 * T4: Collector parse — valid LOCK_REQUEST
 * ------------------------------------------------------------------------- */
static void test_parse_lock_request(void)
{
    printf("\nT4: collector parse — valid LOCK_REQUEST\n");

    ldd_raw_event_t raw;
    fill_raw_lock_request(&raw, 1000, 1001, 0xDEADBEEFUL);

    ldd_typed_event_t typed;
    int rc = ldd_collector_parse(&raw, &typed);
    CHECK(rc == EVENT_LOCK_REQUEST, "parse returns EVENT_LOCK_REQUEST");
    CHECK(typed.lock_request.hdr.event_type == EVENT_LOCK_REQUEST, "event_type field correct");
    CHECK(typed.lock_request.hdr.pid == 1000, "pid correct");
    CHECK(typed.lock_request.hdr.tid == 1001, "tid correct");
    CHECK(typed.lock_request.hdr.lock_addr == 0xDEADBEEFUL, "lock_addr correct");
    CHECK(typed.lock_request.is_trylock == 0, "is_trylock=0 (blocking)");
}

/* -------------------------------------------------------------------------
 * T5: Collector parse — valid LOCK_ACQUIRED
 * ------------------------------------------------------------------------- */
static void test_parse_lock_acquired(void)
{
    printf("\nT5: collector parse — valid LOCK_ACQUIRED\n");

    ldd_raw_event_t raw;
    fill_raw_lock_acquired(&raw, 1000, 1001, 0xDEADBEEFUL);

    ldd_typed_event_t typed;
    int rc = ldd_collector_parse(&raw, &typed);
    CHECK(rc == EVENT_LOCK_ACQUIRED, "parse returns EVENT_LOCK_ACQUIRED");
    CHECK(typed.lock_acquired.hdr.lock_addr == 0xDEADBEEFUL, "lock_addr correct");
}

/* -------------------------------------------------------------------------
 * T6: Collector parse — valid LOCK_RELEASED
 * ------------------------------------------------------------------------- */
static void test_parse_lock_released(void)
{
    printf("\nT6: collector parse — valid LOCK_RELEASED\n");

    ldd_raw_event_t raw;
    fill_raw_lock_released(&raw, 1000, 1001, 0xDEADBEEFUL);

    ldd_typed_event_t typed;
    int rc = ldd_collector_parse(&raw, &typed);
    CHECK(rc == EVENT_LOCK_RELEASED, "parse returns EVENT_LOCK_RELEASED");
    CHECK(typed.lock_released.tid == 1001, "tid correct");
}

/* -------------------------------------------------------------------------
 * T7: Collector parse — schema version mismatch returns -1
 * ------------------------------------------------------------------------- */
static void test_parse_schema_mismatch(void)
{
    printf("\nT7: collector parse — schema version mismatch\n");

    ldd_raw_event_t raw;
    fill_raw_lock_request(&raw, 1000, 1001, 0xAAAAUL);
    raw.buf[0] = 99; /* corrupt schema_version */

    ldd_typed_event_t typed;
    int rc = ldd_collector_parse(&raw, &typed);
    CHECK(rc == -1, "parse returns -1 on schema mismatch");
}

/* -------------------------------------------------------------------------
 * T8: Collector parse — truncated event returns 0 (parse failure)
 * ------------------------------------------------------------------------- */
static void test_parse_truncated(void)
{
    printf("\nT8: collector parse — truncated event\n");

    ldd_raw_event_t raw;
    fill_raw_lock_request(&raw, 1000, 1001, 0xAAAAUL);
    raw.len = 4; /* shorter than any valid event */

    ldd_typed_event_t typed;
    int rc = ldd_collector_parse(&raw, &typed);
    CHECK(rc == 0, "parse returns 0 on truncated event");
}

/* -------------------------------------------------------------------------
 * T9: Collector parse — zero-length event returns 0
 * ------------------------------------------------------------------------- */
static void test_parse_zero_length(void)
{
    printf("\nT9: collector parse — zero-length event\n");

    ldd_raw_event_t raw;
    memset(&raw, 0, sizeof(raw));
    raw.len = 0;

    ldd_typed_event_t typed;
    int rc = ldd_collector_parse(&raw, &typed);
    CHECK(rc == 0, "parse returns 0 for zero-length event");
}

/* -------------------------------------------------------------------------
 * T10: Collector parse — unknown event type returns 0
 * ------------------------------------------------------------------------- */
static void test_parse_unknown_type(void)
{
    printf("\nT10: collector parse — unknown event type\n");

    ldd_raw_event_t raw;
    fill_raw_lock_request(&raw, 1000, 1001, 0xAAAAUL);
    raw.buf[1] = 99; /* unknown event type */

    ldd_typed_event_t typed;
    int rc = ldd_collector_parse(&raw, &typed);
    CHECK(rc == 0, "parse returns 0 for unknown event type");
}

/* -------------------------------------------------------------------------
 * T11: Collector process — valid event returns 0
 * ------------------------------------------------------------------------- */
static void test_process_valid_event(void)
{
    printf("\nT11: collector process — valid event stub returns 0\n");

    ldd_raw_event_t raw;
    fill_raw_lock_request(&raw, 1000, 1001, 0xAAAAUL);

    ldd_typed_event_t typed;
    ldd_collector_parse(&raw, &typed);

    int rc = ldd_collector_process(&typed);
    CHECK(rc == 0, "process returns 0 for known event type");
}

/* -------------------------------------------------------------------------
 * T12: Collector process — NULL pointer returns -1
 * ------------------------------------------------------------------------- */
static void test_process_null(void)
{
    printf("\nT12: collector process — NULL pointer\n");
    int rc = ldd_collector_process(NULL);
    CHECK(rc == -1, "process returns -1 for NULL event");
}

/* -------------------------------------------------------------------------
 * T13: Collector drain — processes a sequence and handles end-of-stream
 * ------------------------------------------------------------------------- */
static void test_drain_sequence(void)
{
    printf("\nT13: collector drain — processes 3-event sequence\n");

    ldd_raw_event_t events[3];
    fill_raw_lock_request (&events[0], 1000, 1001, 0xAAAAUL);
    fill_raw_lock_acquired(&events[1], 1000, 1001, 0xAAAAUL);
    fill_raw_lock_released(&events[2], 1000, 1001, 0xAAAAUL);
    ldd_mock_sequence_install(events, 3);

    int n = ldd_collector_drain();
    CHECK(n == 3, "drain processed 3 events");
    CHECK(ldd_mock_sequence_pos() == 3, "all 3 events consumed from mock");

    ldd_transport_set_mock_source(NULL);
}

/* -------------------------------------------------------------------------
 * T14: Collector drain — injects LOST_EVENTS when lost_count > 0
 * ------------------------------------------------------------------------- */
static void test_drain_injects_lost_events(void)
{
    printf("\nT14: collector drain — injects EVENT_LOST_EVENTS on loss\n");

    /* Single valid event after a simulated loss. */
    ldd_raw_event_t events[1];
    fill_raw_lock_request(&events[0], 1000, 1001, 0xAAAAUL);
    ldd_mock_sequence_install(events, 1);
    ldd_mock_set_lost_count(7);

    int n = ldd_collector_drain();
    /* Drain returns count of processed events excluding the synthetic one. */
    CHECK(n == 1, "drain processed 1 real event");
    CHECK(ldd_transport_lost_count() == 0, "lost count reset to 0 after drain");

    ldd_transport_set_mock_source(NULL);
}

/* -------------------------------------------------------------------------
 * T15: Collector parse — valid EVENT_LOST_EVENTS
 * ------------------------------------------------------------------------- */
static void test_parse_lost_events(void)
{
    printf("\nT15: collector parse — valid EVENT_LOST_EVENTS\n");

    ldd_raw_event_t raw;
    fill_raw_lost_events(&raw, 42);

    ldd_typed_event_t typed;
    int rc = ldd_collector_parse(&raw, &typed);
    CHECK(rc == EVENT_LOST_EVENTS, "parse returns EVENT_LOST_EVENTS");
    CHECK(typed.lost_events.lost_count == 42, "lost_count correct");
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== Phase B unit tests: transport + collector ===\n");

    test_transport_no_mock();
    test_transport_mock_delivers_event();
    test_transport_null_out();
    test_parse_lock_request();
    test_parse_lock_acquired();
    test_parse_lock_released();
    test_parse_schema_mismatch();
    test_parse_truncated();
    test_parse_zero_length();
    test_parse_unknown_type();
    test_process_valid_event();
    test_process_null();
    test_drain_sequence();
    test_drain_injects_lost_events();
    test_parse_lost_events();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
