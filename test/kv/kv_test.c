/*
 * USE THIS WAY: env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/local/bin make run-pinning
 * kv_test.c — KV-cache policy test (bypass and/or LLC way pinning)
 *
 * Usage:  ./kv_test [--bypass | --pinning | --bypass-pinning]
 *   (no arg)          baseline        no KV policy active
 *   --bypass          bypass only     L1/L2 bypass, no LLC pinning
 *   --pinning         pinning only    LLC way reservation, no L1/L2 bypass
 *   --bypass-pinning  full policy     L1/L2 bypass + LLC way reservation
 *
 * SimMarker convention (magic_server.cc):
 *   0xBEEF0001/0002 -> enableKVCachePolicy()  (bypass + pinning)
 *   0xBEEF0003/0004 -> enableKVPinningOnly()  (pinning, no bypass)
 *   bypass-only is achieved by sending 0xBEEF0001/0002 while overriding
 *   kv_cache_reserved_ways=0 via the run-sniper -c flag (see Makefile).
 *
 * What to observe:
 *   bypass modes  : L1-D / L2  kv-bypass-count  > 0
 *                   L1-D / L2  load-misses rise  (KV traffic skips them)
 *                   LLC        load-misses fall   (KV served from LLC)
 *   pinning modes : LLC access-mru-0..3 higher   (KV lines fill way 0-3)
 *                   LLC miss rate lower on re-reads (KV not evicted by reg)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sim_api.h"

/* KV buffer: 2 MB — fits in 4 reserved LLC ways (4-way × 16 sets × 64 B = varies) */
#define KV_SIZE    (2 * 1024 * 1024)
/* Interference buffer: must exceed LLC_size - KV_size (16MB - 2MB = 14MB)
 * so the working set (22 MB) thrashes the 16 MB LLC, creating the eviction
 * pressure needed to demonstrate both bypass and pinning benefits. */
#define REG_SIZE   (20 * 1024 * 1024)
/* Passes over each buffer inside the ROI */
#define N_ITERS    8
/* Cache-line stride */
#define STRIDE     64

int main(int argc, char *argv[])
{
    const char *mode = (argc > 1) ? argv[1] : "";

    int do_bypass     = (strcmp(mode, "--bypass")         == 0 ||
                         strcmp(mode, "--bypass-pinning")  == 0);
    int do_pinning    = (strcmp(mode, "--pinning")        == 0 ||
                         strcmp(mode, "--bypass-pinning")  == 0);
    int pinning_only  =  strcmp(mode, "--pinning")        == 0;

    volatile char *kv_buf  = (volatile char *) malloc(KV_SIZE);
    volatile char *reg_buf = (volatile char *) malloc(REG_SIZE);
    if (!kv_buf || !reg_buf) { fprintf(stderr, "malloc failed\n"); return 1; }

    /* Touch pages before ROI so page-fault latency is excluded */
    memset((void *)kv_buf,  0xAA, KV_SIZE);
    memset((void *)reg_buf, 0xBB, REG_SIZE);

    /* ------------------------------------------------------------------
     * Announce the KV buffer range to the simulator.
     * Both marker pairs set the same start/size; they differ only in
     * which policy they activate inside magic_server.cc.
     * ------------------------------------------------------------------ */
    if (do_bypass || do_pinning) {
        if (pinning_only) {
            SimMarker(0xBEEF0003, (unsigned long)(uintptr_t)kv_buf);
            SimMarker(0xBEEF0004, (unsigned long)KV_SIZE);
        } else {
            SimMarker(0xBEEF0001, (unsigned long)(uintptr_t)kv_buf);
            SimMarker(0xBEEF0002, (unsigned long)KV_SIZE);
        }
    }

    SimRoiStart();

    volatile long sum = 0;

    for (int iter = 0; iter < N_ITERS; iter++) {
        /* KV buffer: bypasses L1/L2 when bypass on; stays pinned in LLC */
        for (int i = 0; i < KV_SIZE; i += STRIDE)
            sum += kv_buf[i];

        /* Interference buffer: thrashes regular LLC ways; should NOT
         * evict KV lines when pinning is active */
        for (int i = 0; i < REG_SIZE; i += STRIDE)
            sum += reg_buf[i];
    }

    SimRoiEnd();

    printf("sum = %ld\n", sum);
    free((void *)kv_buf);
    free((void *)reg_buf);
    return 0;
}
