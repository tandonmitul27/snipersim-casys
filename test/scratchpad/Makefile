include ../../config/buildconf.makefile

CC       = $(SNIPER_CC)
SNIPER   = ../../run-sniper
CFG      = skylake
SDE_ARCH = skl
DUMP     = python3 ../../tools/dump_stats.py

all: spm_test
	@echo ""
	@echo "  make run-baseline          SPM disabled — all accesses through cache"
	@echo "  make run-private           per-core private SPM (1 core)"
	@echo "  make run-shared-1port      shared SPM, 1 port  (4 cores)"
	@echo "  make run-shared-4port      shared SPM, 4 ports (4 cores)"
	@echo "  make run-all               all four in sequence"
	@echo ""

spm_test: spm_test.c Makefile
	$(CC) -O2 -o spm_test spm_test.c $(SNIPER_CFLAGS) $(SNIPER_LDFLAGS)

# ── 1. Baseline: SPM disabled, everything through cache hierarchy ────────────
run-baseline: spm_test
	$(SNIPER) -n 1 -c $(CFG) --sde-arch=$(SDE_ARCH) --roi -d out-baseline \
	    -c perf_model/scratchpad/enabled=false \
	    -- ./spm_test
	$(DUMP) out-baseline

# ── 2. Private SPM: per-core, 1 core, 2-cycle access ────────────────────────
run-private: spm_test
	$(SNIPER) -n 1 -c $(CFG) --sde-arch=$(SDE_ARCH) --roi -d out-private \
	    -c perf_model/scratchpad/enabled=true \
	    -c perf_model/scratchpad/shared=false \
	    -- ./spm_test
	$(DUMP) out-private

# ── 3. Shared SPM, 1 port: 4 cores, max contention ──────────────────────────
run-shared-1port: spm_test
	$(SNIPER) -n 4 -c $(CFG) --sde-arch=$(SDE_ARCH) --roi -d out-shared-1port \
	    -c perf_model/scratchpad/enabled=true \
	    -c perf_model/scratchpad/shared=true \
	    -c perf_model/scratchpad/num_ports=1 \
	    -- ./spm_test
	$(DUMP) out-shared-1port

# ── 4. Shared SPM, 4 ports: 4 cores, minimal contention ─────────────────────
run-shared-4port: spm_test
	$(SNIPER) -n 4 -c $(CFG) --sde-arch=$(SDE_ARCH) --roi -d out-shared-4port \
	    -c perf_model/scratchpad/enabled=true \
	    -c perf_model/scratchpad/shared=true \
	    -c perf_model/scratchpad/num_ports=4 \
	    -- ./spm_test
	$(DUMP) out-shared-4port

# ── All four ─────────────────────────────────────────────────────────────────
run-all: run-baseline run-private run-shared-1port run-shared-4port

clean:
	rm -rf spm_test out-baseline out-private out-shared-1port out-shared-4port
