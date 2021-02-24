#include "kv/include/cache.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "kv/util/hash.h"
#include "kv/util/mutexlock.h"

namespace QuasDB
{
  Cache::~Cache() {}

  // FIFO cache implementation
  //
  // Cache entries have an "in_cache" boolean indicating whether the cache has a
  // reference on the entry.  The only ways that this can become false without the
  // entry being passed to its "deleter" are via Erase(), via Insert() when
  // an element with a duplicate key is inserted, or on destruction of the cache.
  //
  // The cache keeps two linked lists of items in the cache.  All items in the
  // cache are in one list or the other, and never both.  Items still referenced
  // by clients but erased from the cache are in neither list.  The lists are:
  // - in-use:  contains the items currently referenced by clients, in no
  //   particular order.  (This list is used for invariant checking.  If we
  //   removed the check, elements that would otherwise be on this list could be
  //   left as disconnected singleton lists.)
  // - FIFO:  contains the items not currently referenced by clients, in FIFO order
  //   Elements are moved between these lists by the Ref() and Unref() methods,
  //   when they detect an element in the cache acquiring or losing its only
  //   external reference.

  // An entry is a variable length heap-allocated structure.  Entries
  // are kept in a circular doubly linked list ordered by access time.
  struct FIFOHandle
  {
    void *value;
    void (*deleter)(const Slice &, void *value);
    FIFOHandle *next_hash;
    FIFOHandle *next;
    FIFOHandle *prev;
    size_t charge;
    size_t key_length;
    bool in_cache;    // Whether entry is in the cache.
    uint32_t refs;    // References, including cache reference, if present.
    uint32_t hash;    // Hash of key(); used for fast sharding and comparisons
    char key_data[1]; // Beginning of key

    Slice key() const
    {
      // next_ is only equal to this if the LRU handle is the list head of an
      // empty list. List heads never have meaningful keys.
      assert(next != this);

      return Slice(key_data, key_length);
    }
  };

  class HandleTable
  {
  public:
    HandleTable() : length_(0), elems_(0), list_(nullptr) { Resize(); }
    ~HandleTable() { delete[] list_; }

    FIFOHandle *Lookup(const Slice &key, uint32_t hash)
    {
      return *FindPointer(key, hash);
    }

    FIFOHandle *Insert(FIFOHandle *h)
    {
      FIFOHandle **ptr = FindPointer(h->key(), h->hash);
      FIFOHandle *old = *ptr;
      h->next_hash = (old == nullptr ? nullptr : old->next_hash);
      *ptr = h;
      if (old == nullptr)
      {
        ++elems_;
        if (elems_ > length_)
        {
          Resize();
        }
      }
      return old;
    }

    FIFOHandle *Remove(const Slice &key, uint32_t hash)
    {
      FIFOHandle **ptr = FindPointer(key, hash);
      FIFOHandle *result = *ptr;
      if (result != nullptr)
      {
        *ptr = result->next_hash;
        --elems_;
      }
      return result;
    }

  private:
    // The table consists of an array of buckets where each bucket is
    // a linked list of cache entries that hash into the bucket.
    uint32_t length_;
    uint32_t elems_;
    FIFOHandle **list_;

    // Return a pointer to slot that points to a cache entry that
    // matches key/hash.  If there is no such cache entry, return a
    // pointer to the trailing slot in the corresponding linked list.
    FIFOHandle **FindPointer(const Slice &key, uint32_t hash)
    {
      FIFOHandle **ptr = &list_[hash & (length_ - 1)];
      while (*ptr != nullptr && ((*ptr)->hash != hash || key != (*ptr)->key()))
      {
        ptr = &(*ptr)->next_hash;
      }
      return ptr;
    }

    void Resize()
    {
      uint32_t new_length = 4;
      while (new_length < elems_)
      {
        new_length <<= 1;
      }
      FIFOHandle **new_list = new FIFOHandle *[new_length];
      memset(new_list, 0, sizeof(new_list[0]) * new_length);
      uint32_t count = 0;
      for (uint32_t i = 0; i < length_; i++)
      {
        FIFOHandle *h = list_[i];
        while (h != nullptr)
        {
          FIFOHandle *next = h->next_hash;
          uint32_t hash = h->hash;
          FIFOHandle **ptr = &new_list[hash & (new_length - 1)];
          h->next_hash = *ptr;
          *ptr = h;
          h = next;
          count++;
        }
      }
      assert(elems_ == count);
      delete[] list_;
      list_ = new_list;
      length_ = new_length;
    }
  };

  // A single shard of sharded cache.
  class FIFOCache
  {
  public:
    FIFOCache();
    ~FIFOCache();

    // Separate from constructor so caller can easily make an array of FIFOCache
    void SetCapacity(size_t capacity)
    {
      capacity_ = capacity;
    }

    // Like Cache methods, but with an extre "hash" parameter.
    Cache::Handle *Insert(const Slice &key, uint32_t hash, void *value,
                          size_t charge,
                          void (*deleter)(const Slice &key, void *value));
    Cache::Handle *Lookup(const Slice &key, uint32_t hash);
    void Release(Cache::Handle *handle);
    void Erase(const Slice &key, uint32_t hash);
    void Prune();
    size_t TotalCharge() const
    {
      MutexLock l(&mutex_);
      return usage_;
    }

  private:
    void FIFO_Remove(FIFOHandle *e);
    void FIFO_Append(FIFOHandle *list, FIFOHandle *e);
    void Ref(FIFOHandle *e);
    void Unref(FIFOHandle *e);
    bool FinishErase(FIFOHandle *e);

    size_t capacity_;

    mutable std::mutex mutex_;
    size_t usage_;

    // Dummy head of FIFO list.
    // fifo.prev is newest entry, fifo.next is oldest entry.
    // Entries have refs==1 and in_cache==true.
    FIFOHandle fifo_;

    // Dummy head of in-use list.
    //Entries are in use by clients, and have refs >= 2 and in_cache==true.
    FIFOHandle in_use_;

    HandleTable table_;
  };

  FIFOCache::FIFOCache() : capacity_(0), usage_(0)
  {
    // Make empty circular linked lists.
    fifo_.next = &fifo_;
    fifo_.prev = &fifo_;
    in_use_.next = &in_use_;
    in_use_.prev = &in_use_;
  }

  FIFOCache::~FIFOCache()
  {
    assert(in_use_.next == &in_use_); // error if caller has an unreleased handle
    for (FIFOHandle *e = fifo_.next; e != &fifo_;)
    {
      FIFOHandle *next = e->next;
      assert(e->in_cache);
      e->in_cache = false;
      assert(e->refs == 1); // Invariant of fifo_ list
      Unref(e);
      e = next;
    }
  }

  void FIFOCache::Ref(FIFOHandle *e)
  {
    if (e->refs == 1 && e->in_cache) // If on fifo_list, move to in_use_list.
    {
      FIFO_Remove(e);
      FIFO_Append(&in_use_, e);
    }
    e->refs++;
  }

  void FIFOCache::Unref(FIFOHandle *e)
  {
    assert(e->refs > 0);
    e->refs--;
    if (e->refs == 0) // deallocate.
    {
      assert(!e->in_cache);
      (*e->deleter)(e->key(), e->value);
      free(e);
    }
    else if (e->in_cache && e->refs == 1)
    {
      // no longer in use, move to fifo_list.
      FIFO_Remove(e);
      FIFO_Append(&fifo_, e);
    }
  }

  void FIFOCache::FIFO_Remove(FIFOHandle *e)
  {
    e->next->prev = e->prev;
    e->prev->next = e->next;
  }

  void FIFOCache::FIFO_Append(FIFOHandle *list, FIFOHandle *e)
  {
    e->next = list;
    e->prev = list->prev;
    e->prev->next = e;
    e->next->prev = e;
  }

  Cache::Handle *FIFOCache::Lookup(const Slice &key, uint32_t hash)
  {
    MutexLock l(&mutex_);
    FIFOHandle *e = table_.Lookup(key, hash);
    if (e != nullptr)
    {
      Ref(e);
    }
    return reinterpret_cast<Cache::Handle *>(e);
  }

  void FIFOCache::Release(Cache::Handle *handle)
  {
    MutexLock l(&mutex_);
    Unref(reinterpret_cast<FIFOHandle *>(handle));
  }

  Cache::Handle *FIFOCache::Insert(const Slice &key, uint32_t hash, void *value,
                                   size_t charge,
                                   void (*deleter)(const Slice &key,
                                                   void *value))
  {
    MutexLock l(&mutex_);

    FIFOHandle *e = reinterpret_cast<FIFOHandle *>(malloc(sizeof(FIFOHandle) - 1 + key.size()));
    e->value = value;
    e->deleter = deleter;
    e->charge = charge;
    e->key_length = key.size();
    e->hash = hash;
    e->in_cache = false;
    e->refs = 1; // for the returned handle.
    std::memcpy(e->key_data, key.data(), key.size());

    if (capacity_ > 0)
    {
      e->refs++; // for the cache's reference.
      e->in_cache = true;
      FIFO_Append(&in_use_, e);
      usage_ += charge;
      FinishErase(table_.Insert(e));
    }
    else
    {
      // don't cache. (capacity_==0 is supported and turns off caching.)
      // next is read by key() in an assert, so it must be initialized
      e->next = nullptr;
    }
    while (usage_ > capacity_ && fifo_.next != &fifo_)
    {
      FIFOHandle *old = fifo_.next;
      assert(old->refs == 1);
      bool erased = FinishErase(table_.Remove(old->key(), old->hash));
      if (!erased) // to avoid unused variable when compiled NDEBUG
      {
        assert(erased);
      }
    }
    return reinterpret_cast<Cache::Handle *>(e);
  }

  // If e != nullptr, finish removing *e from the cache; it has already been
  // removed from the hash table.  Return whether e != nullptr.
  bool FIFOCache::FinishErase(FIFOHandle *e)
  {
    if (e != nullptr)
    {
      assert(e->in_cache);
      FIFO_Remove(e);
      e->in_cache = false;
      usage_ -= e->charge;
      Unref(e);
    }
    return e != nullptr;
  }

  void FIFOCache::Erase(const Slice &key, uint32_t hash)
  {
    MutexLock l(&mutex_);
    FinishErase(table_.Remove(key, hash));
  }

  void FIFOCache::Prune()
  {
    MutexLock l(&mutex_);
    while (fifo_.next != &fifo_)
    {
      FIFOHandle *e = fifo_.next;
      assert(e->refs == 1);
      bool erased = FinishErase(table_.Remove(e->key(), e->hash));
      if (!erased) // to avoid unused variable when compiled NDEBUG
      {
        assert(erased);
      }
    }
  }

  static const int kNumShardBits = 4;
  static const int kNumShards = 1 << kNumShardBits;

  class ShardedFIFOCache : public Cache
  {
  public:
    explicit ShardedFIFOCache(size_t capacity) : last_id_(0)
    {
      const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
      for (int s = 0; s < kNumShards; s++)
      {
        shard_[s].SetCapacity(per_shard);
      }
    }

    ~ShardedFIFOCache() override {}

    Handle *Insert(const Slice &key, void *value, size_t charge,
                   void (*deleter)(const Slice &key, void *value)) override
    {
      const uint32_t hash = HashSlice(key);
      return shard_[Shard(hash)].Insert(key, hash, value, charge, deleter);
    }

    Handle *Lookup(const Slice &key) override
    {
      const uint32_t hash = HashSlice(key);
      return shard_[Shard(hash)].Lookup(key, hash);
    }
    void Release(Handle *handle) override
    {
      FIFOHandle *h = reinterpret_cast<FIFOHandle *>(handle);
      shard_[Shard(h->hash)].Release(handle);
    }
    void Erase(const Slice &key) override
    {
      const uint32_t hash = HashSlice(key);
      shard_[Shard(hash)].Erase(key, hash);
    }
    void *Value(Handle *handle) override
    {
      return reinterpret_cast<FIFOHandle *>(handle)->value;
    }
    uint64_t NewId() override
    {
      MutexLock l(&id_mutex_);
      return ++(last_id_);
    }
    void Prune() override
    {
      for (int s = 0; s < kNumShards; s++)
      {
        shard_[s].Prune();
      }
    }
    size_t TotalCharge() const override
    {
      size_t total = 0;
      for (int s = 0; s < kNumShards; s++)
      {
        total += shard_[s].TotalCharge();
      }
      return total;
    }

  private:
    FIFOCache shard_[kNumShards];
    std::mutex id_mutex_;
    uint64_t last_id_;

    static inline uint32_t HashSlice(const Slice &s)
    {
      return Hash(s.data(), s.size(), 0);
    }

    static inline uint32_t Shard(uint32_t hash)
    {
      return hash >> (32 - kNumShardBits);
    }
  };

  Cache *NewFIFOCache(size_t capacity) { return new ShardedFIFOCache(capacity); }
} // namespace QuasDB