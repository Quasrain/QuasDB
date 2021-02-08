#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <unistd.h>
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

  // Set by EnvPosixTestHelper::SetReadOnlyMMapLimit() and MaxOpenFiles().
  static int g_open_read_only_file_limit = -1;

  // Up to 1000 mmap regions for 64-bit binaries; none for 32-bit.
  static constexpr const int kDefaultMmapLimit = (sizeof(void *) >= 8) ? 1000 : 0;

  // Can be set using EnvPosixTestHelper::SetReadOnlyMMapLimit().
  static int g_mmap_limit = kDefaultMmapLimit;

  static constexpr const int kOpenBaseFlags = 0;

  static constexpr const size_t kWritableFileBufferSize = 65536;

  // Helper class to limit resource usage to avoid exhaustion.
  // Currently used to limit read-only file descriptors and mmap file usage
  // so that we do not run out of file descriptors or virtual memory, or run into
  // kernel performance problems for very large databases.
  class Limiter
  {
  public:
    // Limit maximum number of resources to |max_acquires|.
    Limiter(int max_acquires) : acquires_allowed_(max_acquires) {}

    Limiter(const Limiter &) = delete;
    Limiter operator=(const Limiter &) = delete;

    // If another resource is available, acquire it and return true.
    // Else return false.
    bool Acquire()
    {
      int old_acquires_allowed =
          acquires_allowed_.fetch_sub(1, std::memory_order_relaxed);

      if (old_acquires_allowed > 0)
        return true;

      acquires_allowed_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    // Release a resource acquired by a previous call to Acquire() that returned
    // true.
    void Release() { acquires_allowed_.fetch_add(1, std::memory_order_relaxed); }

  private:
    // The number of available resources.
    //
    // This is a counter and is not tied to the invariants of any other class, so
    // it can be operated on safely using std::memory_order_relaxed.
    std::atomic<int> acquires_allowed_;
  };

  // Tracks the files locked by PosixEnv::LockFile().
  //
  // We maintain a separate set instead of relying on fcntl(F_SETLK) because
  // fcntl(F_SETLK) does not provide any protection against multiple uses from the
  // same process.
  //
  // Instances are thread-safe because all member data is guarded by a mutex.
  class LockTable
  {
  public:
    bool Insert(const std::string &fname)
    {
      mu_.lock();
      bool succeeded = locked_files_.insert(fname).second;
      mu_.unlock();
      return succeeded;
    }
    void Remove(const std::string &fname)
    {
      mu_.lock();
      locked_files_.erase(fname);
      mu_.unlock();
    }

  private:
    std::mutex mu_;
    std::set<std::string> locked_files_;
  };

  class Env
  {
  public:
    Env();

    Env(const Env &) = delete;
    Env &operator=(const Env &) = delete;

    ~Env()
    {
      static const char msg[] =
          "PosixEnv singleton destroyed. Unsupported behavior!\n";
      std::fwrite(msg, 1, sizeof(msg), stderr);
      std::abort();
    }

    // Return a default environment suitable for the current operating
    // system.  Sophisticated users may wish to provide their own Env
    // implementation instead of relying on this default environment.
    //
    // The result of Default() belongs to leveldb and must never be deleted.
    static Env *Default();

    // Create an object that sequentially reads the file with the specified name.
    // On success, stores a pointer to the new file in *result and returns OK.
    // On failure stores nullptr in *result and returns non-OK.  If the file does
    // not exist, returns a non-OK status.  Implementations should return a
    // NotFound status when the file does not exist.
    //
    // The returned file will only be accessed by one thread at a time.
    Status NewSequentialFile(const std::string &fname,
                             SequentialFile **result);

    // Create an object supporting random-access reads from the file with the
    // specified name.  On success, stores a pointer to the new file in
    // *result and returns OK.  On failure stores nullptr in *result and
    // returns non-OK.  If the file does not exist, returns a non-OK
    // status.  Implementations should return a NotFound status when the file does
    // not exist.
    //
    // The returned file may be concurrently accessed by multiple threads.
    Status NewRandomAccessFile(const std::string &fname,
                               RandomAccessFile **result);

    // Create an object that writes to a new file with the specified
    // name.  Deletes any existing file with the same name and creates a
    // new file.  On success, stores a pointer to the new file in
    // *result and returns OK.  On failure stores nullptr in *result and
    // returns non-OK.
    //
    // The returned file will only be accessed by one thread at a time.
    Status NewWritableFile(const std::string &fname,
                           WritableFile **result);

    // Create an object that either appends to an existing file, or
    // writes to a new file (if the file does not exist to begin with).
    // On success, stores a pointer to the new file in *result and
    // returns OK.  On failure stores nullptr in *result and returns
    // non-OK.
    //
    // The returned file will only be accessed by one thread at a time.
    //
    // May return an IsNotSupportedError error if this Env does
    // not allow appending to an existing file.  Users of Env (including
    // the leveldb implementation) must be prepared to deal with
    // an Env that does not support appending.
    Status NewAppendableFile(const std::string &fname,
                             WritableFile **result);

    // Returns true iff the named file exists.
    bool FileExists(const std::string &fname);

    // Store in *result the names of the children of the specified directory.
    // The names are relative to "dir".
    // Original contents of *results are dropped.
    Status GetChildren(const std::string &dir,
                       std::vector<std::string> *result);

    // Delete the named file.
    Status RemoveFile(const std::string &fname);

    // Create the specified directory.
    Status CreateDir(const std::string &dirname);

    // Delete the specified directory.
    Status RemoveDir(const std::string &dirname);

    // Store the size of fname in *file_size.
    Status GetFileSize(const std::string &fname, uint64_t *file_size);

    // Rename file src to target.
    Status RenameFile(const std::string &src,
                      const std::string &target);

    // Lock the specified file.  Used to prevent concurrent access to
    // the same db by multiple processes.  On failure, stores nullptr in
    // *lock and returns non-OK.
    //
    // On success, stores a pointer to the object that represents the
    // acquired lock in *lock and returns OK.  The caller should call
    // UnlockFile(*lock) to release the lock.  If the process exits,
    // the lock will be automatically released.
    //
    // If somebody else already holds the lock, finishes immediately
    // with a failure.  I.e., this call does not wait for existing locks
    // to go away.
    //
    // May create the named file if it does not already exist.
    Status LockFile(const std::string &fname, FileLock **lock);

    // Release the lock acquired by a previous successful call to LockFile.
    // REQUIRES: lock was returned by a successful LockFile() call
    // REQUIRES: lock has not already been unlocked.
    Status UnlockFile(FileLock *lock);

    // Arrange to run "(*function)(arg)" once in a background thread.
    //
    // "function" may run in an unspecified thread.  Multiple functions
    // added to the same Env may run concurrently in different threads.
    // I.e., the caller may not assume that background work items are
    // serialized.
    void Schedule(void (*function)(void *arg), void *arg);

    // Start a new thread, invoking "function(arg)" within the new thread.
    // When "function(arg)" returns, the thread will be destroyed.
    void StartThread(void (*function)(void *arg), void *arg);

    // *path is set to a temporary directory that can be used for testing. It may
    // or may not have just been created. The directory may or may not differ
    // between runs of the same process, but subsequent calls will return the
    // same directory.
    Status GetTestDirectory(std::string *path);

    // Create and return a log file for storing informational messages.
    Status NewLogger(const std::string &fname, Logger **result);

    // Returns the number of micro-seconds since some fixed point in time. Only
    // useful for computing deltas of time.
    uint64_t NowMicros();

    // Sleep/delay the thread for the prescribed number of micro-seconds.
    void SleepForMicroseconds(int micros);

  private:
    void BackgroundThreadMain();

    static void BackgroundThreadEntryPoint(Env *env)
    {
      env->BackgroundThreadMain();
    }

    // Stores the work item data in a Schedule() call.
    //
    // Instances are constructed on the thread calling Schedule() and used on the
    // background thread.
    //
    // This structure is thread-safe beacuse it is immutable.
    struct BackgroundWorkItem
    {
      explicit BackgroundWorkItem(void (*function)(void *arg), void *arg)
          : function(function), arg(arg) {}

      void (*const function)(void *);
      void *const arg;
    };

    std::mutex background_work_mutex_;
    std::condition_variable background_work_cv_;
    bool started_background_thread_;

    std::queue<BackgroundWorkItem> background_work_queue_;

    LockTable locks_;      // Thread-safe.
    Limiter mmap_limiter_; // Thread-safe.
    Limiter fd_limiter_;   // Thread-safe.
  };

  class SequentialFile
  {
  public:
    SequentialFile() = default;

    SequentialFile(const SequentialFile &) = delete;

    SequentialFile(std::string filename, int fd)
        : fd_(fd), filename_(filename) {}

    SequentialFile &operator=(const SequentialFile &) = delete;

    ~SequentialFile()
    {
      close(fd_);
    }

    // Read up to "n" bytes from the file.  "scratch[0..n-1]" may be
    // written by this routine.  Sets "*result" to the data that was
    // read (including if fewer than "n" bytes were successfully read).
    // May set "*result" to point at data in "scratch[0..n-1]", so
    // "scratch[0..n-1]" must be live when "*result" is used.
    // If an error was encountered, returns a non-OK status.
    //
    // REQUIRES: External synchronization
    Status Read(size_t n, Slice *result, char *scratch);

    // Skip "n" bytes from the file. This is guaranteed to be no
    // slower that reading the same data, but may be faster.
    //
    // If end of file is reached, skipping will stop at the end of the
    // file, and Skip will return OK.
    //
    // REQUIRES: External synchronization
    Status Skip(uint64_t n);

  private:
    const int fd_;
    const std::string filename_;
  };

  class RandomAccessFile
  {
  public:
    RandomAccessFile() = default;

    RandomAccessFile(const RandomAccessFile &) = delete;
    RandomAccessFile &operator=(const RandomAccessFile &) = delete;

    ~RandomAccessFile() = default;

    // Read up to "n" bytes from the file starting at "offset".
    // "scratch[0..n-1]" may be written by this routine.  Sets "*result"
    // to the data that was read (including if fewer than "n" bytes were
    // successfully read).  May set "*result" to point at data in
    // "scratch[0..n-1]", so "scratch[0..n-1]" must be live when
    // "*result" is used.  If an error was encountered, returns a non-OK
    // status.
    //
    // Safe for concurrent use by multiple threads.
    virtual Status Read(uint64_t offset, size_t n, Slice *result,
                        char *scratch) const = 0;
  };

  class WritableFile
  {
  public:
    WritableFile() = default;

    WritableFile(const WritableFile &) = delete;

    WritableFile(std::string filename, int fd)
        : pos_(0),
          fd_(fd),
          is_manifest_(IsManifest(filename)),
          filename_(std::move(filename)),
          dirname_(Dirname(filename_)) {}
    WritableFile &operator=(const WritableFile &) = delete;

    ~WritableFile()
    {
      if (fd_ >= 0)
      {
        // Ignoring any potential errors
        Close();
      }
    }

    Status Append(const Slice &data);
    Status Close();
    Status Flush();
    Status Sync();

  private:
    Status FlushBuffer();
    Status WriteUnbuffered(const char *data, size_t size);
    Status SyncDirIfManifest();
    static Status SyncFd(int fd, const std::string &fd_path);
    static std::string Dirname(const std::string &filename);
    static Slice Basename(const std::string &filename);
    static bool IsManifest(const std::string &filename);

    // buf_[0, pos_ - 1] contains data to be written to fd_.
    char buf_[kWritableFileBufferSize];
    size_t pos_;
    int fd_;

    const bool is_manifest_; // True if the file's name starts with MANIFEST.
    const std::string filename_;
    const std::string dirname_; // The directory of filename_.
  };

  class Logger
  {
  public:
    Logger() = default;
    // Creates a logger that writes to the given file.
    //
    // The PosixLogger instance takes ownership of the file handle.
    explicit Logger(std::FILE *fp) : fp_(fp) { assert(fp != nullptr); }
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    ~Logger()
    {
      std::fclose(fp_);
    }

    // Write an entry to the log file with the specified format.
    void Logv(const char *format, std::va_list ap);

  private:
    std::FILE *const fp_;
  };

  class FileLock
  {
  public:
    FileLock() = default;

    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;
    FileLock(int fd, std::string filename)
        : fd_(fd), filename_(std::move(filename)) {}

    virtual ~FileLock() = default;
    int fd() const { return fd_; }
    const std::string &filename() const { return filename_; }

  private:
    const int fd_;
    const std::string filename_;
  };

  void Log(Logger *info_log, const char *format, ...)
      __attribute__((__format__(__printf__, 2, 3)));

  // A utility routine: write "data" to the named file.
  Status WriteStringToFile(Env *env, const Slice &data,
                           const std::string &fname);

  // A utility routine: read contents of named file into *data
  Status ReadFileToString(Env *env, const std::string &fname,
                          std::string *data);
} // namespace QuasDB