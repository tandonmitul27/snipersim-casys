/*
 * spm_test.c — Scratchpad memory test
 *
 * Tests both private (per-core) and shared SPM modes.
 * The mode is selected at config time (perf_model/scratchpad/shared),
 * NOT by this program — the SimMarker protocol is identical for both.
 *
 * Usage:
 *   ./spm_test              (single-core: exercises SPM init, map, DMA, access)
 *   mpirun -n 4 ./spm_test  (multi-core: all cores access the same SPM in shared mode)
 *
 * What to observe in stats:
 *   SPM enabled:   scratchpad[N].reads > 0, L1-D loads lower (KV reads skip cache)
 *   SPM disabled:  scratchpad[N].reads = 0, all accesses go through cache hierarchy
 *   Shared mode:   scratchpad[N].contention-stalls > 0 (with 1 port, multi-core)
 *   Private mode:  scratchpad[N].contention-stalls = 0
 *
 * SimMarker IDs (0x5FAD prefix):
 *   0x5FAD0001 = SPM_INIT       (arg = size in bytes)
 *   0x5FAD0002 = SPM_MAP        (arg = mmap'd virtual address)
 *   0x5FAD0010 = DMA_LOAD_ADDR  (arg = DRAM source vaddr)
 *   0x5FAD0011 = DMA_LOAD_EXEC  (arg = (spm_offset << 32) | transfer_size)
 *   0x5FAD0020 = DMA_STORE_ADDR (arg = DRAM dest vaddr)
 *   0x5FAD0021 = DMA_STORE_EXEC (arg = (spm_offset << 32) | transfer_size)
 *   0x5FAD00FF = SPM_FREE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include "sim_api.h"

/* SPM region size: 2 MB */
#define SPM_SIZE   (2 * 1024 * 1024)

/* DRAM buffer that we DMA into the SPM */
#define KV_SIZE    (2 * 1024 * 1024)

/* Interference buffer to show SPM accesses DON'T go through cache */
#define REG_SIZE   (20 * 1024 * 1024)

/* Iterations inside ROI */
#define N_ITERS    8

/* Cache-line stride */
#define STRIDE     64

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* ── 1. Allocate DRAM buffers ──────────────────────────────────── */
    volatile char *kv_buf  = (volatile char *)malloc(KV_SIZE);
    volatile char *reg_buf = (volatile char *)malloc(REG_SIZE);
    if (!kv_buf || !reg_buf) { fprintf(stderr, "malloc failed\n"); return 1; }

    /* Fill with known patterns before ROI */
    memset((void *)kv_buf,  0xAA, KV_SIZE);
    memset((void *)reg_buf, 0xBB, REG_SIZE);

    /* ── 2. Reserve a virtual address range for SPM-mapped accesses ─ */
    void *spm_region = mmap(NULL, SPM_SIZE, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (spm_region == MAP_FAILED) {
        fprintf(stderr, "mmap failed\n");
        return 1;
    }
    /* Touch the pages so they're faulted in before simulation */
    memset(spm_region, 0, SPM_SIZE);

    /* ── 3. Initialize SPM via SimMarkers ──────────────────────────── */
    /* SPM_INIT: tell simulator to allocate SPM of SPM_SIZE bytes */
    SimMarker(0x5FAD0001, (unsigned long)SPM_SIZE);

    /* SPM_MAP: map SPM to this mmap'd virtual address range */
    SimMarker(0x5FAD0002, (unsigned long)(uintptr_t)spm_region);

    /* ── 4. DMA load: copy KV data from DRAM into SPM ──────────────── */
    /* DMA_LOAD_ADDR: source = kv_buf in DRAM */
    SimMarker(0x5FAD0010, (unsigned long)(uintptr_t)kv_buf);

    /* DMA_LOAD_EXEC: dest = SPM offset 0, size = KV_SIZE
     * packed argument: (spm_offset << 32) | transfer_size */
    SimMarker(0x5FAD0011, (0UL << 32) | (unsigned long)KV_SIZE);

    /* ── 5. ROI: read from SPM region (fixed-latency) + interference ─ */
    SimRoiStart();

    volatile long sum = 0;

    for (int iter = 0; iter < N_ITERS; iter++) {
        /* SPM reads: these accesses hit the mapped SPM region.
         * With SPM enabled, they bypass the entire cache hierarchy
         * and are served at fixed latency (e.g. 2 cycles). */
        for (int i = 0; i < KV_SIZE; i += STRIDE)
            sum += ((volatile char *)spm_region)[i];

        /* Interference: regular cache traffic to stress the hierarchy.
         * This shows that SPM accesses are unaffected by cache pressure. */
        for (int i = 0; i < REG_SIZE; i += STRIDE)
            sum += reg_buf[i];
    }

    SimRoiEnd();

    /* ── 6. DMA store: write SPM data back to DRAM ─────────────────── */
    /* DMA_STORE_ADDR: destination = kv_buf in DRAM */
    SimMarker(0x5FAD0020, (unsigned long)(uintptr_t)kv_buf);

    /* DMA_STORE_EXEC: source = SPM offset 0, size = KV_SIZE */
    SimMarker(0x5FAD0021, (0UL << 32) | (unsigned long)KV_SIZE);

    /* ── 7. Cleanup ────────────────────────────────────────────────── */
    SimMarker(0x5FAD00FF, 0);

    printf("sum = %ld\n", sum);

    munmap(spm_region, SPM_SIZE);
    free((void *)kv_buf);
    free((void *)reg_buf);
    return 0;
}
