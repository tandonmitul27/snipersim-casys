#!/usr/bin/env bash
# Sweep bench-transformer over guest --n-threads = {1,2,4,8,16,32}
# Each run goes through run-sniper with --power against the xeon_gold cfg.
# All six runs are launched in parallel; each is capped to 8 host cores
# (general/num_host_cores=8) so they don't thrash on the 64-thread host.

set -u
cd "$(dirname "$0")"
SNIPER_ROOT="$(pwd)"
export SNIPER_ROOT

THREADS_LIST=(1 2 4 8 16 32)
SWEEP_DIR="sweep_results_$(date +%Y%m%d_%H%M%S)"
LOG_DIR="$SWEEP_DIR/logs"
mkdir -p "$LOG_DIR"

BENCH=./transformer-benchmark/build/bin/bench-transformer
CONFIG=./transformer-benchmark/bench/bench-transformer/configs/qwen2_7b.json
N_BLOCKS=1
N_ITERS=1
SEQ_LEN=32        # >= n_iters * batch_size = 1 * 8 = 8
BATCH_SIZE=8      # prefill (GEMM)
HOST_CORES_PER_JOB=8

echo "[sweep] dir=$SWEEP_DIR  threads={${THREADS_LIST[*]}}  host_cores_per_job=$HOST_CORES_PER_JOB" \
    | tee "$SWEEP_DIR/sweep.info"
echo "[sweep] start: $(date -Is)" | tee -a "$SWEEP_DIR/sweep.info"

run_one() {
    local n="$1"
    local outdir="$SWEEP_DIR/threads_${n}"
    local log="$LOG_DIR/threads_${n}.log"
    mkdir -p "$outdir"
    {
        echo "[t=$n] start: $(date -Is)"
        setarch "$(uname -m)" -R ./run-sniper \
            -n 32 \
            -c xeon_gold \
            -g --general/num_host_cores=$HOST_CORES_PER_JOB \
            -d "$outdir" \
            --power \
            --sde-arch=icx \
            -- \
            "$BENCH" \
              --config "$CONFIG" \
              --n-blocks "$N_BLOCKS" \
              --n-iters "$N_ITERS" \
              --seq-len "$SEQ_LEN" \
              --batch-size "$BATCH_SIZE" \
              --n-threads "$n" \
              --weight-type q4_k \
              --kv-type f16 \
              --quiet
        rc=$?
        echo "[t=$n] end: $(date -Is)  exit=$rc"
        echo "$rc" > "$outdir/EXIT_CODE"
    } > "$log" 2>&1 &
    echo "[sweep] launched threads=$n pid=$!  log=$log  outdir=$outdir" \
        | tee -a "$SWEEP_DIR/sweep.info"
}

for n in "${THREADS_LIST[@]}"; do
    run_one "$n"
done

# Block this process until every child Sniper run is done so the screen
# session has a clear lifetime; otherwise the script (and screen) exit
# immediately while the actual sims keep running detached.
wait
echo "[sweep] all jobs finished: $(date -Is)" | tee -a "$SWEEP_DIR/sweep.info"

# Quick summary at the bottom of sweep.info
{
    echo
    echo "=== Summary ==="
    for n in "${THREADS_LIST[@]}"; do
        outdir="$SWEEP_DIR/threads_${n}"
        rc="$(cat "$outdir/EXIT_CODE" 2>/dev/null || echo missing)"
        ipc=$(grep -m1 "^  IPC" "$outdir/sim.out" 2>/dev/null | awk '{print $3}')
        rd_pwr=$(grep -m1 "^Processor:" -A60 "$outdir/power.txt" 2>/dev/null \
                  | grep -m1 "Runtime Dynamic" | awk '{print $4, $5}')
        echo "threads=$n  exit=$rc  IPC(core0)=${ipc:-n/a}  RuntimeDynamic=${rd_pwr:-n/a}"
    done
} | tee -a "$SWEEP_DIR/sweep.info"
