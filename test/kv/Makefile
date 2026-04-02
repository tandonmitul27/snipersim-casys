include ../../config/buildconf.makefile

CC       = $(SNIPER_CC)
SNIPER   = ../../run-sniper
CFG      = skylake
SDE_ARCH = skl
DUMP     = python3 ../../tools/dump_stats.py

all: kv_test
	@echo ""
	@echo "  make run-baseline        no KV policy"
	@echo "  make run-bypass          L1/L2 bypass only"
	@echo "  make run-pinning         LLC way pinning only"
	@echo "  make run-bypass-pinning  bypass + pinning"
	@echo "  make run-all             all four in sequence"
	@echo ""

kv_test: kv_test.c Makefile
	$(CC) -O2 -o kv_test kv_test.c $(SNIPER_CFLAGS) $(SNIPER_LDFLAGS)

# ── 1. Baseline ──────────────────────────────────────────────────────────────
run-baseline: kv_test
	$(SNIPER) -n 1 -c $(CFG) --sde-arch=$(SDE_ARCH) --roi -d out-baseline \
	    -- ./kv_test
	$(DUMP) out-baseline

# ── 2. Bypass only ───────────────────────────────────────────────────────────
# kv_cache_reserved_ways=0 disables pinning inside enableKVCachePolicy()
run-bypass: kv_test
	$(SNIPER) -n 1 -c $(CFG) --sde-arch=$(SDE_ARCH) --roi -d out-bypass \
	    -c perf_model/l3_cache/kv_cache_reserved_ways=0 \
	    -- ./kv_test --bypass
	$(DUMP) out-bypass

# ── 3. Pinning only ──────────────────────────────────────────────────────────
# 0xBEEF0003/0004 calls enableKVPinningOnly() — bypass never set
run-pinning: kv_test
	$(SNIPER) -n 1 -c $(CFG) --sde-arch=$(SDE_ARCH) --roi -d out-pinning \
	    -- ./kv_test --pinning
	$(DUMP) out-pinning

# ── 4. Bypass + Pinning ──────────────────────────────────────────────────────
run-bypass-pinning: kv_test
	$(SNIPER) -n 1 -c $(CFG) --sde-arch=$(SDE_ARCH) --roi -d out-bypass-pinning \
	    -- ./kv_test --bypass-pinning
	$(DUMP) out-bypass-pinning

# ── All four ─────────────────────────────────────────────────────────────────
run-all: run-baseline run-bypass run-pinning run-bypass-pinning

clean:
	rm -rf kv_test out-baseline out-bypass out-pinning out-bypass-pinning
