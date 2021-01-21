#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "kv/include/env.h"
#include "kv/include/slice.h"
#include "kv/include/status.h"

namespace QuasDB
{
  namespace
  {
    // Set by EnvPosixTestHelper::SetReadOnlyMMapLimit() and MaxOpenFiles().
    int g_open_read_only_file_limit = -1;

    // Up to 1000 mmap regions for 64-bit binaries; none for 32-bit.
    constexpr const int kDefaultMmapLimit = (sizeof(void *) >= 8) ? 1000 : 0;

    // Can be set using EnvPosixTestHelper::SetReadOnlyMMapLimit().
    int g_mmap_limit = kDefaultMmapLimit;

    constexpr const int kOpenBaseFlags = 0;

    constexpr const size_t kWritableFileBufferSize = 65536;

    Status PosixError(const std::string &context, int error_number)
    {
      if (error_number == ENOENT)
      {
        return Status::NotFound(context, std::strerror(error_number));
      }
      else
      {
        return Status::IOError(context, std::strerror(error_number));
      }
    }

    // Helper class to limit resource usage to avoid exhaustion.
    // Currently used to limit read-only file descriptors and mmap file usage
    // so that we do not run out of file descriptors or virtual memory, or run into
    // kernel performance problems for very large databases.
    class Limiter
    {
    public:
      // Limit maximum number of resources to |max_acquires|.
      Limiter(int max_acquires) : acquires_allowed_(max_acquires) {}

      Limiter(const Limiter &) = delete;
      Limiter operator=(const Limiter &) = delete;

      // If another resource is available, acquire it and return true.
      // Else return false.
      bool Acquire()
      {
        int old_acquires_allowed =
            acquires_allowed_.fetch_sub(1, std::memory_order_relaxed);

        if (old_acquires_allowed > 0)
          return true;

        acquires_allowed_.fetch_add(1, std::memory_order_relaxed);
        return false;
      }

      // Release a resource acquired by a previous call to Acquire() that returned
      // true.
      void Release() { acquires_allowed_.fetch_add(1, std::memory_order_relaxed); }

    private:
      // The number of available resources.
      //
      // This is a counter and is not tied to the invariants of any other class, so
      // it can be operated on safely using std::memory_order_relaxed.
      std::atomic<int> acquires_allowed_;
    };
  } // namespace
} // namespace QuasDB
