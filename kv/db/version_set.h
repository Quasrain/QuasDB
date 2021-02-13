#pragma once

#include <map>
#include <set>
#include <vector>

#include "dbformat.h"
#include "version_edit.h"

namespace QuasDB
{
  namespace log
  {
    class Writer;
  }

  class Compaction;
  class Iterator;
  class MemTable;
  class TableBuilder;
  class TableCache;
  class Version;
  class VersionSet;
  class WritableFile;

  int FindFile(const InternalKeyComparator &icmp,
               const std::vector<FileMetaData *> &files, const Slice &key);

  // Returns true iff some file in "files" overlaps the user key range
  // [*smallest,*largest].
  // smallest==nullptr represents a key smaller than all keys in the DB.
  // largest==nullptr represents a key largest than all keys in the DB.
  // REQUIRES: If disjoint_sorted_files, files[] contains disjoint ranges
  //           in sorted order.
  bool SomeFileOverlapsRange(const InternalKeyComparator &icmp,
                             bool disjoint_sorted_files,
                             const std::vector<FileMetaData *> &files,
                             const Slice *smallest_user_key,
                             const Slice *largest_user_key);
} // namespace QuasDB