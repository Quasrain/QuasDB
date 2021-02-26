#include "db_impl.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "builder.h"
#include "db_iter.h"
#include "dbformat.h"
#include "filename.h"
#include "log_reader.h"
#include "log_writer.h"
#include "memtable.h"
#include "table_cache.h"
#include "version_set.h"
#include "write_batch_internal.h"
#include "kv/include/db.h"
#include "kv/include/env.h"
#include "kv/include/status.h"
#include "kv/include/table.h"
#include "kv/include/table_builder.h"
#include "kv/table/block.h"
#include "kv/table/merger.h"
#include "kv/table/two_level_iterator.h"
#include "kv/util/coding.h"
#include "kv/util/logging.h"
#include "kv/util/mutexlock.h"

namespace QuasDB
{
  const int kNumNonTableCacheFiles = 10;

  // Information kept for every waiting writer
  struct DBImpl::Writer
  {
    explicit Writer(std::mutex *mu)
        : batch(nullptr), sync(false), done(false) {}

    Status status;
    WriteBatch *batch;
    bool sync;
    bool done;
    std::condition_variable cv;
  };

  struct DBImpl::CompactionState
  {
    // Files produced by compaction
    struct Output
    {
      uint64_t number;
      uint64_t file_size;
      InternalKey smallest, largest;
    };

    Output *current_output() { return &outputs[outputs.size() - 1]; }

    explicit CompactionState(Compaction *c)
        : compaction(c),
          smallest_snapshot(0),
          outfile(nullptr),
          builder(nullptr),
          total_bytes(0) {}

    Compaction *const compaction;

    // Sequence numbers < smallest_snapshot are not significant since we
    // will never have to service a snapshot below smallest_snapshot.
    // Therefore if we have seen a sequence number S <= smallest_snapshot,
    // we can drop all entries for the same key with sequence numbers < S.
    SequenceNumber smallest_snapshot;

    std::vector<Output> outputs;

    // State kept for output being generated
    WritableFile *outfile;
    TableBuilder *builder;

    uint64_t total_bytes;
  };

  // Fix user-supplied options to be reasonable
  template <class T, class V>
  static void ClipToRange(T *ptr, V minvalue, V maxvalue)
  {
    if (static_cast<V>(*ptr) > maxvalue)
      *ptr = maxvalue;
    if (static_cast<V>(*ptr) < minvalue)
      *ptr = minvalue;
  }
  Options SanitizeOptions(const std::string &dbname,
                          const InternalKeyComparator *icmp,
                          const InternalFilterPolicy *ipolicy,
                          const Options &src)
  {
    Options result = src;
    result.comparator = icmp;
    result.filter_policy = (src.filter_policy != nullptr) ? ipolicy : nullptr;
    ClipToRange(&result.max_open_files, 64 + kNumNonTableCacheFiles, 50000);
    ClipToRange(&result.write_buffer_size, 64 << 10, 1 << 30);
    ClipToRange(&result.max_file_size, 1 << 20, 1 << 30);
    ClipToRange(&result.block_size, 1 << 10, 4 << 20);
    if (result.info_log == nullptr)
    {
      // Open a log file in the same directory as the db
      src.env->CreateDir(dbname); // In case it does not exist
      src.env->RenameFile(InfoLogFileName(dbname), OldInfoLogFileName(dbname));
      Status s = src.env->NewLogger(InfoLogFileName(dbname), &result.info_log);
      if (!s.ok())
      {
        // No place suitable for logging
        result.info_log = nullptr;
      }
    }
    if (result.block_cache == nullptr)
    {
      result.block_cache = NewFIFOCache(8 << 20);
    }
    return result;
  }

  static int TableCacheSize(const Options &sanitized_options)
  {
    // Reserve ten files or so for other uses and give the rest to TableCache.
    return sanitized_options.max_open_files - kNumNonTableCacheFiles;
  }

  DBImpl::DBImpl(const Options &raw_options, const std::string &dbname)
      : env_(raw_options.env),
        internal_comparator_(raw_options.comparator),
        internal_filter_policy_(raw_options.filter_policy),
        options_(SanitizeOptions(dbname, &internal_comparator_,
                                 &internal_filter_policy_, raw_options)),
        owns_info_log_(options_.info_log != raw_options.info_log),
        owns_cache_(options_.block_cache != raw_options.block_cache),
        dbname_(dbname),
        table_cache_(new TableCache(dbname_, options_, TableCacheSize(options_))),
        db_lock_(nullptr),
        shutting_down_(false),
        mem_(nullptr),
        imm_(nullptr),
        has_imm_(false),
        logfile_(nullptr),
        logfile_number_(0),
        log_(nullptr),
        seed_(0),
        tmp_batch_(new WriteBatch),
        background_compaction_scheduled_(false),
        manual_compaction_(nullptr),
        versions_(new VersionSet(dbname_, &options_, table_cache_,
                                 &internal_comparator_)) {}

  DBImpl::~DBImpl()
  {
    // Wait for background work to finish.
    mutex_.lock();
    shutting_down_.store(true, std::memory_order_release);
    while (background_compaction_scheduled_)
    {
      std::unique_lock<std::mutex> lock(mutex_, std::adopt_lock);
      background_work_finished_signal_.wait(lock);
      lock.release();
    }
    mutex_.unlock();

    if (db_lock_ != nullptr)
    {
      env_->UnlockFile(db_lock_);
    }

    delete versions_;
    if (mem_ != nullptr)
      mem_ = nullptr;
    if (imm_ != nullptr)
      imm_ = nullptr;
    delete tmp_batch_;
    delete log_;
    delete logfile_;
    delete table_cache_;

    if (owns_info_log_)
    {
      delete options_.info_log;
    }
    if (owns_cache_)
    {
      delete options_.block_cache;
    }
  }

  Status DBImpl::NewDB()
  {
    VersionEdit new_db;
    new_db.SetComparatorName(user_comparator()->Name());
    new_db.SetLogNumber(0);
    new_db.SetNextFile(2);
    new_db.SetLastSequence(0);

    const std::string manifest = DescriptorFileName(dbname_, 1);
    WritableFile *file;
    Status s = env_->NewWritableFile(manifest, &file);
    if (!s.ok())
    {
      return s;
    }
    {
      log::Writer log(file);
      std::string record;
      new_db.EncodeTo(&record);
      s = log.AddRecord(record);
      if (s.ok())
      {
        s = file->Close();
      }
    }
    delete file;
    if (s.ok())
    {
      // Make "CURRENT" file that points to the new manifest file.
      s = SetCurrentFile(env_, dbname_, 1);
    }
    else
    {
      env_->RemoveFile(manifest);
    }
    return s;
  }

  void DBImpl::MaybeIgnoreError(Status *s) const
  {
    if (s->ok() || options_.paranoid_checks)
    {
      // No change needed
    }
    else
    {
      Log(options_.info_log, "Ignoring error %s", s->ToString().c_str());
      *s = Status::OK();
    }
  }

  void DBImpl::RemoveObsoleteFiles()
  {
    if (!bg_error_.ok())
    {
      // After a background error, we don't know whether a new version may
      // or may not have been committed, so we cannot safely garbage collect.
      return;
    }

    // Make a set of all of the live files
    std::set<uint64_t> live = pending_outputs_;
    versions_->AddLiveFiles(&live);

    std::vector<std::string> filenames;
    env_->GetChildren(dbname_, &filenames); // Ignoring errors on purpose
    uint64_t number;
    FileType type;
    std::vector<std::string> files_to_delete;
    for (std::string &filename : filenames)
    {
      if (ParseFileName(filename, &number, &type))
      {
        bool keep = true;
        switch (type)
        {
        case kLogFile:
          keep = ((number >= versions_->LogNumber()) ||
                  (number == versions_->PrevLogNumber()));
          break;
        case kDescriptorFile:
          // Keep my manifest file, and any newer incarnations'
          // (in case there is a race that allows other incarnations)
          keep = (number >= versions_->ManifestFileNumber());
          break;
        case kTableFile:
          keep = (live.find(number) != live.end());
          break;
        case kTempFile:
          // Any temp files that are currently being written to must
          // be recorded in pending_outputs_, which is inserted into "live"
          keep = (live.find(number) != live.end());
          break;
        case kCurrentFile:
        case kDBLockFile:
        case kInfoLogFile:
          keep = true;
          break;
        }

        if (!keep)
        {
          files_to_delete.push_back(std::move(filename));
          if (type == kTableFile)
          {
            table_cache_->Evict(number);
          }
          Log(options_.info_log, "Delete type=%d #%lld\n", static_cast<int>(type),
              static_cast<unsigned long long>(number));
        }
      }
    }

    // While deleting all files unblock other threads. All files being deleted
    // have unique names which will not collide with newly created files and
    // are therefore safe to delete while allowing other threads to proceed.
    mutex_.unlock();
    for (const std::string &filename : files_to_delete)
    {
      env_->RemoveFile(dbname_ + "/" + filename);
    }
    mutex_.lock();
  }

  Status DBImpl::Recover(VersionEdit *edit, bool *save_manifest)
  {
    // Ignore error from CreateDir since the creation of the DB is
    // committed only when the descriptor is created, and this directory
    // may already exist from a previous failed creation attempt.
    env_->CreateDir(dbname_);
    assert(db_lock_ == nullptr);
    Status s = env_->LockFile(LockFileName(dbname_), &db_lock_);
    if (!s.ok())
    {
      return s;
    }

    if (!env_->FileExists(CurrentFileName(dbname_)))
    {
      if (options_.create_if_missing)
      {
        s = NewDB();
        if (!s.ok())
        {
          return s;
        }
      }
      else
      {
        return Status::InvalidArgument(
            dbname_, "does not exist (create_if_missing is false)");
      }
    }
    else
    {
      if (options_.error_if_exists)
      {
        return Status::InvalidArgument(dbname_,
                                       "exists (error_if_exists is true)");
      }
    }

    s = versions_->Recover(save_manifest);
    if (!s.ok())
    {
      return s;
    }
    SequenceNumber max_sequence(0);

    // Recover from all newer log files than the ones named in the
    // descriptor (new log files may have been added by the previous
    // incarnation without registering them in the descriptor).
    //
    // Note that PrevLogNumber() is no longer used, but we pay
    // attention to it in case we are recovering a database
    // produced by an older version of leveldb.
    const uint64_t min_log = versions_->LogNumber();
    const uint64_t prev_log = versions_->PrevLogNumber();
    std::vector<std::string> filenames;
    s = env_->GetChildren(dbname_, &filenames);
    if (!s.ok())
    {
      return s;
    }
    std::set<uint64_t> expected;
    versions_->AddLiveFiles(&expected);
    uint64_t number;
    FileType type;
    std::vector<uint64_t> logs;
    for (size_t i = 0; i < filenames.size(); i++)
    {
      if (ParseFileName(filenames[i], &number, &type))
      {
        expected.erase(number);
        if (type == kLogFile && ((number >= min_log) || (number == prev_log)))
          logs.push_back(number);
      }
    }
    if (!expected.empty())
    {
      char buf[50];
      std::snprintf(buf, sizeof(buf), "%d missing files; e.g.",
                    static_cast<int>(expected.size()));
      return Status::Corruption(buf, TableFileName(dbname_, *(expected.begin())));
    }

    // Recover in the order in which the logs were generated
    std::sort(logs.begin(), logs.end());
    for (size_t i = 0; i < logs.size(); i++)
    {
      s = RecoverLogFile(logs[i], (i == logs.size() - 1), save_manifest, edit,
                         &max_sequence);
      if (!s.ok())
      {
        return s;
      }

      // The previous incarnation may not have written any MANIFEST
      // records after allocating this log number.  So we manually
      // update the file number allocation counter in VersionSet.
      versions_->MarkFileNumberUsed(logs[i]);
    }

    if (versions_->LastSequence() < max_sequence)
    {
      versions_->SetLastSequence(max_sequence);
    }

    return Status::OK();
  }

  Status DBImpl::RecoverLogFile(uint64_t log_number, bool last_log,
                                bool *save_manifest, VersionEdit *edit,
                                SequenceNumber *max_sequence)
  {
    struct LogReporter : public log::Reader::Reporter
    {
      Env *env;
      Logger *info_log;
      const char *fname;
      Status *status; // null if options_.paranoid_checks==false
      void Corruption(size_t bytes, const Status &s) override
      {
        Log(info_log, "%s%s: dropping %d bytes; %s",
            (this->status == nullptr ? "(ignoring error) " : ""), fname,
            static_cast<int>(bytes), s.ToString().c_str());
        if (this->status != nullptr && this->status->ok())
          *this->status = s;
      }
    };

    // Open the log file
    std::string fname = LogFileName(dbname_, log_number);
    SequentialFile *file;
    Status status = env_->NewSequentialFile(fname, &file);
    if (!status.ok())
    {
      MaybeIgnoreError(&status);
      return status;
    }

    // Create the log reader.
    LogReporter reporter;
    reporter.env = env_;
    reporter.info_log = options_.info_log;
    reporter.fname = fname.c_str();
    reporter.status = (options_.paranoid_checks ? &status : nullptr);
    // We intentionally make log::Reader do checksumming even if
    // paranoid_checks==false so that corruptions cause entire commits
    // to be skipped instead of propagating bad information (like overly
    // large sequence numbers).
    log::Reader reader(file, &reporter, true /*checksum*/, 0 /*initial_offset*/);
    Log(options_.info_log, "Recovering log #%llu",
        (unsigned long long)log_number);

    // Read all the records and add to a memtable
    std::string scratch;
    Slice record;
    WriteBatch batch;
    int compactions = 0;
    std::shared_ptr<MemTable> mem = nullptr;
    while (reader.ReadRecord(&record, &scratch) && status.ok())
    {
      if (record.size() < 12)
      {
        reporter.Corruption(record.size(),
                            Status::Corruption("log record too small"));
        continue;
      }
      WriteBatchInternal::SetContents(&batch, record);

      if (mem == nullptr)
      {
        mem.reset(new MemTable(internal_comparator_));
      }
      status = WriteBatchInternal::InsertInto(&batch, mem);
      MaybeIgnoreError(&status);
      if (!status.ok())
      {
        break;
      }
      const SequenceNumber last_seq = WriteBatchInternal::Sequence(&batch) +
                                      WriteBatchInternal::Count(&batch) - 1;
      if (last_seq > *max_sequence)
      {
        *max_sequence = last_seq;
      }

      if (mem->ApproximateMemoryUsage() > options_.write_buffer_size)
      {
        compactions++;
        *save_manifest = true;
        status = WriteLevel0Table(mem, edit, nullptr);
        mem = nullptr;
        if (!status.ok())
        {
          // Reflect errors immediately so that conditions like full
          // file-systems cause the DB::Open() to fail.
          break;
        }
      }
    }

    delete file;

    // See if we should keep reusing the last log file.
    if (status.ok() && options_.reuse_logs && last_log && compactions == 0)
    {
      assert(logfile_ == nullptr);
      assert(log_ == nullptr);
      assert(mem_ == nullptr);
      uint64_t lfile_size;
      if (env_->GetFileSize(fname, &lfile_size).ok() &&
          env_->NewAppendableFile(fname, &logfile_).ok())
      {
        Log(options_.info_log, "Reusing old log %s \n", fname.c_str());
        log_ = new log::Writer(logfile_, lfile_size);
        logfile_number_ = log_number;
        if (mem != nullptr)
        {
          mem_ = mem;
          mem = nullptr;
        }
        else
        {
          // mem can be nullptr if lognum exists but was empty.
          mem_.reset(new MemTable(internal_comparator_));
        }
      }
    }

    if (mem != nullptr)
    {
      // mem did not get reused; compact it.
      if (status.ok())
      {
        *save_manifest = true;
        status = WriteLevel0Table(mem, edit, nullptr);
      }
      mem = nullptr;
    }

    return status;
  }

  Status DBImpl::WriteLevel0Table(std::shared_ptr<MemTable> mem, VersionEdit *edit,
                                  std::shared_ptr<Version> base)
  {
    const uint64_t start_micros = env_->NowMicros();
    FileMetaData meta;
    meta.number = versions_->NewFileNumber();
    pending_outputs_.insert(meta.number);
    Iterator *iter = mem->NewIterator();
    Log(options_.info_log, "Level-0 table #%llu: started",
        (unsigned long long)meta.number);

    Status s;
    {
      mutex_.unlock();
      s = BuildTable(dbname_, env_, options_, table_cache_, iter, &meta);
      mutex_.lock();
    }

    Log(options_.info_log, "Level-0 table #%llu: %lld bytes %s",
        (unsigned long long)meta.number, (unsigned long long)meta.file_size,
        s.ToString().c_str());
    delete iter;
    pending_outputs_.erase(meta.number);

    // Note that if file_size is zero, the file has been deleted and
    // should not be added to the manifest.
    int level = 0;
    if (s.ok() && meta.file_size > 0)
    {
      const Slice min_user_key = meta.smallest.user_key();
      const Slice max_user_key = meta.largest.user_key();
      if (base != nullptr)
      {
        level = base->PickLevelForMemTableOutput(min_user_key, max_user_key);
      }
      edit->AddFile(level, meta.number, meta.file_size, meta.smallest,
                    meta.largest);
    }

    CompactionStats stats;
    stats.micros = env_->NowMicros() - start_micros;
    stats.bytes_written = meta.file_size;
    stats_[level].Add(stats);
    return s;
  }

  void DBImpl::CompactMemTable()
  {
    assert(imm_ != nullptr);

    // Save the contents of the memtable as a new Table
    VersionEdit edit;
    std::shared_ptr<Version> base = versions_->current();
    Status s = WriteLevel0Table(imm_, &edit, base);

    if (s.ok() && shutting_down_.load(std::memory_order_acquire))
    {
      s = Status::IOError("Deleting DB during memtable compaction");
    }

    // Replace immutable memtable with the generated Table
    if (s.ok())
    {
      edit.SetPrevLogNumber(0);
      edit.SetLogNumber(logfile_number_); // Earlier logs no longer needed
      s = versions_->LogAndApply(&edit, &mutex_);
    }

    if (s.ok())
    {
      // Commit to the new state
      imm_ = nullptr;
      has_imm_.store(false, std::memory_order_release);
      RemoveObsoleteFiles();
    }
    else
    {
      RecordBackgroundError(s);
    }
  }

  void DBImpl::CompactRange(const Slice *begin, const Slice *end)
  {
    int max_level_with_files = 1;
    {
      MutexLock l(&mutex_);
      std::shared_ptr<Version> base = versions_->current();
      for (int level = 1; level < config::kNumLevels; level++)
      {
        if (base->OverlapInLevel(level, begin, end))
        {
          max_level_with_files = level;
        }
      }
    }
    TEST_CompactMemTable(); // TODO(sanjay): Skip if memtable does not overlap
    for (int level = 0; level < max_level_with_files; level++)
    {
      TEST_CompactRange(level, begin, end);
    }
  }

  void DBImpl::TEST_CompactRange(int level, const Slice *begin,
                                 const Slice *end)
  {
    assert(level >= 0);
    assert(level + 1 < config::kNumLevels);

    InternalKey begin_storage, end_storage;

    ManualCompaction manual;
    manual.level = level;
    manual.done = false;
    if (begin == nullptr)
    {
      manual.begin = nullptr;
    }
    else
    {
      begin_storage = InternalKey(*begin, kMaxSequenceNumber, kValueTypeForSeek);
      manual.begin = &begin_storage;
    }
    if (end == nullptr)
    {
      manual.end = nullptr;
    }
    else
    {
      end_storage = InternalKey(*end, 0, static_cast<ValueType>(0));
      manual.end = &end_storage;
    }

    MutexLock l(&mutex_);
    while (!manual.done && !shutting_down_.load(std::memory_order_acquire) &&
           bg_error_.ok())
    {
      if (manual_compaction_ == nullptr)
      { // Idle
        manual_compaction_ = &manual;
        MaybeScheduleCompaction();
      }
      else
      { // Running either my compaction or another compaction.
        std::unique_lock<std::mutex> lock(mutex_, std::adopt_lock);
        background_work_finished_signal_.wait(lock);
        lock.release();
      }
    }
    if (manual_compaction_ == &manual)
    {
      // Cancel my manual compaction since we aborted early for some reason.
      manual_compaction_ = nullptr;
    }
  }

  Status DBImpl::TEST_CompactMemTable()
  {
    // nullptr batch means just wait for earlier writes to be done
    Status s = Write(WriteOptions(), nullptr);
    if (s.ok())
    {
      // Wait until the compaction completes
      MutexLock l(&mutex_);
      while (imm_ != nullptr && bg_error_.ok())
      {
        std::unique_lock<std::mutex> lock(mutex_, std::adopt_lock);
        background_work_finished_signal_.wait(lock);
        lock.release();
      }
      if (imm_ != nullptr)
      {
        s = bg_error_;
      }
    }
    return s;
  }

  void DBImpl::RecordBackgroundError(const Status &s)
  {
    if (bg_error_.ok())
    {
      bg_error_ = s;
      background_work_finished_signal_.notify_all();
    }
  }

  void DBImpl::MaybeScheduleCompaction()
  {
    if (background_compaction_scheduled_)
    {
      // Already scheduled
    }
    else if (shutting_down_.load(std::memory_order_acquire))
    {
      // DB is being deleted; no more background compactions
    }
    else if (!bg_error_.ok())
    {
      // Already got an error; no more changes
    }
    else if (imm_ == nullptr && manual_compaction_ == nullptr &&
             !versions_->NeedsCompaction())
    {
      // No work to be done
    }
    else
    {
      background_compaction_scheduled_ = true;
      env_->Schedule(&DBImpl::BGWork, this);
    }
  }

  void DBImpl::BGWork(void *db)
  {
    reinterpret_cast<DBImpl *>(db)->BackgroundCall();
  }

  void DBImpl::BackgroundCall()
  {
    MutexLock l(&mutex_);
    assert(background_compaction_scheduled_);
    if (shutting_down_.load(std::memory_order_acquire))
    {
      // No more background work when shutting down.
    }
    else if (!bg_error_.ok())
    {
      // No more background work after a background error.
    }
    else
    {
      BackgroundCompaction();
    }

    background_compaction_scheduled_ = false;

    // Previous compaction may have produced too many files in a level,
    // so reschedule another compaction if needed.
    MaybeScheduleCompaction();
    background_work_finished_signal_.notify_all();
  }

  void DBImpl::BackgroundCompaction()
  {
    if (imm_ != nullptr)
    {
      CompactMemTable();
      return;
    }

    Compaction *c;
    bool is_manual = (manual_compaction_ != nullptr);
    InternalKey manual_end;
    if (is_manual)
    {
      ManualCompaction *m = manual_compaction_;
      c = versions_->CompactRange(m->level, m->begin, m->end);
      m->done = (c == nullptr);
      if (c != nullptr)
      {
        manual_end = c->input(0, c->num_input_files(0) - 1)->largest;
      }
      Log(options_.info_log,
          "Manual compaction at level-%d from %s .. %s; will stop at %s\n",
          m->level, (m->begin ? m->begin->DebugString().c_str() : "(begin)"),
          (m->end ? m->end->DebugString().c_str() : "(end)"),
          (m->done ? "(end)" : manual_end.DebugString().c_str()));
    }
    else
    {
      c = versions_->PickCompaction();
    }

    Status status;
    if (c == nullptr)
    {
      // Nothing to do
    }
    else if (!is_manual && c->IsTrivialMove())
    {
      // Move file to next level
      assert(c->num_input_files(0) == 1);
      FileMetaData *f = c->input(0, 0);
      c->edit()->RemoveFile(c->level(), f->number);
      c->edit()->AddFile(c->level() + 1, f->number, f->file_size, f->smallest,
                         f->largest);
      status = versions_->LogAndApply(c->edit(), &mutex_);
      if (!status.ok())
      {
        RecordBackgroundError(status);
      }
      VersionSet::LevelSummaryStorage tmp;
      Log(options_.info_log, "Moved #%lld to level-%d %lld bytes %s: %s\n",
          static_cast<unsigned long long>(f->number), c->level() + 1,
          static_cast<unsigned long long>(f->file_size),
          status.ToString().c_str(), versions_->LevelSummary(&tmp));
    }
    else
    {
      CompactionState *compact = new CompactionState(c);
      status = DoCompactionWork(compact);
      if (!status.ok())
      {
        RecordBackgroundError(status);
      }
      CleanupCompaction(compact);
      c->ReleaseInputs();
      RemoveObsoleteFiles();
    }
    delete c;

    if (status.ok())
    {
      // Done
    }
    else if (shutting_down_.load(std::memory_order_acquire))
    {
      // Ignore compaction errors found during shutting down
    }
    else
    {
      Log(options_.info_log, "Compaction error: %s", status.ToString().c_str());
    }

    if (is_manual)
    {
      ManualCompaction *m = manual_compaction_;
      if (!status.ok())
      {
        m->done = true;
      }
      if (!m->done)
      {
        // We only compacted part of the requested range.  Update *m
        // to the range that is left to be compacted.
        m->tmp_storage = manual_end;
        m->begin = &m->tmp_storage;
      }
      manual_compaction_ = nullptr;
    }
  }

  void DBImpl::CleanupCompaction(CompactionState *compact)
  {
    if (compact->builder != nullptr)
    {
      // May happen if we get a shutdown call in the middle of compaction
      compact->builder->Abandon();
      delete compact->builder;
    }
    else
    {
      assert(compact->outfile == nullptr);
    }
    delete compact->outfile;
    for (size_t i = 0; i < compact->outputs.size(); i++)
    {
      const CompactionState::Output &out = compact->outputs[i];
      pending_outputs_.erase(out.number);
    }
    delete compact;
  }

  Status DBImpl::OpenCompactionOutputFile(CompactionState *compact)
  {
    assert(compact != nullptr);
    assert(compact->builder == nullptr);
    uint64_t file_number;
    {
      mutex_.lock();
      file_number = versions_->NewFileNumber();
      pending_outputs_.insert(file_number);
      CompactionState::Output out;
      out.number = file_number;
      out.smallest.Clear();
      out.largest.Clear();
      compact->outputs.push_back(out);
      mutex_.unlock();
    }

    // Make the output file
    std::string fname = TableFileName(dbname_, file_number);
    Status s = env_->NewWritableFile(fname, &compact->outfile);
    if (s.ok())
    {
      compact->builder = new TableBuilder(options_, compact->outfile);
    }
    return s;
  }

  Status DBImpl::FinishCompactionOutputFile(CompactionState *compact,
                                            Iterator *input)
  {
    assert(compact != nullptr);
    assert(compact->outfile != nullptr);
    assert(compact->builder != nullptr);

    const uint64_t output_number = compact->current_output()->number;
    assert(output_number != 0);

    // Check for iterator errors
    Status s = input->status();
    const uint64_t current_entries = compact->builder->NumEntries();
    if (s.ok())
    {
      s = compact->builder->Finish();
    }
    else
    {
      compact->builder->Abandon();
    }
    const uint64_t current_bytes = compact->builder->FileSize();
    compact->current_output()->file_size = current_bytes;
    compact->total_bytes += current_bytes;
    delete compact->builder;
    compact->builder = nullptr;

    // Finish and check for file errors
    if (s.ok())
    {
      s = compact->outfile->Sync();
    }
    if (s.ok())
    {
      s = compact->outfile->Close();
    }
    delete compact->outfile;
    compact->outfile = nullptr;

    if (s.ok() && current_entries > 0)
    {
      // Verify that the table is usable
      Iterator *iter =
          table_cache_->NewIterator(ReadOptions(), output_number, current_bytes);
      s = iter->status();
      delete iter;
      if (s.ok())
      {
        Log(options_.info_log, "Generated table #%llu@%d: %lld keys, %lld bytes",
            (unsigned long long)output_number, compact->compaction->level(),
            (unsigned long long)current_entries,
            (unsigned long long)current_bytes);
      }
    }
    return s;
  }

  Status DBImpl::InstallCompactionResults(CompactionState *compact)
  {
    Log(options_.info_log, "Compacted %d@%d + %d@%d files => %lld bytes",
        compact->compaction->num_input_files(0), compact->compaction->level(),
        compact->compaction->num_input_files(1), compact->compaction->level() + 1,
        static_cast<long long>(compact->total_bytes));

    // Add compaction outputs
    compact->compaction->AddInputDeletions(compact->compaction->edit());
    const int level = compact->compaction->level();
    for (size_t i = 0; i < compact->outputs.size(); i++)
    {
      const CompactionState::Output &out = compact->outputs[i];
      compact->compaction->edit()->AddFile(level + 1, out.number, out.file_size,
                                           out.smallest, out.largest);
    }
    return versions_->LogAndApply(compact->compaction->edit(), &mutex_);
  }

  Status DBImpl::DoCompactionWork(CompactionState *compact)
  {
    const uint64_t start_micros = env_->NowMicros();
    int64_t imm_micros = 0; // Micros spent doing imm_ compactions

    Log(options_.info_log, "Compacting %d@%d + %d@%d files",
        compact->compaction->num_input_files(0), compact->compaction->level(),
        compact->compaction->num_input_files(1),
        compact->compaction->level() + 1);

    assert(versions_->NumLevelFiles(compact->compaction->level()) > 0);
    assert(compact->builder == nullptr);
    assert(compact->outfile == nullptr);
    if (snapshots_.empty())
    {
      compact->smallest_snapshot = versions_->LastSequence();
    }
    else
    {
      compact->smallest_snapshot = snapshots_.oldest()->sequence_number();
    }

    Iterator *input = versions_->MakeInputIterator(compact->compaction);

    // Release mutex while we're actually doing the compaction work
    mutex_.unlock();

    input->SeekToFirst();
    Status status;
    ParsedInternalKey ikey;
    std::string current_user_key;
    bool has_current_user_key = false;
    SequenceNumber last_sequence_for_key = kMaxSequenceNumber;
    while (input->Valid() && !shutting_down_.load(std::memory_order_acquire))
    {
      // Prioritize immutable compaction work
      if (has_imm_.load(std::memory_order_relaxed))
      {
        const uint64_t imm_start = env_->NowMicros();
        mutex_.lock();
        if (imm_ != nullptr)
        {
          CompactMemTable();
          // Wake up MakeRoomForWrite() if necessary.
          background_work_finished_signal_.notify_all();
        }
        mutex_.unlock();
        imm_micros += (env_->NowMicros() - imm_start);
      }

      Slice key = input->key();
      if (compact->compaction->ShouldStopBefore(key) &&
          compact->builder != nullptr)
      {
        status = FinishCompactionOutputFile(compact, input);
        if (!status.ok())
        {
          break;
        }
      }

      // Handle key/value, add to state, etc.
      bool drop = false;
      if (!ParseInternalKey(key, &ikey))
      {
        // Do not hide error keys
        current_user_key.clear();
        has_current_user_key = false;
        last_sequence_for_key = kMaxSequenceNumber;
      }
      else
      {
        if (!has_current_user_key ||
            user_comparator()->Compare(ikey.user_key, Slice(current_user_key)) !=
                0)
        {
          // First occurrence of this user key
          current_user_key.assign(ikey.user_key.data(), ikey.user_key.size());
          has_current_user_key = true;
          last_sequence_for_key = kMaxSequenceNumber;
        }

        if (last_sequence_for_key <= compact->smallest_snapshot)
        {
          // Hidden by an newer entry for same user key
          drop = true; // (A)
        }
        else if (ikey.type == kTypeDeletion &&
                 ikey.sequence <= compact->smallest_snapshot &&
                 compact->compaction->IsBaseLevelForKey(ikey.user_key))
        {
          // For this user key:
          // (1) there is no data in higher levels
          // (2) data in lower levels will have larger sequence numbers
          // (3) data in layers that are being compacted here and have
          //     smaller sequence numbers will be dropped in the next
          //     few iterations of this loop (by rule (A) above).
          // Therefore this deletion marker is obsolete and can be dropped.
          drop = true;
        }

        last_sequence_for_key = ikey.sequence;
      }

      if (!drop)
      {
        // Open output file if necessary
        if (compact->builder == nullptr)
        {
          status = OpenCompactionOutputFile(compact);
          if (!status.ok())
          {
            break;
          }
        }
        if (compact->builder->NumEntries() == 0)
        {
          compact->current_output()->smallest.DecodeFrom(key);
        }
        compact->current_output()->largest.DecodeFrom(key);
        compact->builder->Add(key, input->value());

        // Close output file if it is big enough
        if (compact->builder->FileSize() >=
            compact->compaction->MaxOutputFileSize())
        {
          status = FinishCompactionOutputFile(compact, input);
          if (!status.ok())
          {
            break;
          }
        }
      }

      input->Next();
    }

    if (status.ok() && shutting_down_.load(std::memory_order_acquire))
    {
      status = Status::IOError("Deleting DB during compaction");
    }
    if (status.ok() && compact->builder != nullptr)
    {
      status = FinishCompactionOutputFile(compact, input);
    }
    if (status.ok())
    {
      status = input->status();
    }
    delete input;
    input = nullptr;

    CompactionStats stats;
    stats.micros = env_->NowMicros() - start_micros - imm_micros;
    for (int which = 0; which < 2; which++)
    {
      for (int i = 0; i < compact->compaction->num_input_files(which); i++)
      {
        stats.bytes_read += compact->compaction->input(which, i)->file_size;
      }
    }
    for (size_t i = 0; i < compact->outputs.size(); i++)
    {
      stats.bytes_written += compact->outputs[i].file_size;
    }

    mutex_.lock();
    stats_[compact->compaction->level() + 1].Add(stats);

    if (status.ok())
    {
      status = InstallCompactionResults(compact);
    }
    if (!status.ok())
    {
      RecordBackgroundError(status);
    }
    VersionSet::LevelSummaryStorage tmp;
    Log(options_.info_log, "compacted to: %s", versions_->LevelSummary(&tmp));
    return status;
  }

  namespace
  {

    struct IterState
    {
      std::mutex *mu;
      std::shared_ptr<Version> version;
      std::shared_ptr<MemTable> mem;
      std::shared_ptr<MemTable> imm;

      IterState(std::mutex *mutex, std::shared_ptr<MemTable> mem, std::shared_ptr<MemTable> imm, std::shared_ptr<Version> version)
          : mu(mutex), version(version), mem(mem), imm(imm) {}
    };

    static void CleanupIteratorState(void *arg1, void *arg2)
    {
      IterState *state = reinterpret_cast<IterState *>(arg1);
      state->mu->lock();
      state->mem = nullptr;
      if (state->imm != nullptr)
        state->imm = nullptr;
      state->version = nullptr;
      state->mu->unlock();
      delete state;
    }
  }

  Iterator *DBImpl::NewInternalIterator(const ReadOptions &options,
                                        SequenceNumber *latest_snapshot,
                                        uint32_t *seed)
  {
    mutex_.lock();
    *latest_snapshot = versions_->LastSequence();

    // Collect together all needed child iterators
    std::vector<Iterator *> list;
    list.push_back(mem_->NewIterator());
    if (imm_ != nullptr)
    {
      list.push_back(imm_->NewIterator());
    }
    versions_->current()->AddIterators(options, &list);
    Iterator *internal_iter =
        NewMergingIterator(&internal_comparator_, &list[0], list.size());

    IterState *cleanup = new IterState(&mutex_, mem_, imm_, versions_->current());
    internal_iter->RegisterCleanup(CleanupIteratorState, cleanup, nullptr);

    *seed = ++seed_;
    mutex_.unlock();
    return internal_iter;
  }

  Iterator *DBImpl::TEST_NewInternalIterator()
  {
    SequenceNumber ignored;
    uint32_t ignored_seed;
    return NewInternalIterator(ReadOptions(), &ignored, &ignored_seed);
  }

  int64_t DBImpl::TEST_MaxNextLevelOverlappingBytes()
  {
    MutexLock l(&mutex_);
    return versions_->MaxNextLevelOverlappingBytes();
  }

  Status DBImpl::Get(const ReadOptions &options, const Slice &key,
                     std::string *value)
  {
    Status s;
    MutexLock l(&mutex_);
    SequenceNumber snapshot;
    if (options.snapshot != nullptr)
    {
      snapshot =
          static_cast<const SnapshotImpl *>(options.snapshot)->sequence_number();
    }
    else
    {
      snapshot = versions_->LastSequence();
    }

    std::shared_ptr<MemTable> mem = mem_;
    std::shared_ptr<MemTable> imm = imm_;
    std::shared_ptr<Version> current = versions_->current();

    bool have_stat_update = false;
    Version::GetStats stats;

    // Unlock while reading from files and memtables
    {
      mutex_.unlock();
      // First look in the memtable, then in the immutable memtable (if any).
      LookupKey lkey(key, snapshot);
      if (mem->Get(lkey, value, &s))
      {
        // Done
      }
      else if (imm != nullptr && imm->Get(lkey, value, &s))
      {
        // Done
      }
      else
      {
        s = current->Get(options, lkey, value, &stats);
        have_stat_update = true;
      }
      mutex_.lock();
    }

    if (have_stat_update && current->UpdateStats(stats))
    {
      MaybeScheduleCompaction();
    }
    mem = nullptr;
    if (imm != nullptr)
      imm = nullptr;
    current = nullptr;
    return s;
  }
} // namespace QuasDB
