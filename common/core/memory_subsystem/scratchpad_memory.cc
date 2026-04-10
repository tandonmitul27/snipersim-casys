#ifdef ENABLE_SCRATCHPAD

#include "scratchpad_memory.h"
#include "stats.h"
#include "log.h"
#include "simulator.h"

#include <algorithm>
#include <cstring>

ScratchpadMemory::ScratchpadMemory(
      const ComponentPeriod *period,
      UInt32 size_bytes,
      UInt32 access_latency_cycles,
      UInt32 dma_bandwidth_bytes_per_cycle,
      UInt32 dma_startup_overhead_cycles,
      bool shared,
      UInt32 num_ports,
      UInt32 num_cores)
   : m_size(0)
   , m_max_size(size_bytes)
   , m_access_latency(period, access_latency_cycles)
   , m_dma_bandwidth(dma_bandwidth_bytes_per_cycle)
   , m_dma_startup(period, dma_startup_overhead_cycles)
   , m_shared(shared)
   , m_num_ports(std::min(num_ports, (UInt32)MAX_PORTS))
   , m_mapped_base(0)
   , m_initialized(false)
   , m_dma_source_vaddr(0)
   , m_dma_dest_vaddr(0)
   , m_core_stats(num_cores)
   , m_period(period)
{
   LOG_ASSERT_ERROR(num_ports > 0 && num_ports <= MAX_PORTS,
      "num_ports must be between 1 and %u, got %u", MAX_PORTS, num_ports);

   for (UInt32 i = 0; i < MAX_PORTS; i++)
      m_port_available_at[i] = SubsecondTime::Zero();
}

ScratchpadMemory::~ScratchpadMemory()
{
}

void ScratchpadMemory::init(UInt32 size)
{
   if (m_initialized && m_shared)
   {
      // Shared mode: subsequent SPM_INIT calls are no-ops
      return;
   }

   LOG_ASSERT_ERROR(size <= m_max_size,
      "SPM init size (%u) exceeds configured max (%u)", size, m_max_size);

   m_size = size;
   m_initialized = true;
   m_mapped_base = 0;

   LOG_PRINT("ScratchpadMemory::init size=%u shared=%d ports=%u",
      m_size, m_shared, m_num_ports);
}

void ScratchpadMemory::free()
{
   m_initialized = false;
   m_mapped_base = 0;
   m_size = 0;
}

void ScratchpadMemory::setMappedBase(IntPtr paddr)
{
   if (m_mapped_base != 0 && m_shared)
   {
      // Shared mode: subsequent SPM_MAP calls are no-ops
      return;
   }

   m_mapped_base = paddr;
   LOG_PRINT("ScratchpadMemory::setMappedBase paddr=0x%lx size=%u",
      (unsigned long)m_mapped_base, m_size);
}

bool ScratchpadMemory::isMappedAddress(IntPtr paddr) const
{
   if (!m_initialized || m_mapped_base == 0)
      return false;

   return (paddr >= m_mapped_base) && (paddr < m_mapped_base + (IntPtr)m_size);
}

UInt32 ScratchpadMemory::toOffset(IntPtr paddr) const
{
   LOG_ASSERT_ERROR(isMappedAddress(paddr),
      "Address 0x%lx is not in SPM range [0x%lx, 0x%lx)",
      (unsigned long)paddr, (unsigned long)m_mapped_base,
      (unsigned long)(m_mapped_base + m_size));

   return (UInt32)(paddr - m_mapped_base);
}

SubsecondTime ScratchpadMemory::getContentionDelay(SubsecondTime now, SubsecondTime access_time)
{
   // Find the port with the earliest availability
   UInt32 best_port = 0;
   SubsecondTime earliest = m_port_available_at[0];

   for (UInt32 i = 1; i < m_num_ports; i++)
   {
      if (m_port_available_at[i] < earliest)
      {
         earliest = m_port_available_at[i];
         best_port = i;
      }
   }

   SubsecondTime start_time = std::max(now, earliest);
   SubsecondTime delay = start_time - now;  // contention stall

   // Advance port availability
   m_port_available_at[best_port] = start_time + access_time;

   return delay;
}

SubsecondTime ScratchpadMemory::access(
      core_id_t requester, IntPtr spm_offset,
      UInt32 size, bool is_write,
      SubsecondTime now)
{
   LOG_ASSERT_ERROR(m_initialized, "SPM access before init");
   LOG_ASSERT_ERROR(spm_offset + size <= m_size,
      "SPM access out of bounds: offset=%u size=%u spm_size=%u",
      spm_offset, size, m_size);

   SubsecondTime base_lat = m_access_latency.getLatency();
   SubsecondTime contention_delay = SubsecondTime::Zero();

   if (m_shared)
   {
      ScopedLock sl(m_lock);
      contention_delay = getContentionDelay(now, base_lat);
      if (contention_delay > SubsecondTime::Zero())
         m_core_stats[requester].contention_stalls++;
   }

   if (is_write)
      m_core_stats[requester].write_count++;
   else
      m_core_stats[requester].read_count++;

   return base_lat + contention_delay;
}

SubsecondTime ScratchpadMemory::dmaLoad(
      core_id_t requester, UInt32 spm_offset,
      UInt32 size, SubsecondTime now)
{
   LOG_ASSERT_ERROR(m_initialized, "DMA load before SPM init");
   LOG_ASSERT_ERROR(spm_offset + size <= m_size,
      "DMA load out of bounds: offset=%u size=%u spm_size=%u",
      spm_offset, size, m_size);

   // Transfer latency = startup + ceil(size / bandwidth) cycles
   UInt64 transfer_cycles = (size + m_dma_bandwidth - 1) / m_dma_bandwidth;
   SubsecondTime transfer_lat = m_dma_startup.getLatency()
      + static_cast<SubsecondTime>(*m_period) * transfer_cycles;

   SubsecondTime contention_delay = SubsecondTime::Zero();
   if (m_shared)
   {
      ScopedLock sl(m_lock);
      contention_delay = getContentionDelay(now, transfer_lat);
      if (contention_delay > SubsecondTime::Zero())
         m_core_stats[requester].contention_stalls++;
   }

   m_core_stats[requester].dma_load_count++;
   m_core_stats[requester].dma_bytes_loaded += size;

   return transfer_lat + contention_delay;
}

SubsecondTime ScratchpadMemory::dmaStore(
      core_id_t requester, UInt32 spm_offset,
      UInt32 size, SubsecondTime now)
{
   LOG_ASSERT_ERROR(m_initialized, "DMA store before SPM init");
   LOG_ASSERT_ERROR(spm_offset + size <= m_size,
      "DMA store out of bounds: offset=%u size=%u spm_size=%u",
      spm_offset, size, m_size);

   UInt64 transfer_cycles = (size + m_dma_bandwidth - 1) / m_dma_bandwidth;
   SubsecondTime transfer_lat = m_dma_startup.getLatency()
      + static_cast<SubsecondTime>(*m_period) * transfer_cycles;

   SubsecondTime contention_delay = SubsecondTime::Zero();
   if (m_shared)
   {
      ScopedLock sl(m_lock);
      contention_delay = getContentionDelay(now, transfer_lat);
      if (contention_delay > SubsecondTime::Zero())
         m_core_stats[requester].contention_stalls++;
   }

   m_core_stats[requester].dma_store_count++;
   m_core_stats[requester].dma_bytes_stored += size;

   return transfer_lat + contention_delay;
}

void ScratchpadMemory::registerStats(core_id_t core_id)
{
   CoreStats& s = m_core_stats[core_id];

   registerStatsMetric("scratchpad", core_id, "reads",             &s.read_count);
   registerStatsMetric("scratchpad", core_id, "writes",            &s.write_count);
   registerStatsMetric("scratchpad", core_id, "dma-loads",         &s.dma_load_count);
   registerStatsMetric("scratchpad", core_id, "dma-stores",        &s.dma_store_count);
   registerStatsMetric("scratchpad", core_id, "dma-bytes-loaded",  &s.dma_bytes_loaded);
   registerStatsMetric("scratchpad", core_id, "dma-bytes-stored",  &s.dma_bytes_stored);
   registerStatsMetric("scratchpad", core_id, "contention-stalls", &s.contention_stalls);
}

#endif /* ENABLE_SCRATCHPAD */
