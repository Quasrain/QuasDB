#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace QuasDB
{
  class MemPool
  {
  public:
    MemPool();

    MemPool(const MemPool &) = delete;
    MemPool &operator=(const MemPool &) = delete;

    ~MemPool();

    // Return a pointer to a newly allocated memory block of "bytes" bytes.
    char *Allocate(size_t bytes);

    // Allocate memory with the normal alignment guarantees provided by malloc.
    char *AllocateAligned(size_t bytes);

    // Returns an estimate of the total memory usage of data allocated
    // by the pool.
    size_t MemoryUsage() const
    {
      return memory_usage_.load(std::memory_order_relaxed);
    }

  private:
    char *AllocateFallback(size_t bytes);
    char *AllocateNewBlock(size_t block_bytes);

    // Allocation state
    char *alloc_ptr_;
    size_t alloc_bytes_remaining_;

    // Array of new[] allocated memory blocks
    std::vector<char *> blocks_;

    // Total memory usage of the pool.
    std::atomic<size_t> memory_usage_;
  };

  inline char *MemPool::Allocate(size_t bytes)
  {
    // The semantics of what to return are a bit messy if we allow
    // 0-byte allocations, so we disallow them here (we don't need
    // them for our internal use).
    assert(bytes > 0);
    if (bytes <= alloc_bytes_remaining_)
    {
      char *result = alloc_ptr_;
      alloc_ptr_ += bytes;
      alloc_bytes_remaining_ -= bytes;
      return result;
    }
    return AllocateFallback(bytes);
  }
} // namespace QuasDB