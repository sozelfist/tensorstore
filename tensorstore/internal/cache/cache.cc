// Copyright 2020 The TensorStore Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tensorstore/internal/cache/cache.h"

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <atomic>
#include <bitset>
#include <cassert>
#include <memory>
#include <mutex>  // NOLINT
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/synchronization/mutex.h"
#include "tensorstore/internal/cache/cache_pool_limits.h"
#include "tensorstore/internal/container/intrusive_linked_list.h"
#include "tensorstore/internal/intrusive_ptr.h"
#include "tensorstore/internal/meta/type_traits.h"
#include "tensorstore/internal/metrics/counter.h"
#include "tensorstore/internal/metrics/metadata.h"
#include "tensorstore/internal/mutex.h"

// A CacheEntry owns a strong reference to the Cache that contains it only
// if its reference count is > 0.
//
// A Cache owns a weak reference to the CachePool that contains it only if
// its reference count is > 0.

using ::tensorstore::internal_metrics::MetricMetadata;

namespace tensorstore {
namespace internal_cache {

auto& hit_count = internal_metrics::Counter<int64_t>::New(
    "/tensorstore/cache/hit_count", MetricMetadata("Number of cache hits."));
auto& miss_count = internal_metrics::Counter<int64_t>::New(
    "/tensorstore/cache/miss_count", MetricMetadata("Number of cache misses."));
auto& evict_count = internal_metrics::Counter<int64_t>::New(
    "/tensorstore/cache/evict_count",
    MetricMetadata("Number of evictions from the cache."));

using ::tensorstore::internal::PinnedCacheEntry;

#if !defined(NDEBUG)
inline void DebugAssertMutexHeld(absl::Mutex* mutex) { mutex->AssertHeld(); }
#else
inline void DebugAssertMutexHeld(absl::Mutex* mutex) {}
#endif

using LruListAccessor =
    internal::intrusive_linked_list::MemberAccessor<LruListNode>;

CachePoolImpl::CachePoolImpl(const CachePool::Limits& limits)
    : limits_(limits),
      total_bytes_(0),
      strong_references_(1),
      weak_references_(1) {
  Initialize(LruListAccessor{}, &eviction_queue_);
}

namespace {
inline void AcquireWeakReference(CachePoolImpl* p) {
  [[maybe_unused]] auto old_count =
      p->weak_references_.fetch_add(1, std::memory_order_relaxed);
  TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("CachePool:weak:increment", p,
                                            old_count + 1);
}
void ReleaseWeakReference(CachePoolImpl* p) {
  auto new_count = --p->weak_references_;
  TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("CachePool:weak:decrement", p,
                                            new_count);
  if (new_count == 0) {
    delete Access::StaticCast<CachePool>(p);
  }
}

struct DecrementCacheReferenceCount {
  explicit DecrementCacheReferenceCount(CacheImpl* cache_impl, size_t amount) {
    old_count = cache_impl->reference_count_.fetch_sub(
        amount, std::memory_order_acq_rel);
    new_count = old_count - amount;
    TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("Cache:decrement", cache_impl,
                                              new_count);
  }

  bool should_delete() const {
    return !CacheImpl::ShouldDelete(old_count) &&
           CacheImpl::ShouldDelete(new_count);
  }

  bool should_release_cache_pool_weak_reference() const {
    assert(old_count - new_count == CacheImpl::kStrongReferenceIncrement);
    return !CacheImpl::ShouldHoldPoolWeakReference(new_count);
  }

  size_t old_count, new_count;
};

void UnlinkListNode(LruListNode* node) noexcept {
  Remove(LruListAccessor{}, node);
  Initialize(LruListAccessor{}, node);
}

void UnregisterEntryFromPool(CacheEntryImpl* entry,
                             CachePoolImpl* pool) noexcept {
  DebugAssertMutexHeld(&pool->lru_mutex_);
  UnlinkListNode(entry);
  pool->total_bytes_.fetch_sub(entry->num_bytes_, std::memory_order_relaxed);
}

void AddToEvictionQueue(CachePoolImpl* pool, CacheEntryImpl* entry) noexcept {
  DebugAssertMutexHeld(&pool->lru_mutex_);
  auto* eviction_queue = &pool->eviction_queue_;
  if (!OnlyContainsNode(LruListAccessor{}, entry)) {
    Remove(LruListAccessor{}, entry);
  }
  InsertBefore(LruListAccessor{}, eviction_queue, entry);
}

void DestroyCache(CachePoolImpl* pool, CacheImpl* cache);

void MaybeEvictEntries(CachePoolImpl* pool) noexcept {
  DebugAssertMutexHeld(&pool->lru_mutex_);

  constexpr size_t kBufferSize = 64;
  std::array<CacheEntryImpl*, kBufferSize> entries_to_delete;
  // Indicates for each entry in `entries_to_delete` whether its cache should
  // also be deleted.
  std::bitset<kBufferSize> should_delete_cache_for_entry;
  size_t num_entries_to_delete = 0;

  const auto destroy_entries = [&] {
    internal::ScopedWriterUnlock unlock(pool->lru_mutex_);
    for (size_t i = 0; i < num_entries_to_delete; ++i) {
      auto* entry = entries_to_delete[i];
      if (should_delete_cache_for_entry[i]) {
        DestroyCache(entry->cache_->pool_, entry->cache_);
      }
      // Note: The cache that owns entry may have already been destroyed.
      entry->cache_ = nullptr;
      delete Access::StaticCast<CacheEntry>(entry);
    }
  };

  while (pool->total_bytes_.load(std::memory_order_acquire) >
         pool->limits_.total_bytes_limit) {
    auto* queue = &pool->eviction_queue_;
    if (queue->next == queue) {
      // Queue empty.
      break;
    }
    auto* entry = static_cast<CacheEntryImpl*>(queue->next);
    auto* cache = entry->cache_;
    bool evict = false;
    bool should_delete_cache = false;
    auto& shard = cache->ShardForKey(entry->key_);
    if (absl::MutexLock lock(&shard.mutex);
        entry->reference_count_.load(std::memory_order_acquire) == 0) {
      [[maybe_unused]] size_t erase_count = shard.entries.erase(entry);
      assert(erase_count == 1);
      if (shard.entries.empty()) {
        if (DecrementCacheReferenceCount(cache,
                                         CacheImpl::kNonEmptyShardIncrement)
                .should_delete()) {
          should_delete_cache = true;
        }
      }
      evict = true;
    }
    if (!evict) {
      // Entry is still in use, remove it from LRU eviction list.  For
      // efficiency, entries aren't removed from the eviction list when the
      // reference count increases.  It will be put back on the eviction list
      // the next time the reference count becomes 0.  There is no race
      // condition here because both `cache->entries_mutex_` and
      // `pool->lru_mutex_` are held, and the reference count cannot increase
      // from zero except while holding `cache->entries_mutex_`, and the
      // reference count cannot decrease to zero except while holding
      // `pool->lru_mutex_`.
      UnlinkListNode(entry);
      continue;
    }
    UnregisterEntryFromPool(entry, pool);
    evict_count.Increment();
    // Enqueue entry to be destroyed with `pool->lru_mutex_` released.
    should_delete_cache_for_entry[num_entries_to_delete] = should_delete_cache;
    entries_to_delete[num_entries_to_delete++] = entry;
    if (num_entries_to_delete == entries_to_delete.size()) {
      destroy_entries();
      num_entries_to_delete = 0;
    }
  }
  destroy_entries();
}

void InitializeNewEntry(CacheEntryImpl* entry, CacheImpl* cache) noexcept {
  entry->cache_ = cache;
  entry->reference_count_.store(2, std::memory_order_relaxed);
  entry->num_bytes_ = 0;
  Initialize(LruListAccessor{}, entry);
}

void DestroyCache(CachePoolImpl* pool,
                  CacheImpl* cache) ABSL_NO_THREAD_SAFETY_ANALYSIS {
  if (pool) {
    if (!cache->cache_identifier_.empty()) {
      // Remove from caches array. It is possible, given that this cache has
      // been marked `ShouldDelete == true`, that the pool already contains a
      // replacement cache with the same key; the replacement cache should be
      // ignored.
      absl::MutexLock lock(&pool->caches_mutex_);
      auto it = pool->caches_.find(cache);
      if (it != pool->caches_.end() && *it == cache) {
        pool->caches_.erase(it);
      }
    }
    if (HasLruCache(pool)) {
      absl::MutexLock lru_lock(&pool->lru_mutex_);
      for (auto& shard : cache->shards_) {
        absl::MutexLock lock(&shard.mutex);
        for (CacheEntryImpl* entry : shard.entries) {
          // Increment reference count by 2, to ensure that concurrently
          // releasing the last weak reference to `entry` does not result in a
          // concurrent attempt to return `entry` back to the eviction list.
          entry->reference_count_.fetch_add(2, std::memory_order_acq_rel);
          // Ensure entry is not on LRU list.
          UnregisterEntryFromPool(entry, pool);
        }
      }
      // At this point, no external references to any entry are possible, and
      // the entries can safely be destroyed without holding any locks.
    } else {
      for (auto& shard : cache->shards_) {
        absl::MutexLock lock(&shard.mutex);
        for (CacheEntryImpl* entry : shard.entries) {
          // Increment reference count by 2, to ensure that concurrently
          // releasing the last weak reference to `entry` does not result in a
          // concurrent attempt to return `entry` back to the eviction list.
          entry->reference_count_.fetch_add(2, std::memory_order_acq_rel);
        }
      }
    }
    for (auto& shard : cache->shards_) {
      // absl::MutexLock lock(&shard.mutex);
      for (CacheEntryImpl* entry : shard.entries) {
        assert(entry->reference_count_.load() >= 2 &&
               entry->reference_count_.load() <= 3);
        delete Access::StaticCast<Cache::Entry>(entry);
      }
    }
  }

  delete Access::StaticCast<Cache>(cache);
}

// Decrease `reference_count` in such a way that it only reaches threshold
// while `mutex` is held.
//
// If `reference_count` was decreased to a value less than or equal to
// `lock_threshold`, returns a lock on `mutex`.  Otherwise, returns an
// unlocked `UniqueWriterLock`.
//
// Args:
//   reference_count: Reference count to adjust.
//   mutex: Mutex that must be locked while `reference_count` is decreased to
//     `lock_threshold`.
//   new_count[out]: Set to new reference count on return.
//   decrease_amount: Amount to subtract from `reference_count`.
//   lock_threshold: Maximum reference count for which `mutex` must be locked.
template <typename T, typename LockFn>
inline UniqueWriterLock<absl::Mutex> DecrementReferenceCountWithLock(
    std::atomic<T>& reference_count, LockFn mutex_fn, T& new_count,
    internal::type_identity_t<T> decrease_amount,
    internal::type_identity_t<T> lock_threshold) {
  static_assert(std::is_invocable_v<LockFn>);
  static_assert(std::is_same_v<absl::Mutex&, std::invoke_result_t<LockFn>>);

  // If the new reference count will be greater than lock_threshold, we can
  // simply subtract `decrease_amount`.  However, if the reference count will
  // possibly become less than or equal to `lock_threshold`, we must lock the
  // mutex before subtracting `decrease_amount` to ensure that another thread
  // doesn't concurrently obtain another reference.
  {
    auto count = reference_count.load(std::memory_order_relaxed);
    while (true) {
      if (count <= lock_threshold + decrease_amount) break;
      if (reference_count.compare_exchange_weak(count, count - decrease_amount,
                                                std::memory_order_acq_rel)) {
        new_count = count - decrease_amount;
        return {};
      }
    }
  }

  // Handle the case of the reference_count possibly becoming less than or
  // equal to lock_threshold.
  UniqueWriterLock lock(mutex_fn());
  // Reference count may have changed between the time at which we last
  // checked it and the time at which we acquired the mutex.
  auto count =
      reference_count.fetch_sub(decrease_amount, std::memory_order_acq_rel) -
      decrease_amount;
  new_count = count;
  if (count > lock_threshold) {
    // Reference count has changed, we didn't bring the count to below
    // threshold.
    return {};
  }
  return lock;
}

}  // namespace

void StrongPtrTraitsCacheEntry::decrement_impl(
    CacheEntryImpl* entry_impl) noexcept ABSL_NO_THREAD_SAFETY_ANALYSIS {
  auto* cache = entry_impl->cache_;
  uint32_t new_count;
  if (auto* pool_impl = cache->pool_) {
    if (pool_impl->limits_.total_bytes_limit == 0) {
      CacheImpl::Shard* shard = nullptr;
      auto lock = DecrementReferenceCountWithLock(
          entry_impl->reference_count_,
          [&]() -> absl::Mutex& {
            shard = &cache->ShardForKey(entry_impl->key_);
            return shard->mutex;
          },
          new_count,
          /*decrease_amount=*/2, /*lock_threshold=*/1);
      TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("CacheEntry:decrement",
                                                entry_impl, new_count);
      if (!lock) return;
      if (new_count == 0) {
        shard->entries.erase(entry_impl);
        if (shard->entries.empty()) {
          // Note: There is no need to check `ShouldDelete` conditions here
          // because we still hold a strong reference to the cache (released
          // below), which guarantees that `ShouldDelete == false`.
          cache->reference_count_.fetch_sub(CacheImpl::kNonEmptyShardIncrement,
                                            std::memory_order_relaxed);
        }
        // Release lock before invoking entry destructor, as that may be
        // expensive.
        lock = {};
        delete entry_impl;
      }
    } else {
      auto lock = DecrementReferenceCountWithLock(
          entry_impl->reference_count_,
          [pool_impl]() -> absl::Mutex& { return pool_impl->lru_mutex_; },
          new_count,
          /*decrease_amount=*/2, /*lock_threshold=*/1);
      TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("CacheEntry:decrement",
                                                entry_impl, new_count);
      if (!lock) return;
      if (new_count == 0) {
        AddToEvictionQueue(pool_impl, entry_impl);
        MaybeEvictEntries(pool_impl);
      }
    }
    // `entry` may not be valid at this point.
    assert(new_count <= 1);
  } else {
    new_count =
        entry_impl->reference_count_.fetch_sub(2, std::memory_order_acq_rel) -
        2;
    TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("CacheEntry:decrement",
                                              entry_impl, new_count);
    if (new_count > 1) return;
    delete entry_impl;
  }
  StrongPtrTraitsCache::decrement(Access::StaticCast<Cache>(cache));
}

inline bool TryToAcquireCacheStrongReference(CachePoolImpl* pool,
                                             CacheImpl* cache_impl) {
  auto old_count = cache_impl->reference_count_.load(std::memory_order_relaxed);
  while (true) {
    if (CacheImpl::ShouldDelete(old_count)) {
      return false;
    }
    if (cache_impl->reference_count_.compare_exchange_weak(
            old_count, old_count + CacheImpl::kStrongReferenceIncrement,
            std::memory_order_acq_rel)) {
      TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT(
          "Cache:increment", cache_impl,
          old_count + CacheImpl::kStrongReferenceIncrement);
      if (!CacheImpl::ShouldHoldPoolWeakReference(old_count)) {
        // When the first strong reference to the cache is acquired (either
        // when the cache is created, or when it is retrieved from the
        // caches_ table of the pool after all prior references have been
        // removed), also acquire a weak reference to the pool to ensure the
        // CachePoolImpl object is not destroyed while a cache still
        // references it.
        AcquireWeakReference(pool);
      }
      return true;
    }
  }
}

CachePtr<Cache> GetCacheInternal(
    CachePoolImpl* pool, const std::type_info& cache_type,
    std::string_view cache_key,
    absl::FunctionRef<std::unique_ptr<Cache>()> make_cache) {
  CachePoolImpl::CacheKey key(cache_type, cache_key);
  if (pool && !cache_key.empty()) {
    // An non-empty key indicates to look for an existing cache.
    absl::MutexLock lock(&pool->caches_mutex_);
    auto it = pool->caches_.find(key);
    if (it != pool->caches_.end()) {
      auto* cache = *it;
      if (!TryToAcquireCacheStrongReference(pool, cache)) {
        pool->caches_.erase(it);
      } else {
        return CachePtr<Cache>(Access::StaticCast<Cache>(cache),
                               internal::adopt_object_ref);
      }
    }
  }
  // No existing cache, create a new one with the pool mutex unlocked.
  std::unique_ptr<Cache> new_cache = make_cache();
  if (!new_cache) return CachePtr<Cache>();
  auto* cache_impl = Access::StaticCast<CacheImpl>(new_cache.get());
  cache_impl->pool_ = pool;
  // An empty key indicates not to store the Cache in the map.
  if (!pool || cache_key.empty()) {
    if (pool) {
      AcquireWeakReference(pool);
    }
    new_cache.release();
    TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT(
        "Cache:increment", cache_impl, CacheImpl::kStrongReferenceIncrement);
    cache_impl->reference_count_.store(CacheImpl::kStrongReferenceIncrement,
                                       std::memory_order_relaxed);
    return CachePtr<Cache>(Access::StaticCast<Cache>(cache_impl),
                           internal::adopt_object_ref);
  }
  cache_impl->cache_type_ = &cache_type;
  cache_impl->cache_identifier_ = std::string(cache_key);
  absl::MutexLock lock(&pool->caches_mutex_);
  auto insert_result = pool->caches_.insert(cache_impl);
  if (insert_result.second ||
      !TryToAcquireCacheStrongReference(pool, *insert_result.first)) {
    if (!insert_result.second) {
      const_cast<CacheImpl*&>(*insert_result.first) = cache_impl;
    }
    new_cache.release();
    size_t initial_count = CacheImpl::kStrongReferenceIncrement;
    if (pool->strong_references_.load(std::memory_order_relaxed) != 0) {
      initial_count += CacheImpl::kCachePoolStrongReferenceIncrement;
    }
    cache_impl->reference_count_.store(initial_count,
                                       std::memory_order_relaxed);
    TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("Cache:increment", cache_impl,
                                              initial_count);
    AcquireWeakReference(pool);
  }
  return CachePtr<Cache>(Access::StaticCast<Cache>(*insert_result.first),
                         internal::adopt_object_ref);
}

PinnedCacheEntry<Cache> GetCacheEntryInternal(internal::Cache* cache,
                                              std::string_view key) {
  auto* cache_impl = Access::StaticCast<CacheImpl>(cache);
  PinnedCacheEntry<Cache> returned_entry;
  if (!cache_impl->pool_) {
    std::string temp_key(key);  // May throw, done before allocating entry.
    auto* entry_impl =
        Access::StaticCast<CacheEntryImpl>(cache->DoAllocateEntry());
    entry_impl->key_ = std::move(temp_key);      // noexcept
    InitializeNewEntry(entry_impl, cache_impl);  // noexcept
    StrongPtrTraitsCache::increment(cache);
    returned_entry = PinnedCacheEntry<Cache>(
        Access::StaticCast<CacheEntry>(entry_impl), internal::adopt_object_ref);
  } else {
    auto& shard = cache_impl->ShardForKey(key);
    absl::MutexLock lock(&shard.mutex);
    auto it = shard.entries.find(key);
    if (it != shard.entries.end()) {
      hit_count.Increment();
      auto* entry_impl = *it;
      auto old_count =
          entry_impl->reference_count_.fetch_add(2, std::memory_order_acq_rel);
      TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("CacheEntry:increment",
                                                entry_impl, old_count + 2);
      if (old_count <= 1) {
        // When the first strong reference to an entry is acquired, also
        // acquire a strong reference to the cache to be held by the entry.
        // This ensures the Cache object is not destroyed while any of its
        // entries are referenced.
        StrongPtrTraitsCache::increment(cache);
      }
      // Adopt reference added via `fetch_add` above.
      returned_entry =
          PinnedCacheEntry<Cache>(Access::StaticCast<Cache::Entry>(entry_impl),
                                  internal::adopt_object_ref);
    } else {
      miss_count.Increment();
      std::string temp_key(key);  // May throw, done before allocating entry.
      auto* entry_impl =
          Access::StaticCast<CacheEntryImpl>(cache->DoAllocateEntry());
      entry_impl->key_ = std::move(temp_key);      // noexcept
      InitializeNewEntry(entry_impl, cache_impl);  // noexcept
      std::unique_ptr<CacheEntry> entry(
          Access::StaticCast<CacheEntry>(entry_impl));
      // Add to entries table. This may throw, in which case the entry will be
      // deleted during unwind.
      //
      // Warning: This, like all other aspects of exception safety and
      // `std::bad_alloc`-safety in particular, have not been tested in
      // tensorstore and probably don't work.
      [[maybe_unused]] auto inserted = shard.entries.insert(entry_impl).second;
      assert(inserted);
      if (shard.entries.size() == 1) {
        cache_impl->reference_count_.fetch_add(
            CacheImpl::kNonEmptyShardIncrement, std::memory_order_relaxed);
      }
      StrongPtrTraitsCache::increment(cache);
      returned_entry =
          PinnedCacheEntry<Cache>(entry.release(), internal::adopt_object_ref);
    }
  }
  auto* entry_impl = Access::StaticCast<CacheEntryImpl>(returned_entry.get());
  absl::call_once(entry_impl->initialized_, [&] {
    returned_entry->DoInitialize();
    if (HasLruCache(cache_impl->pool_)) {
      size_t new_size = entry_impl->num_bytes_ =
          cache->DoGetSizeInBytes(returned_entry.get());
      UpdateTotalBytes(*cache_impl->pool_, new_size);
    }
  });
  return returned_entry;
}

void StrongPtrTraitsCache::decrement_impl(CacheImpl* cache) noexcept {
  auto decrement_result =
      DecrementCacheReferenceCount(cache, CacheImpl::kStrongReferenceIncrement);
  CachePoolImpl* pool = nullptr;
  if (decrement_result.should_release_cache_pool_weak_reference()) {
    pool = cache->pool_;
  }
  if (decrement_result.should_delete()) {
    DestroyCache(cache->pool_, cache);
  }
  if (pool) {
    ReleaseWeakReference(pool);
  }
}

CacheImpl::CacheImpl() : pool_(nullptr), reference_count_(0) {}
CacheImpl::~CacheImpl() = default;

void StrongPtrTraitsCachePool::increment(CachePool* p) noexcept {
  auto* pool = Access::StaticCast<CachePoolImpl>(p);
  if (pool->strong_references_.fetch_add(1, std::memory_order_acq_rel) == 0) {
    AcquireWeakReference(Access::StaticCast<CachePoolImpl>(p));
  }
}

void StrongPtrTraitsCachePool::decrement(CachePool* p) noexcept {
  auto* pool = Access::StaticCast<CachePoolImpl>(p);
  size_t new_count;
  auto lock = DecrementReferenceCountWithLock(
      pool->strong_references_,
      [pool]() -> absl::Mutex& { return pool->caches_mutex_; }, new_count,
      /*decrease_amount=*/1, /*lock_threshold=*/0);
  TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("CachePool:decrement", p,
                                            new_count);
  if (!lock) return;
  std::vector<CacheImpl*> caches;
  caches.reserve(pool->caches_.size());
  for (auto* cache : pool->caches_) {
    if (DecrementCacheReferenceCount(
            cache, CacheImpl::kCachePoolStrongReferenceIncrement)
            .should_delete()) {
      caches.push_back(cache);
    }
  }
  lock.unlock();
  for (auto* cache : caches) {
    DestroyCache(pool, cache);
  }
  ReleaseWeakReference(pool);
}

void WeakPtrTraitsCachePool::increment(CachePool* p) noexcept {
  AcquireWeakReference(Access::StaticCast<CachePoolImpl>(p));
}

void WeakPtrTraitsCachePool::decrement(CachePool* p) noexcept {
  ReleaseWeakReference(Access::StaticCast<CachePoolImpl>(p));
}

void intrusive_ptr_decrement(CacheEntryWeakState* p)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  size_t new_weak_count;
  auto weak_lock = DecrementReferenceCountWithLock(
      p->weak_references, [p]() -> absl::Mutex& { return p->mutex; },
      new_weak_count,
      /*decrease_amount=*/1, /*lock_threshold=*/0);
  TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("CacheEntryWeakState:decrement", p,
                                            new_weak_count);
  if (!weak_lock) return;
  // This is the last weak reference.
  auto* entry = p->entry;
  if (!entry) {
    // Entry was already destroyed.  Destroy the weak state now that there are
    // no other weak references.
    weak_lock = {};
    delete p;
    return;
  }

  // Entry still exists.  While still holding `weak_lock`, mark that there are
  // no remaining weak references.
  uint32_t new_count;
  auto* cache = entry->cache_;
  auto* pool = cache->pool_;
  ABSL_ASSUME(pool);
  if (!HasLruCache(pool)) {
    CacheImpl::Shard* shard = nullptr;
    auto entries_lock = DecrementReferenceCountWithLock(
        entry->reference_count_,
        [&]() -> absl::Mutex& {
          shard = &cache->ShardForKey(entry->key_);
          return shard->mutex;
        },
        new_count,
        /*decrease_amount=*/1, /*lock_threshold=*/0);
    TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("CacheEntry:decrement", entry,
                                              new_count);
    weak_lock = {};
    if (!entries_lock) return;
    [[maybe_unused]] size_t erase_count = shard->entries.erase(entry);
    assert(erase_count == 1);
    bool should_delete_cache = false;
    if (shard->entries.empty()) {
      if (DecrementCacheReferenceCount(cache,
                                       CacheImpl::kNonEmptyShardIncrement)
              .should_delete()) {
        should_delete_cache = true;
      }
    }
    entries_lock = {};
    delete Access::StaticCast<CacheEntry>(entry);
    if (should_delete_cache) {
      DestroyCache(pool, cache);
    }
    return;
  }
  auto pool_lock = DecrementReferenceCountWithLock(
      entry->reference_count_,
      [pool]() -> absl::Mutex& { return pool->lru_mutex_; }, new_count,
      /*decrease_amount=*/1,
      /*lock_threshold=*/0);
  TENSORSTORE_INTERNAL_CACHE_DEBUG_REFCOUNT("CacheEntry:decrement", entry,
                                            new_count);
  if (!pool_lock) return;

  // There are also no remaining strong references.  Update the entry's queue
  // state if applicable.
  weak_lock = {};
  AddToEvictionQueue(pool, entry);
  MaybeEvictEntries(pool);
}

internal::IntrusivePtr<CacheEntryWeakState> AcquireWeakCacheEntryReference(
    CacheEntryImpl* entry_impl) {
  CacheEntryWeakState* weak_state =
      entry_impl->weak_state_.load(std::memory_order_acquire);
  if (!weak_state) {
    if (!entry_impl->cache_->pool_) {
      // Weak references aren't supported if cache pool is disabled, since
      // they would serve no purpose.
      return {};
    }
    // Must allocate new weak reference state, since there have been no prior
    // weak references to this entry.
    auto* new_weak_state = new CacheEntryWeakState;
    new_weak_state->entry = entry_impl;
    new_weak_state->weak_references.store(1, std::memory_order_relaxed);
    if (entry_impl->weak_state_.compare_exchange_strong(
            weak_state, new_weak_state, std::memory_order_acq_rel)) {
      // Mark the existence of a weak reference in the entry.
      entry_impl->reference_count_.fetch_add(1, std::memory_order_relaxed);
      return internal::IntrusivePtr<CacheEntryWeakState>(
          new_weak_state, internal::adopt_object_ref);
    } else {
      // Weak state was concurrently created by another thread.
      delete new_weak_state;
    }
  }
  if (weak_state->weak_references.fetch_add(1, std::memory_order_acq_rel) ==
      0) {
    // All previous weak references were released.  Must update the entry to
    // mark the existence of a weak reference.
    entry_impl->reference_count_.fetch_add(1, std::memory_order_relaxed);
  }
  return internal::IntrusivePtr<CacheEntryWeakState>(
      weak_state, internal::adopt_object_ref);
}

void UpdateTotalBytes(CachePoolImpl& pool, ptrdiff_t change) {
  assert(HasLruCache(&pool));
  if (pool.total_bytes_.fetch_add(change, std::memory_order_acq_rel) + change <=
          pool.limits_.total_bytes_limit ||
      change <= 0) {
    return;
  }
  absl::MutexLock lock(&pool.lru_mutex_);
  MaybeEvictEntries(&pool);
}

}  // namespace internal_cache

namespace internal {

Cache::Cache() = default;
Cache::~Cache() = default;

size_t Cache::DoGetSizeInBytes(Cache::Entry* entry) {
  return ((internal_cache::CacheEntryImpl*)entry)->key_.capacity() +
         this->DoGetSizeofEntry();
}

CacheEntry::~CacheEntry() {
  auto* weak_state = this->weak_state_.load(std::memory_order_relaxed);
  if (!weak_state) return;
  {
    absl::MutexLock lock(&weak_state->mutex);
    weak_state->entry = nullptr;
    if (weak_state->weak_references.load(std::memory_order_acquire) != 0) {
      // Don't destroy the weak reference state, since there are still weak
      // references.  It will be destroyed instead when the last weak
      // reference is released.
      return;
    }
  }
  delete weak_state;
}

void CacheEntry::DoInitialize() {}

void CacheEntry::WriterLock() { mutex_.WriterLock(); }

void CacheEntry::WriterUnlock() {
  UniqueWriterLock lock(mutex_, std::adopt_lock);
  auto flags = std::exchange(flags_, 0);
  if (!flags) return;

  // Currently only one flag.
  assert(flags & kSizeChanged);

  auto& cache = GetOwningCache(*this);
  auto* pool = cache.pool();
  auto* pool_impl =
      internal_cache::Access::StaticCast<internal_cache::CachePoolImpl>(pool);
  if (!internal_cache::HasLruCache(pool_impl)) return;

  const size_t new_size = cache.DoGetSizeInBytes(this);
  ptrdiff_t change = new_size - std::exchange(num_bytes_, new_size);
  lock.unlock();

  internal_cache::UpdateTotalBytes(*pool_impl, change);
}

CachePool::StrongPtr CachePool::Make(const CachePool::Limits& cache_limits) {
  CachePool::StrongPtr pool;
  internal_cache::Access::StaticCast<internal_cache::CachePoolStrongPtr>(&pool)
      ->reset(new internal_cache::CachePool(cache_limits), adopt_object_ref);
  return pool;
}

CachePool::StrongPtr::StrongPtr(const CachePool::WeakPtr& ptr)
    : Base(ptr.get(), adopt_object_ref) {
  if (!ptr) return;
  auto* pool =
      internal_cache::Access::StaticCast<internal_cache::CachePoolImpl>(
          ptr.get());
  absl::MutexLock lock(&pool->caches_mutex_);
  if (pool->strong_references_.fetch_add(1, std::memory_order_acq_rel) == 0) {
    internal_cache::AcquireWeakReference(pool);
    for (auto* cache : pool->caches_) {
      cache->reference_count_.fetch_add(
          internal_cache::CacheImpl::kCachePoolStrongReferenceIncrement,
          std::memory_order_acq_rel);
    }
  }
}

}  // namespace internal

}  // namespace tensorstore
