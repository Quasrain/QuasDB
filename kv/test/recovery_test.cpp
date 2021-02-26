#include "gtest/gtest.h"
#include "kv/db/db_impl.h"
#include "kv/db/filename.h"
#include "kv/db/version_set.h"
#include "kv/db/write_batch_internal.h"
#include "kv/include/db.h"
#include "kv/include/env.h"
#include "kv/include/write_batch.h"
#include "kv/util/logging.h"
#include "kv/util/testhelper.h"

namespace QuasDB
{

  class RecoveryTest : public testing::Test
  {
  public:
    RecoveryTest() : env_(Env::Default()), db_(nullptr)
    {
      dbname_ = testing::TempDir() + "/recovery_test";
      DestroyDB(dbname_, Options());
      Open();
    }

    ~RecoveryTest()
    {
      Close();
      DestroyDB(dbname_, Options());
    }

    DBImpl *dbfull() const { return reinterpret_cast<DBImpl *>(db_); }
    Env *env() const { return env_; }

    bool CanAppend()
    {
      WritableFile *tmp;
      Status s = env_->NewAppendableFile(CurrentFileName(dbname_), &tmp);
      delete tmp;
      if (s.IsNotSupportedError())
      {
        return false;
      }
      else
      {
        return true;
      }
    }

    void Close()
    {
      delete db_;
      db_ = nullptr;
    }

    Status OpenWithStatus(Options *options = nullptr)
    {
      Close();
      Options opts;
      if (options != nullptr)
      {
        opts = *options;
      }
      else
      {
        opts.reuse_logs = true; // TODO(sanjay): test both ways
        opts.create_if_missing = true;
      }
      if (opts.env == nullptr)
      {
        opts.env = env_;
      }
      return DB::Open(opts, dbname_, &db_);
    }

    void Open(Options *options = nullptr)
    {
      ASSERT_THAT(OpenWithStatus(options), QuasDB::test::IsOK());
      ASSERT_EQ(1, NumLogs());
    }

    Status Put(const std::string &k, const std::string &v)
    {
      return db_->Put(WriteOptions(), k, v);
    }

    std::string Get(const std::string &k, const Snapshot *snapshot = nullptr)
    {
      std::string result;
      Status s = db_->Get(ReadOptions(), k, &result);
      if (s.IsNotFound())
      {
        result = "NOT_FOUND";
      }
      else if (!s.ok())
      {
        result = s.ToString();
      }
      return result;
    }

    std::string ManifestFileName()
    {
      std::string current;
      EXPECT_THAT(ReadFileToString(env_, CurrentFileName(dbname_), &current), QuasDB::test::IsOK());
      size_t len = current.size();
      if (len > 0 && current[len - 1] == '\n')
      {
        current.resize(len - 1);
      }
      return dbname_ + "/" + current;
    }

    std::string LogName(uint64_t number) { return LogFileName(dbname_, number); }

    size_t RemoveLogFiles()
    {
      Close();
      std::vector<uint64_t> logs = GetFiles(kLogFile);
      for (size_t i = 0; i < logs.size(); i++)
      {
        EXPECT_THAT(env_->RemoveFile(LogName(logs[i])), QuasDB::test::IsOK()) << LogName(logs[i]);
      }
      return logs.size();
    }

    void RemoveManifestFile()
    {
      ASSERT_THAT(env_->RemoveFile(ManifestFileName()), QuasDB::test::IsOK());
    }

    uint64_t FirstLogFile() { return GetFiles(kLogFile)[0]; }

    std::vector<uint64_t> GetFiles(FileType t)
    {
      std::vector<std::string> filenames;
      EXPECT_THAT(env_->GetChildren(dbname_, &filenames), QuasDB::test::IsOK());
      std::vector<uint64_t> result;
      for (size_t i = 0; i < filenames.size(); i++)
      {
        uint64_t number;
        FileType type;
        if (ParseFileName(filenames[i], &number, &type) && type == t)
        {
          result.push_back(number);
        }
      }
      return result;
    }

    int NumLogs() { return GetFiles(kLogFile).size(); }

    int NumTables() { return GetFiles(kTableFile).size(); }

    uint64_t FileSize(const std::string &fname)
    {
      uint64_t result;
      EXPECT_THAT(env_->GetFileSize(fname, &result), QuasDB::test::IsOK()) << fname;
      return result;
    }

    void CompactMemTable() { dbfull()->TEST_CompactMemTable(); }

    // Directly construct a log file that sets key to val.
    void MakeLogFile(uint64_t lognum, SequenceNumber seq, Slice key, Slice val)
    {
      std::string fname = LogFileName(dbname_, lognum);
      WritableFile *file;
      ASSERT_THAT(env_->NewWritableFile(fname, &file), QuasDB::test::IsOK());
      log::Writer writer(file);
      WriteBatch batch;
      batch.Put(key, val);
      WriteBatchInternal::SetSequence(&batch, seq);
      ASSERT_THAT(writer.AddRecord(WriteBatchInternal::Contents(&batch)), QuasDB::test::IsOK());
      ASSERT_THAT(file->Flush(), QuasDB::test::IsOK());
      delete file;
    }

  private:
    std::string dbname_;
    Env *env_;
    DB *db_;
  };

  TEST_F(RecoveryTest, ManifestReused)
  {
    if (!CanAppend())
    {
      std::fprintf(stderr,
                   "skipping test because env does not support appending\n");
      return;
    }
    ASSERT_THAT(Put("foo", "bar"), QuasDB::test::IsOK());
    Close();
    std::string old_manifest = ManifestFileName();
    Open();
    ASSERT_EQ(old_manifest, ManifestFileName());
    ASSERT_EQ("bar", Get("foo"));
    Open();
    ASSERT_EQ(old_manifest, ManifestFileName());
    ASSERT_EQ("bar", Get("foo"));
  }

  TEST_F(RecoveryTest, LargeManifestCompacted)
  {
    if (!CanAppend())
    {
      std::fprintf(stderr,
                   "skipping test because env does not support appending\n");
      return;
    }
    ASSERT_THAT(Put("foo", "bar"), QuasDB::test::IsOK());
    Close();
    std::string old_manifest = ManifestFileName();

    // Pad with zeroes to make manifest file very big.
    {
      uint64_t len = FileSize(old_manifest);
      WritableFile *file;
      ASSERT_THAT(env()->NewAppendableFile(old_manifest, &file), QuasDB::test::IsOK());
      std::string zeroes(3 * 1048576 - static_cast<size_t>(len), 0);
      ASSERT_THAT(file->Append(zeroes), QuasDB::test::IsOK());
      ASSERT_THAT(file->Flush(), QuasDB::test::IsOK());
      delete file;
    }

    Open();
    std::string new_manifest = ManifestFileName();
    ASSERT_NE(old_manifest, new_manifest);
    ASSERT_GT(10000, FileSize(new_manifest));
    ASSERT_EQ("bar", Get("foo"));

    Open();
    ASSERT_EQ(new_manifest, ManifestFileName());
    ASSERT_EQ("bar", Get("foo"));
  }

  TEST_F(RecoveryTest, NoLogFiles)
  {
    ASSERT_THAT(Put("foo", "bar"), QuasDB::test::IsOK());
    ASSERT_EQ(1, RemoveLogFiles());
    Open();
    ASSERT_EQ("NOT_FOUND", Get("foo"));
    Open();
    ASSERT_EQ("NOT_FOUND", Get("foo"));
  }

  TEST_F(RecoveryTest, LogFileReuse)
  {
    if (!CanAppend())
    {
      std::fprintf(stderr,
                   "skipping test because env does not support appending\n");
      return;
    }
    for (int i = 0; i < 2; i++)
    {
      ASSERT_THAT(Put("foo", "bar"), QuasDB::test::IsOK());
      if (i == 0)
      {
        // Compact to ensure current log is empty
        CompactMemTable();
      }
      Close();
      ASSERT_EQ(1, NumLogs());
      uint64_t number = FirstLogFile();
      if (i == 0)
      {
        ASSERT_EQ(0, FileSize(LogName(number)));
      }
      else
      {
        ASSERT_LT(0, FileSize(LogName(number)));
      }
      Open();
      ASSERT_EQ(1, NumLogs());
      ASSERT_EQ(number, FirstLogFile()) << "did not reuse log file";
      ASSERT_EQ("bar", Get("foo"));
      Open();
      ASSERT_EQ(1, NumLogs());
      ASSERT_EQ(number, FirstLogFile()) << "did not reuse log file";
      ASSERT_EQ("bar", Get("foo"));
    }
  }

  TEST_F(RecoveryTest, MultipleMemTables)
  {
    // Make a large log.
    const int kNum = 1000;
    for (int i = 0; i < kNum; i++)
    {
      char buf[100];
      std::snprintf(buf, sizeof(buf), "%050d", i);
      ASSERT_THAT(Put(buf, buf), QuasDB::test::IsOK());
    }
    ASSERT_EQ(0, NumTables());
    Close();
    ASSERT_EQ(0, NumTables());
    ASSERT_EQ(1, NumLogs());
    uint64_t old_log_file = FirstLogFile();

    // Force creation of multiple memtables by reducing the write buffer size.
    Options opt;
    opt.reuse_logs = true;
    opt.write_buffer_size = (kNum * 100) / 2;
    Open(&opt);
    ASSERT_LE(2, NumTables());
    ASSERT_EQ(1, NumLogs());
    ASSERT_NE(old_log_file, FirstLogFile()) << "must not reuse log";
    for (int i = 0; i < kNum; i++)
    {
      char buf[100];
      std::snprintf(buf, sizeof(buf), "%050d", i);
      ASSERT_EQ(buf, Get(buf));
    }
  }

  TEST_F(RecoveryTest, MultipleLogFiles)
  {
    ASSERT_THAT(Put("foo", "bar"), QuasDB::test::IsOK());
    Close();
    ASSERT_EQ(1, NumLogs());

    // Make a bunch of uncompacted log files.
    uint64_t old_log = FirstLogFile();
    MakeLogFile(old_log + 1, 1000, "hello", "world");
    MakeLogFile(old_log + 2, 1001, "hi", "there");
    MakeLogFile(old_log + 3, 1002, "foo", "bar2");

    // Recover and check that all log files were processed.
    Open();
    ASSERT_LE(1, NumTables());
    ASSERT_EQ(1, NumLogs());
    uint64_t new_log = FirstLogFile();
    ASSERT_LE(old_log + 3, new_log);
    ASSERT_EQ("bar2", Get("foo"));
    ASSERT_EQ("world", Get("hello"));
    ASSERT_EQ("there", Get("hi"));

    // Test that previous recovery produced recoverable state.
    Open();
    ASSERT_LE(1, NumTables());
    ASSERT_EQ(1, NumLogs());
    if (CanAppend())
    {
      ASSERT_EQ(new_log, FirstLogFile());
    }
    ASSERT_EQ("bar2", Get("foo"));
    ASSERT_EQ("world", Get("hello"));
    ASSERT_EQ("there", Get("hi"));

    // Check that introducing an older log file does not cause it to be re-read.
    Close();
    MakeLogFile(old_log + 1, 2000, "hello", "stale write");
    Open();
    ASSERT_LE(1, NumTables());
    ASSERT_EQ(1, NumLogs());
    if (CanAppend())
    {
      ASSERT_EQ(new_log, FirstLogFile());
    }
    ASSERT_EQ("bar2", Get("foo"));
    ASSERT_EQ("world", Get("hello"));
    ASSERT_EQ("there", Get("hi"));
  }

  TEST_F(RecoveryTest, ManifestMissing)
  {
    ASSERT_THAT(Put("foo", "bar"), QuasDB::test::IsOK());
    Close();
    RemoveManifestFile();

    Status status = OpenWithStatus();
    ASSERT_TRUE(status.IsCorruption());
  }

} // namespace QuasDB

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
