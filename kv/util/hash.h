#pragma once

#include <cstddef>
#include <cstdint>

namespace QuasDB
{
  uint32_t Hash(const char *data, size_t n, uint32_t seed);
} // namespace QuasDB