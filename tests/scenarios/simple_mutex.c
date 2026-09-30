/*
 * tests/scenarios/simple_mutex.c
 *
 * Smoke-test target program for Phase G live eBPF tracing.
 *
 * Creates observable pthread_mutex_lock / unlock activity so the tracer
 * can be verified against a known sequence.  Does NOT deadlock.
 *
 * Build on Ubuntu:
 *   gcc -std=c11 -Wall -pthread tests/scenarios/simple_mutex.c \
 *       -o build/simple_mutex
 *
 * Run (in one terminal):
 *   ./build/simple_mutex
 *
 * Trace (in another terminal, as root):
 *   sudo python3 src/tracer/tracer.py --verbose $(pgrep simple_mutex)
 *
 * Expected events per iteration (per thread):
 *   LOCK_REQUEST -> LOCK_ACQUIRED -> LOCK_RELEASED
 *   (two threads, so 6 events per loop cycle on average)
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

#define NUM_THREADS   2
#define NUM_ITERS     20
#define SLEEP_USEC    50000   /* 50 ms between iterations */

static pthread_mutex_t g_mutex_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_mutex_b = PTHREAD_MUTEX_INITIALIZER;
static volatile int    g_counter = 0;

static void *worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    int i;

    for (i = 0; i < NUM_ITERS; i++) {
        /* Alternate lock order to produce interesting event patterns
         * without creating a deadlock (both threads take A before B). */
        pthread_mutex_lock(&g_mutex_a);
        pthread_mutex_lock(&g_mutex_b);

        g_counter++;
        printf("[thread %d] iter %d  counter=%d\n", id, i, g_counter);
        fflush(stdout);

        pthread_mutex_unlock(&g_mutex_b);
        pthread_mutex_unlock(&g_mutex_a);

        nanosleep(&(struct timespec){0, SLEEP_USEC * 1000L}, NULL);
    }

    printf("[thread %d] done\n", id);
    fflush(stdout);
    return NULL;
}

int main(void)
{
    pthread_t threads[NUM_THREADS];
    int i;

    printf("simple_mutex smoke test — PID %d\n", (int)getpid());
    printf("Locks: mutex_a @ %p  mutex_b @ %p\n",
           (void *)&g_mutex_a, (void *)&g_mutex_b);
    fflush(stdout);

    for (i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, worker, (void *)(intptr_t)i) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("All threads done. Final counter = %d\n", g_counter);
    pthread_mutex_destroy(&g_mutex_a);
    pthread_mutex_destroy(&g_mutex_b);
    return 0;
}
