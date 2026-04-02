#ifndef CACHE_SET_LRU_H
#define CACHE_SET_LRU_H

#include "cache_set.h"

class CacheSetInfoLRU : public CacheSetInfo
{
   public:
      CacheSetInfoLRU(String name, String cfgname, core_id_t core_id, UInt32 associativity, UInt8 num_attempts);
      virtual ~CacheSetInfoLRU();
      void increment(UInt32 index)
      {
         LOG_ASSERT_ERROR(index < m_associativity, "Index(%d) >= Associativity(%d)", index, m_associativity);
         ++m_access[index];
      }
      void incrementAttempt(UInt8 attempt)
      {
         if (m_attempts)
            ++m_attempts[attempt];
         else
            LOG_ASSERT_ERROR(attempt == 0, "No place to store attempt# histogram but attempt != 0");
      }
#ifdef ENABLE_KV_PINNING
      void incrementKVInsertion(UInt32 index, UInt32 num_kv_reserved_ways)
      {
         if (index < num_kv_reserved_ways)
            ++m_kv_insert_reserved;
         else
            ++m_kv_insert_nonreserved;
      }

      void incrementRegularInsertion(UInt32 index, UInt32 num_kv_reserved_ways)
      {
         if (index < num_kv_reserved_ways)
            ++m_regular_insert_reserved;
         else
            ++m_regular_insert_nonreserved;
      }
#endif
   private:
      const UInt32 m_associativity;
      UInt64* m_access;
      UInt64* m_attempts;
#ifdef ENABLE_KV_PINNING
      UInt64 m_kv_insert_reserved;
      UInt64 m_kv_insert_nonreserved;
      UInt64 m_regular_insert_reserved;
      UInt64 m_regular_insert_nonreserved;
#endif
};

class CacheSetLRU : public CacheSet
{
   public:
      CacheSetLRU(CacheBase::cache_t cache_type,
            UInt32 associativity, UInt32 blocksize, CacheSetInfoLRU* set_info, UInt8 num_attempts);
      virtual ~CacheSetLRU();

      virtual UInt32 getReplacementIndex(CacheCntlr *cntlr);
#ifdef ENABLE_KV_PINNING
      virtual UInt32 getKVReplacementIndex(CacheCntlr *cntlr);
#endif
      void updateReplacementIndex(UInt32 accessed_index);

   protected:
      const UInt8 m_num_attempts;
      UInt8* m_lru_bits;
      CacheSetInfoLRU* m_set_info;
      void moveToMRU(UInt32 accessed_index);
};

#endif /* CACHE_SET_LRU_H */
