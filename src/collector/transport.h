/*
 * src/collector/transport.h
 *
 * Event transport interface for the Live Deadlock Detector.
 *
 * Abstracts the event source behind a single polling function so the
 * collector is independent of the actual transport mechanism. In
 * production the source is the BCC Python tracer writing binary event
 * structs to a file descriptor. In tests the source is a mock installed
 * via ldd_transport_set_mock_source().
 *
 * Source of truth: docs/MODULE_INTERFACES.md §2.1
 * Ownership: Developer A (Person 2 — collector)
 */

#ifndef LDD_TRANSPORT_H
#define LDD_TRANSPORT_H

#include <stdint.h>

/*
 * ldd_raw_event_t — opaque buffer holding one raw event from the transport.
 *
 * buf must be at least as large as the largest event struct. The largest
 * kernel-emitted struct is ldd_lock_request_event at 40 bytes (Linux x86_64).
 * 64 bytes provides headroom for future events without changing this type.
 *
 * len is set by the transport to the number of valid bytes in buf.
 * Bytes buf[len..63] are undefined and must not be read by the collector.
 */
typedef struct {
    uint8_t  buf[64];   /* raw event bytes; >= largest event struct (40 B) */
    uint16_t len;       /* number of valid bytes written into buf           */
} ldd_raw_event_t;

/*
 * ldd_transport_read() — read one event from the active transport source.
 *
 * Fills *out with the next available event.
 *
 * Returns:
 *   0   event available and written to *out
 *   1   no event currently available (non-blocking transports; not an error)
 *  -1   transport error (logged by transport layer; caller continues)
 *
 * Thread safety: single-threaded consumer assumed in v1.
 * out must not be NULL.
 */
int ldd_transport_read(ldd_raw_event_t *out);

/*
 * ldd_transport_lost_count() — return events lost since last call.
 *
 * When the BPF ring/perf buffer overflows the kernel drops events and
 * reports a loss count. This function returns the cumulative count since
 * the last call and resets the internal counter to zero.
 *
 * A non-zero return causes the collector's drain loop to inject a
 * synthetic EVENT_LOST_EVENTS notification into the processing pipeline.
 *
 * Returns 0 when the mock source is active (mocks do not simulate loss
 * unless the mock implementation explicitly sets it via the internal API).
 */
uint64_t ldd_transport_lost_count(void);

/*
 * ldd_transport_set_mock_source() — install a mock event source.
 *
 * When set, ldd_transport_read() delegates to mock_read instead of the
 * real transport. Pass NULL to restore the default (no-op) transport.
 *
 * mock_read follows the same return-value contract as ldd_transport_read:
 *   0   event written to *out
 *   1   no more events (end of mock sequence)
 *  -1   mock transport error
 *
 * Used exclusively for unit and integration tests. Must not be called
 * after real BCC/eBPF transport is initialised.
 */
void ldd_transport_set_mock_source(int (*mock_read)(ldd_raw_event_t *));

#endif /* LDD_TRANSPORT_H */
