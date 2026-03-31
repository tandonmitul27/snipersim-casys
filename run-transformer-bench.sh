#!/usr/bin/env bash
# Wrapper around run-sniper for transformer-benchmark runs.
# Saves results to results/<timestamp>_<config>_<ncores>c/
#
# Usage:
#   ./run-transformer-bench.sh --config tiny_test --n-blocks 1 --n-iters 2 --seq-len 16
#
# Optional overrides (must come before --config):
#   --sniper-cores <N>    number of simulated cores (default: 18)
#   --sniper-cfg <name>   sniper config name (default: skylake)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCH_BIN="$SCRIPT_DIR/transformer-benchmark/build/bin/bench-transformer"
CONFIGS_DIR="$SCRIPT_DIR/transformer-benchmark/bench-transformer/configs"
RESULTS_DIR="$SCRIPT_DIR/results"

# Defaults
SNIPER_CORES=18
SNIPER_CFG=skylake

# Pull out our own args before passing the rest to bench-transformer
BENCH_ARGS=()
MODEL_CONFIG="tiny_test"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sniper-cores) SNIPER_CORES="$2"; shift 2 ;;
        --sniper-cfg)   SNIPER_CFG="$2";   shift 2 ;;
        --config)       MODEL_CONFIG="$2"; BENCH_ARGS+=("$1" "$CONFIGS_DIR/$2.json"); shift 2 ;;
        *)              BENCH_ARGS+=("$1"); shift ;;
    esac
done

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUTDIR="$RESULTS_DIR/${TIMESTAMP}_${MODEL_CONFIG}_${SNIPER_CORES}c"
mkdir -p "$OUTDIR"

echo "[BENCH] Sniper config : $SNIPER_CFG ($SNIPER_CORES cores)"
echo "[BENCH] Model config  : $MODEL_CONFIG"
echo "[BENCH] Output dir    : $OUTDIR"
echo "[BENCH] Bench args    : ${BENCH_ARGS[*]}"
echo ""

env -i \
    HOME="$HOME" \
    PATH=/usr/bin:/bin:/usr/local/bin \
    SNIPER_ROOT="$SCRIPT_DIR" \
    bash -c "cd '$SCRIPT_DIR' && \
        ./run-sniper -n $SNIPER_CORES -c $SNIPER_CFG --sde-arch=skl \
            -d '$OUTDIR' \
            -- '$BENCH_BIN' ${BENCH_ARGS[*]}"

echo ""
echo "[BENCH] Results saved to: $OUTDIR"
echo "[BENCH] Files:"
ls "$OUTDIR"
