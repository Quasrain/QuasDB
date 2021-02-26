#pragma once
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "kv/include/env.h"
#include "kv/include/slice.h"
#include "kv/util/random.h"

namespace QuasDB
{
  namespace test
  {

    MATCHER(IsOK, "") { return arg.ok(); }

    // Store in *dst a random string of length "len" and return a Slice that
    // references the generated data.
    Slice RandomString(Random *rnd, int len, std::string *dst)
    {
      dst->resize(len);
      for (int i = 0; i < len; i++)
      {
        (*dst)[i] = static_cast<char>(' ' + rnd->Uniform(95)); // ' ' .. '~'
      }
      return Slice(*dst);
    }

    // Return a random key with the specified length that may contain interesting
    // characters (e.g. \x00, \xff, etc.).
    std::string RandomKey(Random *rnd, int len)
    {
      // Make sure to generate a wide variety of characters so we
      // test the boundary conditions for short-key optimizations.
      static const char kTestChars[] = {'\0', '\1', 'a', 'b', 'c',
                                        'd', 'e', '\xfd', '\xfe', '\xff'};
      std::string result;
      for (int i = 0; i < len; i++)
      {
        result += kTestChars[rnd->Uniform(sizeof(kTestChars))];
      }
      return result;
    }

    // Store in *dst a string of length "len" that will compress to
    // "N*compressed_fraction" bytes and return a Slice that references
    // the generated data.
    Slice CompressibleString(Random *rnd, double compressed_fraction, size_t len,
                             std::string *dst)
    {
      int raw = static_cast<int>(len * compressed_fraction);
      if (raw < 1)
        raw = 1;
      std::string raw_data;
      RandomString(rnd, raw, &raw_data);

      // Duplicate the random data until we have filled "len" bytes
      dst->clear();
      while (dst->size() < len)
      {
        dst->append(raw_data);
      }
      dst->resize(len);
      return Slice(*dst);
    }
  }
}