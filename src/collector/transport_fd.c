/*
 * src/collector/transport_fd.c
 *
 * File-descriptor transport implementation for the Live Deadlock Detector.
 *
 * Replaces transport_mock.c in production.  The BCC Python tracer writes
 * raw binary event structs (framed with a 2-byte length prefix) to its
 * stdout.  This module reads from a caller-supplied file descriptor —
 * typically stdin when the collector is started as:
 *
 *   sudo python3 src/tracer/tracer.py <pid> | ./ldd_collector
 *
 * Frame format (written by tracer.py):
 *   [uint16_t len_le][uint8_t payload[len]]
 *   len is little-endian.  Maximum payload is 64 bytes (ldd_raw_event_t.buf).
 *
 * Compile into the production collector binary instead of transport_mock.c:
 *   gcc ... src/collector/transport_fd.c src/collector/collector.c \
 *           src/state/lock_state.c src/graph/graph.c -o ldd_collector
 *
 * The mock transport (transport_mock.c) remains available for all unit and
 * integration tests; this file is never included in the test builds.
 *
 * Ownership: Developer A (Person 1 / Person 2)
 */

#include "transport.h"

#include <stddef.h>    /* NULL         */
#include <stdint.h>
#include <string.h>    /* memset       */
#include <unistd.h>    /* read         */
#include <errno.h>     /* EINTR, EAGAIN */
#include <stdio.h>     /* fprintf      */

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

static int      g_fd         = -1;     /* source file descriptor */
static uint64_t g_lost_count =  0;     /* cumulative perf-buffer loss count */

/* -------------------------------------------------------------------------
 * Initialise the fd transport.
 * Call once before ldd_transport_read() is used in production.
 * fd is typically STDIN_FILENO (0).
 * ------------------------------------------------------------------------- */

void ldd_transport_fd_init(int fd)
{
    g_fd         = fd;
    g_lost_count = 0;
}

/* -------------------------------------------------------------------------
 * ldd_transport_fd_add_lost() — called by the tracer glue when the Python
 * layer reports a perf-buffer loss count.  The next call to
 * ldd_transport_lost_count() will return the accumulated total.
 * ------------------------------------------------------------------------- */

void ldd_transport_fd_add_lost(uint64_t n)
{
    g_lost_count += n;
}

/* -------------------------------------------------------------------------
 * Helper: read exactly n bytes from g_fd, retrying on EINTR.
 * Returns n on success, 0 on EOF, -1 on error.
 * ------------------------------------------------------------------------- */

static int read_exact(void *buf, int n)
{
    int total = 0;
    while (total < n) {
        int r = (int)read(g_fd, (char *)buf + total, (size_t)(n - total));
        if (r == 0) return 0;         /* EOF */
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += r;
    }
    return n;
}

/* -------------------------------------------------------------------------
 * Public transport interface (declared in transport.h)
 * ------------------------------------------------------------------------- */

int ldd_transport_read(ldd_raw_event_t *out)
{
    uint16_t len_le;
    int rc;

    if (out == NULL) return -1;
    if (g_fd < 0)   return 1;    /* not initialised — no events available */

    /* Read the 2-byte length prefix. */
    rc = read_exact(&len_le, sizeof(len_le));
    if (rc == 0) return 1;  /* EOF = no more events */
    if (rc < 0) {
        fprintf(stderr, "[transport_fd] read error on length prefix: errno=%d\n", errno);
        return -1;
    }

    /* len_le is little-endian; on x86-64 Linux no conversion needed. */
    uint16_t payload_len = len_le;

    if (payload_len == 0 || payload_len > sizeof(out->buf)) {
        fprintf(stderr, "[transport_fd] invalid payload length %u (max %zu)\n",
                (unsigned)payload_len, sizeof(out->buf));
        return -1;
    }

    /* Read the payload. */
    rc = read_exact(out->buf, (int)payload_len);
    if (rc == 0) {
        fprintf(stderr, "[transport_fd] unexpected EOF mid-payload\n");
        return -1;
    }
    if (rc < 0) {
        fprintf(stderr, "[transport_fd] read error on payload: errno=%d\n", errno);
        return -1;
    }

    out->len = payload_len;
    return 0;
}

uint64_t ldd_transport_lost_count(void)
{
    uint64_t count = g_lost_count;
    g_lost_count   = 0;
    return count;
}

void ldd_transport_set_mock_source(int (*mock_read)(ldd_raw_event_t *))
{
    /*
     * In production builds using transport_fd.c, mock source installation
     * is not supported.  This function exists to satisfy the interface
     * declaration and logs a warning if called.
     */
    if (mock_read != NULL) {
        fprintf(stderr, "[transport_fd] WARNING: ldd_transport_set_mock_source "
                "called in production transport — ignored\n");
    }
}
