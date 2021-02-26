#pragma once

#include <cstdint>

#include "dbformat.h"
#include "kv/include/db.h"

namespace QuasDB
{

  class DBImpl;

  // Return a new iterator that converts internal keys (yielded by
  // "*internal_iter") that were live at the specified "sequence" number
  // into appropriate user keys.
  Iterator *NewDBIterator(DBImpl *db, const Comparator *user_key_comparator,
                          Iterator *internal_iter, SequenceNumber sequence,
                          uint32_t seed);
}