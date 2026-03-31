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

### Must run without conda
The conda environment sets library paths that conflict with SDE. The wrapper script
handles this automatically via `env -i`. If running `run-sniper` directly, prefix
your command with:
```bash
env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/local/bin SNIPER_ROOT=<path> bash -c "..."
```
