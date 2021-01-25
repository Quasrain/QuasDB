#pragma once

#include <sys/time.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>
#include <thread>

#include "status.h"

namespace QuasDB
{
  class Env
  {
  public:
    Env();

    Env(const Env &) = delete;
    Env &operator=(const Env &) = delete;

    ~Env()
    {
      static const char msg[] = "Env singleton destroyed behavaior!\n";
      std::fwrite(msg, 1, sizeof(msg), stderr);
      std::abort();
    }

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
    //
    // The default implementation calls DeleteFile, to support legacy Env
    // implementations. Updated Env implementations must override RemoveFile and
    // ignore the existence of DeleteFile. Updated code calling into the Env API
    // must call RemoveFile instead of DeleteFile.
    Status RemoveFile(const std::string &fname);

    // A future release will remove this method.
    Status DeleteFile(const std::string &fname);

    // Create the specified directory.
    Status CreateDir(const std::string &dirname);

    // Delete the specified directory.
    //
    // The default implementation calls DeleteDir, to support legacy Env
    // implementations. Updated Env implementations must override RemoveDir and
    // ignore the existence of DeleteDir. Modern code calling into the Env API
    // must call RemoveDir instead of DeleteDir.
    //
    // A future release will remove DeleteDir and the default implementation of
    // RemoveDir.
    Status RemoveDir(const std::string &dirname);

    // DEPRECATED: Modern Env implementations should override RemoveDir instead.
    //
    // The default implementation calls RemoveDir, to support legacy Env user
    // code that calls this method on modern Env implementations. Modern Env user
    // code should call RemoveDir.
    //
    // A future release will remove this method.
    Status DeleteDir(const std::string &dirname);

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
  };

  class SequentialFile
  {
  public:
    SequentialFile() = default;

    SequentialFile(const SequentialFile &) = delete;
    SequentialFile &operator=(const SequentialFile &) = delete;
    SequentialFile(std::string filename, int fd)
        : fd_(fd), filename_(filename) {}

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

    Status Read(size_t n, Slice *result, char *scratch)
    {
      Status status;
      while (true)
      {
        ::ssize_t read_size = ::read(fd_, scratch, n);
        if (read_size < 0)
        {
          if (errno == EINTR)
          {
            continue; // Retry
          }
          status = PosixError(filename_, errno);
          break;
        }
        *result = Slice(scratch, read_size);
        break;
      }
      return status;
    }

    // Skip "n" bytes from the file. This is guaranteed to be no
    // slower that reading the same data, but may be faster.
    //
    // If end of file is reached, skipping will stop at the end of the
    // file, and Skip will return OK.
    //
    // REQUIRES: External synchronization
    Status Skip(uint64_t n)
    {
      if (::lseek(fd_, n, SEEK_CUR) == static_cast<off_t>(-1))
      {
        return PosixError(filename_, errno);
      }
      return Status::OK();
    }

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

    RandomAccessFile(std::string filename, int fd, Limiter *fd_limiter)
        : has_permanent_fd_(fd_limiter->Acquire()),
          fd_(has_permanent_fd_ ? fd : -1),
          fd_limiter_(fd_limiter),
          filename_(std::move(filename))
    {
      if (!has_permanent_fd_)
      {
        assert(fd_ == -1);
        ::close(fd); // The file will be opened on every read.
      }
    }

    ~RandomAccessFile()
    {
      close(fd_);
    }

    // Read up to "n" bytes from the file starting at "offset".
    // "scratch[0..n-1]" may be written by this routine.  Sets "*result"
    // to the data that was read (including if fewer than "n" bytes were
    // successfully read).  May set "*result" to point at data in
    // "scratch[0..n-1]", so "scratch[0..n-1]" must be live when
    // "*result" is used.  If an error was encountered, returns a non-OK
    // status.
    //
    // Safe for concurrent use by multiple threads.
    Status Read(uint64_t offset, size_t n, Slice *result,
                char *scratch) const
    {
      int fd = fd_;
      if (!has_permanent_fd_)
      {
        fd = ::open(filename_.c_str(), O_RDONLY | kOpenBaseFlags);
        if (fd < 0)
        {
          return PosixError(filename_, errno);
        }
      }

      assert(fd != -1);

      Status status;
      ssize_t read_size = ::pread(fd, scratch, n, static_cast<off_t>(offset));
      *result = Slice(scratch, (read_size < 0) ? 0 : read_size);
      if (read_size < 0)
      {
        // An error: return a non-ok status.
        status = PosixError(filename_, errno);
      }
      if (!has_permanent_fd_)
      {
        // Close the temporary file descriptor opened earlier.
        assert(fd != fd_);
        ::close(fd);
      }
      return status;
    }

  private:
    const bool has_permanent_fd_; // If false, the file is opened on every read.
    const int fd_;                // -1 if has_permanent_fd_ is false.
    Limiter *const fd_limiter_;
    const std::string filename_;
  };

  class MmapReadableFile
  {
  public:
    MmapReadableFile() = default;

    MmapReadableFile(const MmapReadableFile &) = delete;
    MmapReadableFile &operator=(const MmapReadableFile &) = delete;

    // mmap_base[0, length-1] points to the memory-mapped contents of the file. It
    // must be the result of a successful call to mmap(). This instances takes
    // over the ownership of the region.
    //
    // |mmap_limiter| must outlive this instance. The caller must have already
    // aquired the right to use one mmap region, which will be released when this
    // instance is destroyed.
    MmapReadableFile(std::string filename, char *mmap_base, size_t length,
                     Limiter *mmap_limiter)
        : mmap_base_(mmap_base),
          length_(length),
          mmap_limiter_(mmap_limiter),
          filename_(std::move(filename)) {}

    ~MmapReadableFile()
    {
      ::munmap(static_cast<void *>(mmap_base_), length_);
      mmap_limiter_->Release();
    }

    // Read up to "n" bytes from the file starting at "offset".
    // "scratch[0..n-1]" may be written by this routine.  Sets "*result"
    // to the data that was read (including if fewer than "n" bytes were
    // successfully read).  May set "*result" to point at data in
    // "scratch[0..n-1]", so "scratch[0..n-1]" must be live when
    // "*result" is used.  If an error was encountered, returns a non-OK
    // status.
    //
    // Safe for concurrent use by multiple threads.
    Status Read(uint64_t offset, size_t n, Slice *result,
                char *scratch) const
    {
      if (offset + n > length_)
      {
        *result = Slice();
        return PosixError(filename_, EINVAL);
      }
    }

  private:
    char *const mmap_base_;
    const size_t length_;
    Limiter *const mmap_limiter_;
    const std::string filename_;
  };

  class WritableFile
  {
  public:
    WritableFile() = default;

    WritableFile(const WritableFile &) = delete;
    WritableFile &operator=(const WritableFile &) = delete;

    WritableFile(std::string filename, int fd)
        : pos_(0),
          fd_(fd),
          is_manifest_(IsManifest(filename)),
          filename_(std::move(filename)),
          dirname_(Dirname(filename_)) {}

    ~WritableFile()
    {
      if (fd_ >= 0)
      {
        Close();
      }
    }

    Status Append(const Slice &data)
    {
      size_t write_size = data.size();
      const char *write_data = data.data();

      // Fit as much as possible into buffer
      size_t copy_size = std::min(write_size, kWritableFileBufferSize - pos_);
      std::memcpy(buf_ + pos_, write_data, copy_size);
      write_data += copy_size;
      write_size -= copy_size;
      pos_ += copy_size;
      if (write_size == 0)
      {
        return Status::OK();
      }

      // Can't fit in buffer, so need to do at least one write.
      Status status = FlushBuffer();
      if (!status.ok())
      {
        return status;
      }

      // Small writes go to buffer, large writes are written directly.
      if (write_size < kWritableFileBufferSize)
      {
        std::memcpy(buf_, write_data, write_size);
        pos_ = write_size;
        return Status::OK();
      }
      return WriteUnbuffered(write_data, write_size);
    }

    Status Close()
    {
      Status status = FlushBuffer();
      const int close_result = ::close(fd_);
      if (close_result < 0 && status.ok())
      {
        status = PosixError(filename_, errno);
      }
      fd_ = -1;
      return status;
    }

    Status Flush()
    {
      return FlushBuffer();
    }

    Status Sync()
    {
      // Ensure new files referred to by the manifest are in the filesystem.
      //
      // This needs to happen before the manifest file is flushed to disk, to
      // avoid crashing in a state where the manifest refers to files that are not
      // yet on disk.
      Status status = SyncDirIfManifest();
      if (!status.ok())
      {
        return status;
      }

      status = FlushBuffer();
      if (!status.ok())
      {
        return status;
      }

      return SyncFd(fd_, filename_);
    }

  private:
    Status FlushBuffer()
    {
      Status status = WriteUnbuffered(buf_, pos_);
      pos_ = 0;
      return status;
    }

    Status WriteUnbuffered(const char *data, size_t size)
    {
      while (size > 0)
      {
        ssize_t write_result = ::write(fd_, data, size);
        if (write_result < 0)
        {
          if (errno == EINTR)
          {
            continue; // Retry
          }
          return PosixError(filename_, errno);
        }
        data += write_result;
        size -= write_result;
      }
      return Status::OK();
    }

    Status SyncDirIfManifest()
    {
      Status status;
      if (!is_manifest_)
      {
        return status;
      }

      int fd = ::open(dirname_.c_str(), O_RDONLY | kOpenBaseFlags);
      if (fd < 0)
      {
        status = PosixError(dirname_, errno);
      }
      else
      {
        status = SyncFd(fd, dirname_);
        ::close(fd);
      }
      return status;
    }

    // Ensures that all the caches associated with the given file descriptor's
    // data are flushed all the way to durable media, and can withstand power
    // failures.
    //
    // The path argument is only used to populate the description string in the
    // returned Status if an error occurs.
    static Status SyncFd(int fd, const std::string &fd_path)
    {
      bool sync_success = ::fsync(fd) == 0;
      if (sync_success)
      {
        return Status::OK();
      }
      return PosixError(fd_path, errno);
    }

    // Returns the directory name in a path pointing to a file.
    //
    // Returns "." if the path does not contain any directory separator.
    static std::string Dirname(const std::string &filename)
    {
      std::string::size_type separator_pos = filename.rfind('/');
      if (separator_pos == std::string::npos)
      {
        return std::string(".");
      }
      // The filename component should not contain a path separator. If it does,
      // the splitting was done incorrectly.
      assert(filename.find('/', separator_pos + 1) == std::string::npos);

      return filename.substr(0, separator_pos);
    }

    // Extracts the file name from a path pointing to a file.
    //
    // The returned Slice points to |filename|'s data buffer, so it is only valid
    // while |filename| is alive and unchanged.
    static Slice Basename(const std::string &filename)
    {
      std::string::size_type separator_pos = filename.rfind('/');
      if (separator_pos == std::string::npos)
      {
        return Slice(filename);
      }
      // The filename component should not contain a path separator. If it does,
      // the splitting was done incorrectly.
      assert(filename.find('/', separator_pos + 1) == std::string::npos);

      return Slice(filename.data() + separator_pos + 1,
                   filename.length() - separator_pos - 1);
    }

    // True if the given file is a manifest file.
    static bool IsManifest(const std::string &filename)
    {
      return Basename(filename).starts_with("MANIFEST");
    }

    // buf_[0, pos_ - 1] contains data to be written to fd_.
    char buf_[kWritableFileBufferSize];
    size_t pos_;
    int fd_;

    const bool is_manifest_; // True if the file's name starts with MANIFEST.
    const std::string filename_;
    const std::string dirname_; // The directory of filename_.
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
      mu_.Lock();
      bool succeeded = locked_files_.insert(fname).second;
      mu_.Unlock();
      return succeeded;
    }

    void Remove(const std::string &fname)
    {
      mu_.Lock();
      locked_files_.erase(fname);
      mu_.Unlock();
    }

  private:
    std::mutex mu_;
    std::set<std::string> locked_files_;
  };

  class Logger
  {
  public:
    Logger() = default;

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    explicit Logger(std::FILE *fp) : fp_(fp)
    {
      assert(fp != nullptr);
    }

    ~Logger()
    {
      std::fclose(fp_);
    }

    // Write an entry to the log file with the specified format.
    void Logv(const char *format, std::va_list arguments)
    {
      // Record the time as close to the Logv() call as possible.
      struct ::timeval now_timeval;
      ::gettimeofday(&now_timeval, nullptr);
      const std::time_t now_seconds = now_timeval.tv_sec;
      struct std::tm now_components;
      ::localtime_r(&now_seconds, &now_components);

      // Record the thread ID.
      constexpr const int kMaxThreadIdSize = 32;
      std::ostringstream thread_stream;
      thread_stream << std::this_thread::get_id();
      std::string thread_id = thread_stream.str();
      if (thread_id.size() > kMaxThreadIdSize)
      {
        thread_id.resize(kMaxThreadIdSize);
      }

      // We first attempt to print into a stack-allocated buffer. If this attempt
      // fails, we make a second attempt with a dynamically allocated buffer.
      constexpr const int kStackBufferSize = 512;
      char stack_buffer[kStackBufferSize];
      static_assert(sizeof(stack_buffer) == static_cast<size_t>(kStackBufferSize),
                    "sizeof(char) is expected to be 1 in C++");

      int dynamic_buffer_size = 0; // Computed in the first iteration.
      for (int iteration = 0; iteration < 2; ++iteration)
      {
        const int buffer_size =
            (iteration == 0) ? kStackBufferSize : dynamic_buffer_size;
        char *const buffer =
            (iteration == 0) ? stack_buffer : new char[dynamic_buffer_size];

        // Print the header into the buffer.
        int buffer_offset = std::snprintf(
            buffer, buffer_size, "%04d/%02d/%02d-%02d:%02d:%02d.%06d %s ",
            now_components.tm_year + 1900, now_components.tm_mon + 1,
            now_components.tm_mday, now_components.tm_hour, now_components.tm_min,
            now_components.tm_sec, static_cast<int>(now_timeval.tv_usec),
            thread_id.c_str());

        // The header can be at most 28 characters (10 date + 15 time +
        // 3 delimiters) plus the thread ID, which should fit comfortably into the
        // static buffer.
        assert(buffer_offset <= 28 + kMaxThreadIdSize);
        static_assert(28 + kMaxThreadIdSize < kStackBufferSize,
                      "stack-allocated buffer may not fit the message header");
        assert(buffer_offset < buffer_size);

        // Print the message into the buffer.
        std::va_list arguments_copy;
        va_copy(arguments_copy, arguments);
        buffer_offset +=
            std::vsnprintf(buffer + buffer_offset, buffer_size - buffer_offset,
                           format, arguments_copy);
        va_end(arguments_copy);

        // The code below may append a newline at the end of the buffer, which
        // requires an extra character.
        if (buffer_offset >= buffer_size - 1)
        {
          // The message did not fit into the buffer.
          if (iteration == 0)
          {
            // Re-run the loop and use a dynamically-allocated buffer. The buffer
            // will be large enough for the log message, an extra newline and a
            // null terminator.
            dynamic_buffer_size = buffer_offset + 2;
            continue;
          }

          // The dynamically-allocated buffer was incorrectly sized. This should
          // not happen, assuming a correct implementation of std::(v)snprintf.
          // Fail in tests, recover by truncating the log message in production.
          assert(false);
          buffer_offset = buffer_size - 1;
        }
        // Add a newline if necessary.
        if (buffer[buffer_offset - 1] != '\n')
        {
          buffer[buffer_offset] = '\n';
          ++buffer_offset;
        }

        assert(buffer_offset <= buffer_size);
        std::fwrite(buffer, 1, buffer_offset, fp_);
        std::fflush(fp_);

        if (iteration != 0)
        {
          delete[] buffer;
        }
        break;
      }
    }

  private:
    std::FILE *const fp_;
  };

  int LockOrUnlock(int fd, bool lock)
  {
    errno = 0;
    struct ::flock file_lock_info;
    std::memset(&file_lock_info, 0, sizeof(file_lock_info));
    file_lock_info.l_type = (lock ? F_WRLCK : F_UNLCK);
    file_lock_info.l_whence = SEEK_SET;
    file_lock_info.l_start = 0;
    file_lock_info.l_len = 0; // Lock/unlock entire file.
    return ::fcntl(fd, F_SETLK, &file_lock_info);
  }

  class FileLock
  {
  public:
    FileLock() = default;

    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;

    FileLock(int fd, std::string filename)
        : fd_(fd), filename_(std::move(filename)) {}

    ~FileLock();

    int fd() const { return fd_; }
    const std::string &filename() const { return filename_; }

  private:
    const int fd_;
    const std::string filename_;
  };

  void Log(Logger *info_log, const char *format, ...)
  {
    __attribute__((__format__(__printf__, 2, 3)));
  }

  // A utility routine: write "data" to the named file.
  Status WriteStringToFile(Env *env, const Slice &data,
                           const std::string &fname);

  // A utility routine: read contents of named file into *data
  Status ReadFileToString(Env *env, const std::string &fname,
                          std::string *data);
} // namespace QuasDB