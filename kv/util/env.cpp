#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <sstream>
#include <thread>
#include <type_traits>
#include <utility>

#include "kv/include/env.h"
#include "kv/include/slice.h"
#include "kv/include/status.h"

namespace QuasDB
{
  Status PosixError(const std::string &context, int error_number)
  {
    if (error_number == ENOENT)
    {
      return Status::NotFound(context, std::strerror(error_number));
    }
    else
    {
      return Status::IOError(context, std::strerror(error_number));
    }
  }

  Status SequentialFile::Read(size_t n, Slice *result, char *scratch)
  {
    Status status;
    while (true)
    {
      ::ssize_t read_size = ::read(fd_, scratch, n);
      if (read_size < 0)
      { // Read error.
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

  Status SequentialFile::Skip(uint64_t n)
  {
    if (::lseek(fd_, n, SEEK_CUR) == static_cast<off_t>(-1))
    {
      return PosixError(filename_, errno);
    }
    return Status::OK();
  }

  // Implements random read access in a file using pread().
  //
  // Instances of this class are thread-safe, as required by the RandomAccessFile
  // API. Instances are immutable and Read() only calls thread-safe library
  // functions.
  class PosixRandomAccessFile final : public RandomAccessFile
  {
  public:
    // The new instance takes ownership of |fd|. |fd_limiter| must outlive this
    // instance, and will be used to determine if .
    PosixRandomAccessFile(std::string filename, int fd, Limiter *fd_limiter)
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

    ~PosixRandomAccessFile() override
    {
      if (has_permanent_fd_)
      {
        assert(fd_ != -1);
        ::close(fd_);
        fd_limiter_->Release();
      }
    }

    Status Read(uint64_t offset, size_t n, Slice *result,
                char *scratch) const override
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

  // Implements random read access in a file using mmap().
  //
  // Instances of this class are thread-safe, as required by the RandomAccessFile
  // API. Instances are immutable and Read() only calls thread-safe library
  // functions.
  class PosixMmapReadableFile final : public RandomAccessFile
  {
  public:
    // mmap_base[0, length-1] points to the memory-mapped contents of the file. It
    // must be the result of a successful call to mmap(). This instances takes
    // over the ownership of the region.
    //
    // |mmap_limiter| must outlive this instance. The caller must have already
    // aquired the right to use one mmap region, which will be released when this
    // instance is destroyed.
    PosixMmapReadableFile(std::string filename, char *mmap_base, size_t length,
                          Limiter *mmap_limiter)
        : mmap_base_(mmap_base),
          length_(length),
          mmap_limiter_(mmap_limiter),
          filename_(std::move(filename)) {}

    ~PosixMmapReadableFile() override
    {
      ::munmap(static_cast<void *>(mmap_base_), length_);
      mmap_limiter_->Release();
    }

    Status Read(uint64_t offset, size_t n, Slice *result,
                char *scratch) const override
    {
      if (offset + n > length_)
      {
        *result = Slice();
        return PosixError(filename_, EINVAL);
      }

      *result = Slice(mmap_base_ + offset, n);
      return Status::OK();
    }

  private:
    char *const mmap_base_;
    const size_t length_;
    Limiter *const mmap_limiter_;
    const std::string filename_;
  };

  Status WritableFile::Append(const Slice &data)
  {
    size_t write_size = data.size();
    const char *write_data = data.data();

    // Fit as much as possible into buffer.
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

  Status WritableFile::Close()
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

  Status WritableFile::Flush()
  {
    return FlushBuffer();
  }

  Status WritableFile::Sync()
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

  Status WritableFile::FlushBuffer()
  {
    Status status = WriteUnbuffered(buf_, pos_);
    pos_ = 0;
    return status;
  }

  Status WritableFile::SyncDirIfManifest()
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

  Status WritableFile::SyncFd(int fd, const std::string &fd_path)
  {
    bool sync_success = ::fsync(fd) == 0;
    if (sync_success)
    {
      return Status::OK();
    }
    return PosixError(fd_path, errno);
  }

  std::string WritableFile::Dirname(const std::string &filename)
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

  Slice WritableFile::Basename(const std::string &filename)
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

  bool WritableFile::IsManifest(const std::string &filename)
  {
    return Basename(filename).starts_with("MANIFEST");
  }

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

  Status Env::NewSequentialFile(const std::string &filename,
                                SequentialFile **result)
  {
    int fd = ::open(filename.c_str(), O_RDONLY | kOpenBaseFlags);
    if (fd < 0)
    {
      *result = nullptr;
      return PosixError(filename, errno);
    }

    *result = new SequentialFile(filename, fd);
    return Status::OK();
  }

  Status Env::NewRandomAccessFile(const std::string &filename,
                                  RandomAccessFile **result)
  {
    *result = nullptr;
    int fd = ::open(filename.c_str(), O_RDONLY | kOpenBaseFlags);
    if (fd < 0)
    {
      return PosixError(filename, errno);
    }

    if (!mmap_limiter_.Acquire())
    {
      *result = new PosixRandomAccessFile(filename, fd, &fd_limiter_);
      return Status::OK();
    }

    uint64_t file_size;
    Status status = GetFileSize(filename, &file_size);
    if (status.ok())
    {
      void *mmap_base =
          ::mmap(/*addr=*/nullptr, file_size, PROT_READ, MAP_SHARED, fd, 0);
      if (mmap_base != MAP_FAILED)
      {
        *result = new PosixMmapReadableFile(filename,
                                            reinterpret_cast<char *>(mmap_base),
                                            file_size, &mmap_limiter_);
      }
      else
      {
        status = PosixError(filename, errno);
      }
    }
    ::close(fd);
    if (!status.ok())
    {
      mmap_limiter_.Release();
    }
    return status;
  }

  Status Env::NewWritableFile(const std::string &filename,
                              WritableFile **result)
  {
    int fd = ::open(filename.c_str(),
                    O_TRUNC | O_WRONLY | O_CREAT | kOpenBaseFlags, 0644);
    if (fd < 0)
    {
      *result = nullptr;
      return PosixError(filename, errno);
    }

    *result = new WritableFile(filename, fd);
    return Status::OK();
  }

  Status NewAppendableFile(const std::string &filename,
                           WritableFile **result)
  {
    int fd = ::open(filename.c_str(),
                    O_APPEND | O_WRONLY | O_CREAT | kOpenBaseFlags, 0644);
    if (fd < 0)
    {
      *result = nullptr;
      return PosixError(filename, errno);
    }

    *result = new WritableFile(filename, fd);
    return Status::OK();
  }

  bool Env::FileExists(const std::string &filename)
  {
    return ::access(filename.c_str(), F_OK) == 0;
  }

  Status Env::GetChildren(const std::string &directory_path,
                          std::vector<std::string> *result)
  {
    result->clear();
    ::DIR *dir = ::opendir(directory_path.c_str());
    if (dir == nullptr)
    {
      return PosixError(directory_path, errno);
    }
    struct ::dirent *entry;
    while ((entry = ::readdir(dir)) != nullptr)
    {
      result->emplace_back(entry->d_name);
    }
    ::closedir(dir);
    return Status::OK();
  }

  Status Env::RemoveFile(const std::string &filename)
  {
    if (::unlink(filename.c_str()) != 0)
    {
      return PosixError(filename, errno);
    }
    return Status::OK();
  }

  Status Env::CreateDir(const std::string &dirname)
  {
    if (::mkdir(dirname.c_str(), 0755) != 0)
    {
      return PosixError(dirname, errno);
    }
    return Status::OK();
  }

  Status Env::RemoveDir(const std::string &dirname)
  {
    if (::rmdir(dirname.c_str()) != 0)
    {
      return PosixError(dirname, errno);
    }
    return Status::OK();
  }

  Status Env::GetFileSize(const std::string &filename, uint64_t *size)
  {
    struct ::stat file_stat;
    if (::stat(filename.c_str(), &file_stat) != 0)
    {
      *size = 0;
      return PosixError(filename, errno);
    }
    *size = file_stat.st_size;
    return Status::OK();
  }

  Status Env::RenameFile(const std::string &from, const std::string &to)
  {
    if (std::rename(from.c_str(), to.c_str()) != 0)
    {
      return PosixError(from, errno);
    }
    return Status::OK();
  }

  Status Env::LockFile(const std::string &filename, FileLock **lock)
  {
    *lock = nullptr;

    int fd = ::open(filename.c_str(), O_RDWR | O_CREAT | kOpenBaseFlags, 0644);
    if (fd < 0)
    {
      return PosixError(filename, errno);
    }

    if (!locks_.Insert(filename))
    {
      ::close(fd);
      return Status::IOError("lock " + filename, "already held by process");
    }

    if (LockOrUnlock(fd, true) == -1)
    {
      int lock_errno = errno;
      ::close(fd);
      locks_.Remove(filename);
      return PosixError("lock " + filename, lock_errno);
    }

    *lock = new FileLock(fd, filename);
    return Status::OK();
  }

  Status Env::UnlockFile(FileLock *lock)
  {
    FileLock *posix_file_lock = static_cast<FileLock *>(lock);
    if (LockOrUnlock(posix_file_lock->fd(), false) == -1)
    {
      return PosixError("unlock " + posix_file_lock->filename(), errno);
    }
    locks_.Remove(posix_file_lock->filename());
    ::close(posix_file_lock->fd());
    delete posix_file_lock;
    return Status::OK();
  }

  void Env::StartThread(void (*thread_main)(void *thread_main_arg),
                        void *thread_main_arg)
  {
    std::thread new_thread(thread_main, thread_main_arg);
    new_thread.detach();
  }

  Status Env::GetTestDirectory(std::string *result)
  {
    const char *env = std::getenv("TEST_TMPDIR");
    if (env && env[0] != '\0')
    {
      *result = env;
    }
    else
    {
      char buf[100];
      std::snprintf(buf, sizeof(buf), "/tmp/leveldbtest-%d",
                    static_cast<int>(::geteuid()));
      *result = buf;
    }

    // The CreateDir status is ignored because the directory may already exist.
    CreateDir(*result);

    return Status::OK();
  }

  Status Env::NewLogger(const std::string &filename, Logger **result)
  {
    int fd = ::open(filename.c_str(),
                    O_APPEND | O_WRONLY | O_CREAT | kOpenBaseFlags, 0644);
    if (fd < 0)
    {
      *result = nullptr;
      return PosixError(filename, errno);
    }

    std::FILE *fp = ::fdopen(fd, "w");
    if (fp == nullptr)
    {
      ::close(fd);
      *result = nullptr;
      return PosixError(filename, errno);
    }
    else
    {
      *result = new Logger(fp);
      return Status::OK();
    }
  }

  uint64_t Env::NowMicros()
  {
    static constexpr uint64_t kUsecondsPerSecond = 1000000;
    struct ::timeval tv;
    ::gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * kUsecondsPerSecond + tv.tv_usec;
  }

  void Env::SleepForMicroseconds(int micros)
  {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
  }

  void Logger::Logv(const char *format, std::va_list arguments)
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

  // Return the maximum number of concurrent mmaps.
  int MaxMmaps() { return g_mmap_limit; }

  // Return the maximum number of read-only files to keep open.
  int MaxOpenFiles()
  {
    if (g_open_read_only_file_limit >= 0)
    {
      return g_open_read_only_file_limit;
    }
    struct ::rlimit rlim;
    if (::getrlimit(RLIMIT_NOFILE, &rlim))
    {
      // getrlimit failed, fallback to hard-coded default.
      g_open_read_only_file_limit = 50;
    }
    else if (rlim.rlim_cur == RLIM_INFINITY)
    {
      g_open_read_only_file_limit = std::numeric_limits<int>::max();
    }
    else
    {
      // Allow use of 20% of available file descriptors for read-only files.
      g_open_read_only_file_limit = rlim.rlim_cur / 5;
    }
    return g_open_read_only_file_limit;
  }

  Env::Env()
      : started_background_thread_(false),
        mmap_limiter_(MaxMmaps()),
        fd_limiter_(MaxOpenFiles()) {}

  void Env::Schedule(
      void (*background_work_function)(void *background_work_arg),
      void *background_work_arg)
  {
    background_work_mutex_.lock();

    // Start the background thread, if we haven't done so already.
    if (!started_background_thread_)
    {
      started_background_thread_ = true;
      std::thread background_thread(Env::BackgroundThreadEntryPoint, this);
      background_thread.detach();
    }

    // If the queue is empty, the background thread may be waiting for work.
    if (background_work_queue_.empty())
    {
      background_work_cv_.notify_one();
    }

    background_work_queue_.emplace(background_work_function, background_work_arg);
    background_work_mutex_.unlock();
  }

  void Env::BackgroundThreadMain()
  {
    while (true)
    {
      background_work_mutex_.lock();

      // Wait until there is work to be done.
      while (background_work_queue_.empty())
      {
        std::unique_lock<std::mutex> lock(background_work_mutex_, std::adopt_lock);
        background_work_cv_.wait(lock);
        lock.release();
      }

      assert(!background_work_queue_.empty());
      auto background_work_function = background_work_queue_.front().function;
      void *background_work_arg = background_work_queue_.front().arg;
      background_work_queue_.pop();

      background_work_mutex_.unlock();
      background_work_function(background_work_arg);
    }
  }

  Env *Env::Default()
  {
    static Env env;
    return &env;
  }

  void Log(Logger *info_log, const char *format, ...)
  {
    if (info_log != nullptr)
    {
      std::va_list ap;
      va_start(ap, format);
      info_log->Logv(format, ap);
      va_end(ap);
    }
  }

  static Status DoWriteStringToFile(Env *env, const Slice &data,
                                    const std::string &fname, bool should_sync)
  {
    WritableFile *file;
    Status s = env->NewWritableFile(fname, &file);
    if (!s.ok())
    {
      return s;
    }
    s = file->Append(data);
    if (s.ok() && should_sync)
    {
      s = file->Sync();
    }
    if (s.ok())
    {
      s = file->Close();
    }
    delete file; // Will auto-close if we did not close above
    if (!s.ok())
    {
      env->RemoveFile(fname);
    }
    return s;
  }

  Status WriteStringToFile(Env *env, const Slice &data,
                           const std::string &fname)
  {
    return DoWriteStringToFile(env, data, fname, false);
  }

  Status WriteStringToFileSync(Env *env, const Slice &data,
                               const std::string &fname)
  {
    return DoWriteStringToFile(env, data, fname, true);
  }

  Status ReadFileToString(Env *env, const std::string &fname, std::string *data)
  {
    data->clear();
    SequentialFile *file;
    Status s = env->NewSequentialFile(fname, &file);
    if (!s.ok())
    {
      return s;
    }
    static const int kBufferSize = 8192;
    char *space = new char[kBufferSize];
    while (true)
    {
      Slice fragment;
      s = file->Read(kBufferSize, &fragment, space);
      if (!s.ok())
      {
        break;
      }
      data->append(fragment.data(), fragment.size());
      if (fragment.empty())
      {
        break;
      }
    }
    delete[] space;
    delete file;
    return s;
  }

} // namespace QuasDB
