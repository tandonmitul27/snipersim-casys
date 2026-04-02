# Transformer Benchmark on SniperSim

This document covers the additions made to the original SniperSim repo to run
transformer decode workloads using the `transformer-benchmark` folder.

---

## What Was Added

### 1. `transformer-benchmark/build/`
CMake build directory for the transformer benchmark, compiled **without AVX/AVX2**
for compatibility with SDE on glibc 2.34+. The binary is at:
```
transformer-benchmark/build/bin/bench-transformer
```

To rebuild from scratch:
```bash
cd transformer-benchmark
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DGGML_NATIVE=OFF -DGGML_AVX=OFF -DGGML_AVX2=OFF \
  -DGGML_AVX512=OFF -DGGML_FMA=OFF -DGGML_F16C=OFF
cmake --build build --target bench-transformer -j$(nproc)
```

---

### 2. `config/skylake.cfg`
A new SniperSim processor config modelling an **Intel Xeon Gold 6154 (Skylake-SP)**,
replacing the default Gainestown (Nehalem, 2009) config.

| Parameter | Gainestown | Skylake |
|-----------|-----------|---------|
| Frequency | 2.66 GHz | 3.0 GHz |
| Cores | 4 | 18 |
| Core model | Interval, 128-entry ROB | ROB, 224-entry ROB |
| L1 cache | 32 KB, 4-way | 32 KB, 8-way |
| L2 cache | 256 KB, 8-way | 1 MB, 16-way |
| L3 cache | 8 MB, 16-way, shared/4 | 16 MB, 16-way, shared/18 |
| DRAM | DDR3, 45 ns, 7.6 GB/s | DDR4-2666, 60 ns, 21.3 GB/s |
| Technology | 45 nm | 14 nm |

Config constraints satisfied:
- All cache sizes are powers of 2 (required by `address_hash = mask`)
- `controllers_interleaving` (18) is a multiple of `l3_cache/shared_cores` (18)

---

### 3. `transformer-benchmark/bench-transformer/configs/`
Four synthetic model configs of increasing size, designed to stress different
cache levels of the Skylake config (L2=1 MB/core, L3=16 MB shared):

| Config | n_embd | n_head | n_ff | Est. weights (1 block) | Cache regime |
|--------|--------|--------|------|------------------------|--------------|
| `tiny.json` | 128 | 4 | 256 | ~0.7 MB | L2-resident |
| `small.json` | 256 | 8 | 512 | ~2.7 MB | L2→L3 boundary |
| `medium.json` | 512 | 8 | 1024 | ~12 MB | L3-resident |
| `large.json` | 768 | 12 | 2048 | ~33 MB | DRAM-bound |

All use GQA (grouped-query attention), RoPE positional encoding, and SwiGLU FFN
— matching the architecture of modern LLMs (LLaMA, Qwen, DeepSeek).

---

### 4. `run-transformer-bench.sh`
A wrapper script that handles the full simulation flow:
- Strips the conda/shell environment (fixes glibc 2.34+ SDE crash)
- Passes `--sde-arch=skl` for Skylake CPUID emulation
- Saves results to a unique timestamped folder under `results/`

---

## Running a Simulation

### Quick start
```bash
./run-transformer-bench.sh \
    --config tiny \
    --n-blocks 1 --n-iters 2 --n-threads 1 --seq-len 16 \
    --weight-type f32 --kv-type f32
```

### All options
```
./run-transformer-bench.sh [sniper options] --config <name> [bench options]

Sniper options (must come before --config):
  --sniper-cores <N>    number of simulated cores (default: 18)
  --sniper-cfg <name>   sniper config name (default: skylake)

--config <name>         model config name (without .json) from bench-transformer/configs/

Bench options (passed directly to bench-transformer):
  --n-blocks <N>        transformer blocks per decode step (default: 1)
  --n-iters <N>         tokens to generate (default: 100)
  --n-threads <N>       CPU threads — keep at 1 (see Known Issues)
  --seq-len <N>         KV cache capacity
  --weight-type <t>     f32 | f16 | q4_0 | q4_k
  --kv-type <t>         f32 | f16
  --prefill <N>         pre-fill N KV cache positions before decode
```

### Example runs

**Tiny model, Skylake, 18 cores:**
```bash
./run-transformer-bench.sh \
    --config tiny \
    --n-blocks 1 --n-iters 2 --n-threads 1 --seq-len 16 \
    --weight-type f32 --kv-type f32
```

**Small model, more iterations:**
```bash
./run-transformer-bench.sh \
    --config small \
    --n-blocks 1 --n-iters 4 --n-threads 1 --seq-len 32 \
    --weight-type f32 --kv-type f32
```

**Compare against Gainestown:**
```bash
./run-transformer-bench.sh \
    --sniper-cfg gainestown --sniper-cores 4 \
    --config tiny \
    --n-blocks 1 --n-iters 2 --n-threads 1 --seq-len 16 \
    --weight-type f32 --kv-type f32
```

**Medium model (L3-resident):**
```bash
./run-transformer-bench.sh \
    --config medium \
    --n-blocks 1 --n-iters 2 --n-threads 1 --seq-len 64 \
    --weight-type f32 --kv-type f32
```

---

## Simulation Output

Each run saves results to `results/<timestamp>_<config>_<Ncores>c/`:

```
results/
└── 20260331_142305_tiny_18c/
    ├── sim.out            # Human-readable stats (IPC, cache, DRAM, branch predictor)
    ├── sim.stats.sqlite3  # Full stats database (queryable)
    ├── sim.cfg            # Exact merged config used for this run
    └── sim.info           # Metadata: timestamp, command line, git revision
```

Key stats in `sim.out`:

| Stat | What it means |
|------|---------------|
| IPC | Instructions per cycle — higher is better |
| L1-D miss rate | % of data accesses that miss L1 |
| L2 miss rate | % of L1 misses that also miss L2 |
| L3 miss rate | % of L2 misses that also miss L3 — high = DRAM bound |
| DRAM avg latency | Average DRAM access time in ns |
| Branch misprediction rate | % of branches predicted wrong |

---

## Known Issues

### `--n-threads > 1` crashes SDE
Running the benchmark with multiple threads (`--n-threads N`) causes SDE to crash
with:
```
bash: symbol lookup error: /lib/x86_64-linux-gnu/libc.so.6: undefined symbol: __nptl_change_stack_perm, version GLIBC_PRIVATE
```
This is a known incompatibility between SDE/PIN's bundled libpthread and glibc 2.34+
(this system uses glibc 2.39). Always use `--n-threads 1`.

### SDE segfault at exit
```
[SIFT_RECORDER] Tool (or Pin) caused signal 11
```
SDE's recorder tool segfaults during cleanup after the app exits. This is a known
PIN bug and does **not** affect simulation results — all traces are already sent to
Sniper before this happens.

---

## KV Cache Bypass and LLC Way Pinning

Two cache management policies were added to SniperSim to model hardware-assisted
KV cache optimization for transformer inference workloads.

### Concept

During transformer decode, the KV cache is read every token but rarely written.
On a standard cache hierarchy, interference from weight/activation traffic can
evict KV cache lines, causing expensive DRAM re-fetches.

- **L1/L2 Bypass**: KV cache reads skip L1 and L2, going directly to the LLC.
  This prevents KV data from polluting the small inner caches that are more useful
  for the hot working set (weights, activations, loop variables).

- **LLC Way Pinning**: A configurable number of LLC ways are *reserved* for KV
  cache lines. Regular (non-KV) data can only use the remaining ways. This
  protects KV data from eviction by the much larger weight/activation traffic.

### How It Works

#### Compile-time flags (`common/Makefile.common`)
Both are enabled by default:
```makefile
ENABLE_KV_BYPASS  ?= 1    # -DENABLE_KV_BYPASS
ENABLE_KV_PINNING ?= 1    # -DENABLE_KV_PINNING
```
These guard the code paths but do NOT activate the policies. Activation happens
at runtime via SimMarker magic instructions.

#### Runtime activation (SimMarker protocol)
The application announces the KV cache buffer's address range using two pairs of
magic instructions:

| Marker pair | Calls | Effect |
|---|---|---|
| `SimMarker(0xBEEF0001, addr)` + `SimMarker(0xBEEF0002, size)` | `enableKVCachePolicy()` | Activates both bypass and pinning |
| `SimMarker(0xBEEF0003, addr)` + `SimMarker(0xBEEF0004, size)` | `enableKVPinningOnly()` | Activates pinning only (no bypass) |

To get bypass-only, use the `0xBEEF0001/0002` pair and set
`kv_cache_reserved_ways=0` in the config.

#### Config parameter (`config/skylake.cfg`)
```ini
[perf_model/l3_cache]
kv_cache_reserved_ways = 8   # ways reserved for KV lines (out of 16-way LLC)
```

#### Key files modified
| File | Changes |
|---|---|
| `common/system/magic_server.cc` | SimMarker handling; VA-to-PA translation of KV start address |
| `common/core/memory_subsystem/parametric_dram_directory_msi/cache_cntlr.cc` | Bypass logic in `processMemOpFromCore` and `processShmemReqFromPrevCache`; LLC miss handling with stack lock management |
| `common/core/memory_subsystem/parametric_dram_directory_msi/cache_cntlr.h` | `m_kv_bypass` flag, `shouldBypassForKV()`, bypass counter |
| `common/core/memory_subsystem/parametric_dram_directory_msi/memory_manager.cc` | `enableKVCachePolicy()`, `enableKVPinningOnly()`, `isKVCacheAddr()`, debug stats |
| `common/core/memory_subsystem/parametric_dram_directory_msi/memory_manager.h` | Static KV range tracking (`s_kv_cache_start`, `s_kv_cache_size`, `s_kv_policy_active`) |
| `common/core/memory_subsystem/cache/cache_set.h` | `m_num_kv_reserved_ways`, `getKVReplacementIndex()` |
| `common/core/memory_subsystem/cache/cache_set_lru.cc` | Way-partitioned LRU replacement: KV lines routed to reserved ways, regular lines restricted to non-reserved ways |
| `common/core/memory_subsystem/cache/cache_block_info.h` | `m_is_kv_cache_line` flag, `isKVCacheLine()` / `setKVCacheLine()` |

### Stats to check

| Stat | Meaning |
|---|---|
| `L1-D[0].kv-bypass-count` | Number of L1-D accesses that bypassed to the next level |
| `L2[0].kv-bypass-count` | Number of L2 accesses that bypassed to L3 |
| `L3[0].pinning-kv-insert-reserved` | KV lines inserted into reserved LLC ways |
| `L3[0].pinning-regular-insert-nonreserved` | Regular lines inserted into non-reserved LLC ways |
| `L3[0].access-mru-15` | Hits in way position 15 (reserved way) — high value confirms pinning |
| `kv-debug[0].kv-is-addr-hits` | Times `isKVCacheAddr()` returned true during LLC insertion |

---

## KV Cache Test (`test/kv/`)

A standalone microbenchmark that validates the bypass and pinning mechanisms in
isolation, without the complexity of the full transformer benchmark.

### Test design

`test/kv/kv_test.c` allocates two buffers:
- **KV buffer** (2 MB): simulates the KV cache, announced to the simulator via SimMarker
- **Interference buffer** (20 MB): simulates weight/activation traffic that competes for LLC space

The combined working set (22 MB) exceeds the 16 MB LLC, creating the eviction
pressure needed to demonstrate pinning benefits. Inside the ROI, the test
alternates 8 passes over both buffers at cache-line stride.

### Running the tests

```bash
cd test/kv
env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/local/bin make run-all
```

Individual targets:
```bash
make run-baseline         # no KV policy
make run-bypass           # L1/L2 bypass only (kv_cache_reserved_ways=0)
make run-pinning          # LLC way pinning only
make run-bypass-pinning   # both bypass and pinning
```

### Results (16 MB LLC, 16-way, 8 reserved ways for pinning)

| Metric | Baseline | Bypass | Pinning | Bypass+Pinning |
|---|---|---|---|---|
| Speedup vs baseline | 1.00x | 1.02x | 1.06x | 1.06x |
| L1-D kv-bypass-count | 0 | 262,136 | 0 | 262,136 |
| L3 load-misses | 2,883,823 | 2,883,824 | 2,392,305 | 2,392,305 |
| L3 KV inserts (reserved) | 0 | 0 | 32,767 | 32,767 |
| DRAM reads | 3,244,280 | 3,244,282 | 2,752,762 | 2,752,763 |

**Key observations:**
- **Bypass** reduces L1/L2 pollution (262K fewer L1-D evictions) but does not
  reduce DRAM traffic — without pinning, KV lines are still evicted from LLC by
  the 20 MB interference buffer.
- **Pinning** eliminates ~491K DRAM reads by protecting KV lines in reserved LLC
  ways. This is the dominant performance win (~6% speedup).
- **Bypass+Pinning** composes correctly: L2 bypass count drops from 524K to 295K
  because pinned KV lines now hit in LLC on the first try, avoiding the DRAM
  retry path.

---

### Must run without conda
The conda environment sets library paths that conflict with SDE. The wrapper script
handles this automatically via `env -i`. If running `run-sniper` directly, prefix
your command with:
```bash
env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/local/bin SNIPER_ROOT=<path> bash -c "..."
```
