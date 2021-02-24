#pragma once

#include <cstdint>
#include <vector>

#include "kv/include/slice.h"

namespace QuasDB
{
  struct Options;

  class BlockBuilder
  {
  public:
    explicit BlockBuilder(const Options *options);

    BlockBuilder(const BlockBuilder &) = delete;
    BlockBuilder &operator=(const BlockBuilder &) = delete;

    // Reset the contents as if the BlockBuilder was just constructed.
    void Reset();

    void Add(const Slice &key, const Slice &value);

    // Finish building the block and return a slice that refers to the
    // block contents.  The returned slice will remain valid for the
    // lifetime of this builder or until Reset() is called.
    Slice Finish();

    // Returns an estimate of the current (uncompressed) size of the block
    // we are building.
    size_t CurrentSizeEstimate() const;

    // Return true iff no entries have been added since the last Reset()
    bool empty() const { return buffer_.empty(); }

  private:
    const Options *options_;
    std::string buffer_;             // Destination buffer
    std::vector<uint32_t> restarts_; // Restart points
    int counter_;                    // Number of entries emitted since restart
    bool finished_;                  // Has Finish() been called?
    std::string last_key_;
  };
} // namespace QuasDB