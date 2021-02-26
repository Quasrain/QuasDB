#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <string>

#include "dbformat.h"
#include "log_writer.h"
#include "snapshot.h"
#include "kv/include/db.h"
#include "kv/include/env.h"

namespace QuasDB
{
  class MemTable;
  class TableCache;
  class Version;
  class VersionEdit;
  class VersionSet;

  class DBImpl : public DB
  {
  public:
    DBImpl(const Options &options, const std::string &dbname);

    DBImpl(const DBImpl &) = delete;
    DBImpl &operator=(const DBImpl &) = delete;

    ~DBImpl() override;

    // Implementations of the DB interface
    Status Put(const WriteOptions &, const Slice &key,
               const Slice &value) override;
    Status Delete(const WriteOptions &, const Slice &key) override;
    Status Write(const WriteOptions &options, WriteBatch *updates) override;
    Status Get(const ReadOptions &options, const Slice &key,
               std::string *value) override;
    Iterator *NewIterator(const ReadOptions &) override;
    const Snapshot *GetSnapshot() override;
    void ReleaseSnapshot(const Snapshot *snapshot) override;
    bool GetProperty(const Slice &property, std::string *value) override;
    void GetApproximateSizes(const Range *range, int n, uint64_t *sizes) override;
    void CompactRange(const Slice *begin, const Slice *end) override;

    // Extra methods (for testing) that are not in the public DB interface

    // Compact any files in the named level that overlap [*begin,*end]
    void TEST_CompactRange(int level, const Slice *begin, const Slice *end);

    // Force current memtable contents to be compacted.
    Status TEST_CompactMemTable();

    // Return an internal iterator over the current state of the database.
    // The keys of this iterator are internal keys (see format.h).
    // The returned iterator should be deleted when no longer needed.
    Iterator *TEST_NewInternalIterator();

    // Return the maximum overlapping data (in bytes) at next level for any
    // file at a level >= 1.
    int64_t TEST_MaxNextLevelOverlappingBytes();

    // Record a sample of bytes read at the specified internal key.
    // Samples are taken approximately once every config::kReadBytesPeriod
    // bytes.
    void RecordReadSample(Slice key);

  private:
    friend class DB;
    struct CompactionState;
    struct Writer;

    // Information for a manual compaction
    struct ManualCompaction
    {
      int level;
      bool done;
      const InternalKey *begin; // null means beginning of key range
      const InternalKey *end;   // null means end of key range
      InternalKey tmp_storage;  // Used to keep track of compaction progress
    };

    // Per level compaction stats.  stats_[level] stores the stats for
    // compactions that produced data for the specified "level".
    struct CompactionStats
    {
      CompactionStats() : micros(0), bytes_read(0), bytes_written(0) {}

      void Add(const CompactionStats &c)
      {
        this->micros += c.micros;
        this->bytes_read += c.bytes_read;
        this->bytes_written += c.bytes_written;
      }

      int64_t micros;
      int64_t bytes_read;
      int64_t bytes_written;
    };

    Iterator *NewInternalIterator(const ReadOptions &,
                                  SequenceNumber *latest_snapshot,
                                  uint32_t *seed);

    Status NewDB();

    // Recover the descriptor from persistent storage.  May do a significant
    // amount of work to recover recently logged updates.  Any changes to
    // be made to the descriptor are added to *edit.
    Status Recover(VersionEdit *edit, bool *save_manifest);

    void MaybeIgnoreError(Status *s) const;

    // Delete any unneeded files and stale in-memory entries.
    void RemoveObsoleteFiles();

    // Compact the in-memory write buffer to disk.  Switches to a new
    // log-file/memtable and writes a new descriptor iff successful.
    // Errors are recorded in bg_error_.
    void CompactMemTable();

    Status RecoverLogFile(uint64_t log_number, bool last_log, bool *save_manifest,
                          VersionEdit *edit, SequenceNumber *max_sequence);

    Status WriteLevel0Table(std::shared_ptr<MemTable> mem, VersionEdit *edit, std::shared_ptr<Version> base);

    Status MakeRoomForWrite(bool force /* compact even if there is room? */);
    WriteBatch *BuildBatchGroup(Writer **last_writer);

    void RecordBackgroundError(const Status &s);

    void MaybeScheduleCompaction();
    static void BGWork(void *db);
    void BackgroundCall();
    void BackgroundCompaction();
    void CleanupCompaction(CompactionState *compact);
    Status DoCompactionWork(CompactionState *compact);

    Status OpenCompactionOutputFile(CompactionState *compact);
    Status FinishCompactionOutputFile(CompactionState *compact, Iterator *input);
    Status InstallCompactionResults(CompactionState *compact);

    const Comparator *user_comparator() const
    {
      return internal_comparator_.user_comparator();
    }

    // Constant after construction
    Env *const env_;
    const InternalKeyComparator internal_comparator_;
    const InternalFilterPolicy internal_filter_policy_;
    const Options options_; // options_.comparator == &internal_comparator_
    const bool owns_info_log_;
    const bool owns_cache_;
    const std::string dbname_;

    // table_cache_ provides its own synchronization
    TableCache *const table_cache_;

    // Lock over the persistent DB state.  Non-null iff successfully acquired.
    FileLock *db_lock_;

    // State below is protected by mutex_
    std::mutex mutex_;
    std::atomic<bool> shutting_down_;
    std::condition_variable background_work_finished_signal_;
    std::shared_ptr<MemTable> mem_;
    std::shared_ptr<MemTable> imm_;
    std::atomic<bool> has_imm_; // So bg thread can detect non-null imm_
    WritableFile *logfile_;
    uint64_t logfile_number_;
    log::Writer *log_;
    uint32_t seed_; // For sampling.

    // Queue of writers.
    std::deque<Writer *> writers_;
    WriteBatch *tmp_batch_;

    SnapshotList snapshots_;

    // Set of table files to protect from deletion because they are
    // part of ongoing compactions.
    std::set<uint64_t> pending_outputs_;

    // Has a background compaction been scheduled or is running?
    bool background_compaction_scheduled_;

    ManualCompaction *manual_compaction_;

    VersionSet *const versions_;

    // Have we encountered a background error in paranoid mode?
    Status bg_error_;

    CompactionStats stats_[config::kNumLevels];
  };

  // Sanitize db options.  The caller should delete result.info_log if
  // it is not equal to src.info_log.
  Options SanitizeOptions(const std::string &db,
                          const InternalKeyComparator *icmp,
                          const InternalFilterPolicy *ipolicy,
                          const Options &src);
}