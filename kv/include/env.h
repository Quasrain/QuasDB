#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>
#include <vector>

#include "kv/include/status.h"

namespace QuasDB
{
  class FileLock;
  class Logger;
  class RandomAccessFile;
  class SequentialFile;
  class Slice;
  class WritableFile;

  class Env
  {
  public:
    Env();
    Env(const Env &) = delete;
    Env &operator(const Env &) = delete;

    virtual ~Env();
  }
} // namespace QuasDB