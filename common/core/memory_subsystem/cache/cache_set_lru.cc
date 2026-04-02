#include "cache_set_lru.h"
#include "log.h"
#include "stats.h"

// Implements LRU replacement, optionally augmented with Query-Based Selection [Jaleel et al., MICRO'10]

CacheSetLRU::CacheSetLRU(
      CacheBase::cache_t cache_type,
      UInt32 associativity, UInt32 blocksize, CacheSetInfoLRU* set_info, UInt8 num_attempts)
   : CacheSet(cache_type, associativity, blocksize)
   , m_num_attempts(num_attempts)
   , m_set_info(set_info)
{
   m_lru_bits = new UInt8[m_associativity];
   for (UInt32 i = 0; i < m_associativity; i++)
      m_lru_bits[i] = i;
}

CacheSetLRU::~CacheSetLRU()
{
   delete [] m_lru_bits;
}

UInt32
CacheSetLRU::getReplacementIndex(CacheCntlr *cntlr)
{
#ifdef ENABLE_KV_PINNING
   // Regular traffic only touches ways [kv_ways, assoc).
   // When no KV ways are reserved, the full [0, assoc) range is used.
   const UInt32 start = m_num_kv_reserved_ways;
#else
   const UInt32 start = 0;
#endif

   // First try to find an invalid block in the allowed range
   for (UInt32 i = start; i < m_associativity; i++)
   {
      if (!m_cache_block_info_array[i]->isValid())
      {
         moveToMRU(i);
#ifdef ENABLE_KV_PINNING
         if (m_set_info)
            m_set_info->incrementRegularInsertion(i, m_num_kv_reserved_ways);
#endif
         return i;
      }
   }

   // Make m_num_attempts attempts at evicting the LRU block in the allowed range
   for(UInt8 attempt = 0; attempt < m_num_attempts; ++attempt)
   {
      UInt32 index = start;
      UInt8 max_bits = 0;
      for (UInt32 i = start; i < m_associativity; i++)
      {
         if (m_lru_bits[i] > max_bits && isValidReplacement(i))
         {
            index = i;
            max_bits = m_lru_bits[i];
         }
      }
      LOG_ASSERT_ERROR(index < m_associativity, "Error Finding LRU bits");

      bool qbs_reject = false;
      if (attempt < m_num_attempts - 1)
      {
         LOG_ASSERT_ERROR(cntlr != NULL, "CacheCntlr == NULL, QBS can only be used when cntlr is passed in");
         qbs_reject = cntlr->isInLowerLevelCache(m_cache_block_info_array[index]);
      }

      if (qbs_reject)
      {
         moveToMRU(index);
         cntlr->incrementQBSLookupCost();
         continue;
      }
      else
      {
         moveToMRU(index);
         m_set_info->incrementAttempt(attempt);
#ifdef ENABLE_KV_PINNING
         if (m_set_info)
            m_set_info->incrementRegularInsertion(index, m_num_kv_reserved_ways);
#endif
         return index;
      }
   }

   LOG_PRINT_ERROR("Should not reach here");
}

#ifdef ENABLE_KV_PINNING
UInt32
CacheSetLRU::getKVReplacementIndex(CacheCntlr *cntlr)
{
   // KV traffic only touches ways [0, kv_ways).
   // Fall back to the full-range search when way reservation is disabled.
   if (m_num_kv_reserved_ways == 0)
      return getReplacementIndex(cntlr);

   // First try to find an invalid block in the KV-reserved range
   for (UInt32 i = 0; i < m_num_kv_reserved_ways; i++)
   {
      if (!m_cache_block_info_array[i]->isValid())
      {
         moveToMRU(i);
         if (m_set_info)
            m_set_info->incrementKVInsertion(i, m_num_kv_reserved_ways);
         return i;
      }
   }

   // LRU victim within KV-reserved ways only
   UInt32 index = 0;
   UInt8 max_bits = 0;
   for (UInt32 i = 0; i < m_num_kv_reserved_ways; i++)
   {
      if (m_lru_bits[i] > max_bits)
      {
         index = i;
         max_bits = m_lru_bits[i];
      }
   }
   moveToMRU(index);
   if (m_set_info)
      m_set_info->incrementKVInsertion(index, m_num_kv_reserved_ways);
   return index;
}
#endif

void
CacheSetLRU::updateReplacementIndex(UInt32 accessed_index)
{
   m_set_info->increment(m_lru_bits[accessed_index]);
   moveToMRU(accessed_index);
}

void
CacheSetLRU::moveToMRU(UInt32 accessed_index)
{
   for (UInt32 i = 0; i < m_associativity; i++)
   {
      if (m_lru_bits[i] < m_lru_bits[accessed_index])
         m_lru_bits[i] ++;
   }
   m_lru_bits[accessed_index] = 0;
}

CacheSetInfoLRU::CacheSetInfoLRU(String name, String cfgname, core_id_t core_id, UInt32 associativity, UInt8 num_attempts)
   : m_associativity(associativity)
   , m_attempts(NULL)
#ifdef ENABLE_KV_PINNING
   , m_kv_insert_reserved(0)
   , m_kv_insert_nonreserved(0)
   , m_regular_insert_reserved(0)
   , m_regular_insert_nonreserved(0)
#endif
{
   m_access = new UInt64[m_associativity];
   for(UInt32 i = 0; i < m_associativity; ++i)
   {
      m_access[i] = 0;
      registerStatsMetric(name, core_id, String("access-mru-")+itostr(i), &m_access[i]);
   }

   if (num_attempts > 1)
   {
      m_attempts = new UInt64[num_attempts];
      for(UInt32 i = 0; i < num_attempts; ++i)
      {
         m_attempts[i] = 0;
         registerStatsMetric(name, core_id, String("qbs-attempt-")+itostr(i), &m_attempts[i]);
      }
   }

#ifdef ENABLE_KV_PINNING
   registerStatsMetric(name, core_id, "pinning-kv-insert-reserved", &m_kv_insert_reserved);
   registerStatsMetric(name, core_id, "pinning-kv-insert-nonreserved", &m_kv_insert_nonreserved);
   registerStatsMetric(name, core_id, "pinning-regular-insert-reserved", &m_regular_insert_reserved);
   registerStatsMetric(name, core_id, "pinning-regular-insert-nonreserved", &m_regular_insert_nonreserved);
#endif
};

CacheSetInfoLRU::~CacheSetInfoLRU()
{
   delete [] m_access;
   if (m_attempts)
      delete [] m_attempts;
}
