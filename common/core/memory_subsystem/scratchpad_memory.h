#ifndef __SCRATCHPAD_MEMORY_H__
#define __SCRATCHPAD_MEMORY_H__

#ifdef ENABLE_SCRATCHPAD

#include "fixed_types.h"
#include "subsecond_time.h"
#include "lock.h"

#include <vector>

class ScratchpadMemory
{
public:
   static const UInt32 MAX_PORTS = 64;

   ScratchpadMemory(const ComponentPeriod *period,
                    UInt32 size_bytes,
                    UInt32 access_latency_cycles,
                    UInt32 dma_bandwidth_bytes_per_cycle,
                    UInt32 dma_startup_overhead_cycles,
                    bool shared,
                    UInt32 num_ports,
                    UInt32 num_cores);

   ~ScratchpadMemory();

   // Direct access (for load/store interception)
   SubsecondTime access(core_id_t requester, IntPtr spm_offset,
                        UInt32 size, bool is_write,
                        SubsecondTime now);

   // DMA between SPM and DRAM (returns modeled transfer latency)
   SubsecondTime dmaLoad(core_id_t requester, UInt32 spm_offset,
                         UInt32 size, SubsecondTime now);
   SubsecondTime dmaStore(core_id_t requester, UInt32 spm_offset,
                          UInt32 size, SubsecondTime now);

   // Address mapping
   void setMappedBase(IntPtr paddr);
   bool isMappedAddress(IntPtr paddr) const;
   UInt32 toOffset(IntPtr paddr) const;

   // DMA address staging (set by preceding marker, consumed by exec marker)
   void setDmaSourceAddr(UInt64 vaddr) { m_dma_source_vaddr = vaddr; }
   void setDmaDestAddr(UInt64 vaddr)   { m_dma_dest_vaddr = vaddr; }
   UInt64 getDmaSourceAddr() const     { return m_dma_source_vaddr; }
   UInt64 getDmaDestAddr() const       { return m_dma_dest_vaddr; }

   // Lifecycle
   void init(UInt32 size);
   void free();
   bool isInitialized() const { return m_initialized; }
   UInt32 getSize() const     { return m_size; }
   bool isShared() const      { return m_shared; }

   // Stats registration (call from MemoryManager constructor)
   void registerStats(core_id_t core_id);

   // Per-core stats accessors (for external registration)
   struct CoreStats {
      UInt64 read_count;
      UInt64 write_count;
      UInt64 dma_load_count;
      UInt64 dma_store_count;
      UInt64 dma_bytes_loaded;
      UInt64 dma_bytes_stored;
      UInt64 contention_stalls;   // shared mode: number of times a core had to wait

      CoreStats()
         : read_count(0), write_count(0)
         , dma_load_count(0), dma_store_count(0)
         , dma_bytes_loaded(0), dma_bytes_stored(0)
         , contention_stalls(0)
      {}
   };

   CoreStats& getCoreStats(core_id_t core_id) { return m_core_stats[core_id]; }

private:
   UInt32 m_size;                    // SPM capacity in bytes (0 until init)
   UInt32 m_max_size;                // max capacity from config
   ComponentLatency m_access_latency;
   UInt32 m_dma_bandwidth;           // bytes per cycle for DMA transfers
   ComponentLatency m_dma_startup;   // fixed overhead per DMA initiation
   bool   m_shared;
   UInt32 m_num_ports;
   IntPtr m_mapped_base;             // physical address base (set by SPM_MAP)
   bool   m_initialized;

   // DMA address staging
   UInt64 m_dma_source_vaddr;
   UInt64 m_dma_dest_vaddr;

   // Contention tracking (shared mode only)
   SubsecondTime m_port_available_at[MAX_PORTS];

   // Lock for shared mode concurrent access
   Lock m_lock;

   // Per-core stats
   std::vector<CoreStats> m_core_stats;

   // Period reference (for computing cycle-based latencies)
   const ComponentPeriod *m_period;

   // Internal: pick the earliest-available port and return contention delay
   SubsecondTime getContentionDelay(SubsecondTime now, SubsecondTime access_time);
};

#endif /* ENABLE_SCRATCHPAD */
#endif /* __SCRATCHPAD_MEMORY_H__ */
