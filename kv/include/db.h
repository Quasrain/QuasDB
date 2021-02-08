#pragma once

#include <cstdint>
#include <cstdio>

#include "iterator.h"
#include "options.h"

namespace QuasDB
{
  static const int kMajorVersion = 1;
  static const int kMinorVersion = 22;

  struct Options;
  struct ReadOptions;
  struct WriteOptions;
  class WriteBatch;

  // Abstract handle to particular state of a DB.
  // A Snapshot is an immutable object and can therefore be safely
  // accessed from multiple threads without any external synchronization.
  class Snapshot
  {
  protected:
    virtual ~Snapshot();
  };

  // a range of keys
  struct Range
  {
    Range() = default;
    Range(const Slice &s, const Slice &l) : start(s), limit(l) {}

    Slice start; // Included in the range
    Slice limit; // Not included in the range
  };

  // A DB is a persistent ordered map from keys to values.
  // A DB is safe for concurrent access from multiple threads without
  // any external synchronization.
  class DB
  {
  public:
    // Open the database with the specified "name".
    // Stores a pointer to a heap-allocated database in *dbptr and returns
    // OK on success.
    // Stores nullptr in *dbptr and returns a non-OK status on error.
    // Caller should delete *dbptr when it is no longer needed.
    static Status Open(const Options &options, const std::string &name,
                       DB **dbptr);

    DB() = default;

    DB(const DB &) = delete;
    DB &operator=(const DB &) = delete;

    virtual ~DB();

    // Set the database entry for "key" to "value".  Returns OK on success,
    // and a non-OK status on error.
    // Note: consider setting options.sync = true.
    virtual Status Put(const WriteOptions &options, const Slice &key,
                       const Slice &value) = 0;

    // Remove the database entry (if any) for "key".  Returns OK on
    // success, and a non-OK status on error.  It is not an error if "key"
    // did not exist in the database.
    // Note: consider setting options.sync = true.
    virtual Status Delete(const WriteOptions &options, const Slice &key) = 0;

    // Apply the specified updates to the database.
    // Returns OK on success, non-OK on failure.
    // Note: consider setting options.sync = true.
    virtual Status Write(const WriteOptions &options, WriteBatch *updates) = 0;

    // If the database contains an entry for "key" store the
    // corresponding value in *value and return OK.
    //
    // If there is no entry for "key" leave *value unchanged and return
    // a status for which Status::IsNotFound() returns true.
    //
    // May return some other Status on an error.
    virtual Status Get(const ReadOptions &options, const Slice &key,
                       std::string *value) = 0;

    // Return a heap-allocated iterator over the contents of the database.
    // The result of NewIterator() is initially invalid (caller must
    // call one of the Seek methods on the iterator before using it).
    //
    // Caller should delete the iterator when it is no longer needed.
    // The returned iterator should be deleted before this db is deleted.
    virtual Iterator *NewIterator(const ReadOptions &options) = 0;

    // Return a handle to the current DB state.  Iterators created with
    // this handle will all observe a stable snapshot of the current DB
    // state.  The caller must call ReleaseSnapshot(result) when the
    // snapshot is no longer needed.
    virtual const Snapshot *GetSnapshot() = 0;

    // Release a previously acquired snapshot.  Thewe caller must not
    // use "snapshot" after this call.
    virtual void ReleaseSnapshot(const Snapshot *snapshot) = 0;

    virtual bool GetProperty(const Slice &property, std::string *value) = 0;

    virtual void GetApproximateSizes(const Range *range, int n,
                                     uint64_t *sizes) = 0;

    virtual void CompactRange(const Slice *begin, const Slice *end) = 0;
  };

  Status DestroyDB(const std::string &name,
                   const Options &options);

  Status RepairDB(const std::string &dbname,
                  const Options &options);
} // namespace QuasDB