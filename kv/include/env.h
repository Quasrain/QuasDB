#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>
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

  class Env
  {
  public:
    Env();

    Env(const Env &) = delete;
    Env &operator=(const Env &) = delete;

    virtual ~Env();

    // Return a default environment suitable for the current operating
    // system.  Sophisticated users may wish to provide their own Env
    // implementation instead of relying on this default environment.
    //
    // The result of Default() belongs to QuasDB and must never be deleted.
    static Env *Default();

    // Create an object that sequentially reads the file with the specified name.
    // On success, stores a pointer to the new file in *result and returns OK.
    // On failure stores nullptr in *result and returns non-OK.  If the file does
    // not exist, returns a non-OK status.  Implementations should return a
    // NotFound status when the file does not exist.
    //
    // The returned file will only be accessed by one thread at a time.
    virtual Status NewSequentialFile(const std::string &fname,
                                     SequentialFile **result) = 0;

    // Create an object supporting random-access reads from the file with the
    // specified name.  On success, stores a pointer to the new file in
    // *result and returns OK.  On failure stores nullptr in *result and
    // returns non-OK.  If the file does not exist, returns a non-OK
    // status.  Implementations should return a NotFound status when the file does
    // not exist.
    //
    // The returned file may be concurrently accessed by multiple threads.
    virtual Status NewRandomAccessFile(const std::string &fname,
                                       RandomAccessFile **result) = 0;

    // Create an object that writes to a new file with the specified
    // name.  Deletes any existing file with the same name and creates a
    // new file.  On success, stores a pointer to the new file in
    // *result and returns OK.  On failure stores nullptr in *result and
    // returns non-OK.
    //
    // The returned file will only be accessed by one thread at a time.
    virtual Status NewWritableFile(const std::string &fname,
                                   WritableFile **result) = 0;

    // Create an object that either appends to an existing file, or
    // writes to a new file (if the file does not exist to begin with).
    // On success, stores a pointer to the new file in *result and
    // returns OK.  On failure stores nullptr in *result and returns
    // non-OK.
    //
    // The returned file will only be accessed by one thread at a time.
    virtual Status NewAppendableFile(const std::string &fname,
                                     WritableFile **result);

    // Returns true iff the named file exists.
    virtual bool FileExists(const std::string &fname) = 0;

    // Store in *result the names of the children of the specified directory.
    // The names are relative to "dir".
    // Original contents of *results are dropped.
    virtual Status GetChildren(const std::string &dir,
                               std::vector<std::string> *result) = 0;

    // Delete the named file.
    virtual Status RemoveFile(const std::string &fname);

    // Create the specified directory.
    virtual Status CreateDir(const std::string &dirname) = 0;

    // Delete the specified directory.
    virtual Status RemoveDir(const std::string &dirname);

    // Store the size of fname in *file_size.
    virtual Status GetFileSize(const std::string &fname, uint64_t *file_size) = 0;

    // Rename file src to target.
    virtual Status RenameFile(const std::string &src,
                              const std::string &target) = 0;

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
    virtual Status LockFile(const std::string &fname, FileLock **lock) = 0;

    // Release the lock acquired by a previous successful call to LockFile.
    // REQUIRES: lock was returned by a successful LockFile() call
    // REQUIRES: lock has not already been unlocked.
    virtual Status UnlockFile(FileLock *lock) = 0;

    // Arrange to run "(*function)(arg)" once in a background thread.
    //
    // "function" may run in an unspecified thread.  Multiple functions
    // added to the same Env may run concurrently in different threads.
    // I.e., the caller may not assume that background work items are
    // serialized.
    virtual void Schedule(void (*function)(void *arg), void *arg) = 0;

    // Start a new thread, invoking "function(arg)" within the new thread.
    // When "function(arg)" returns, the thread will be destroyed.
    virtual void StartThread(void (*function)(void *arg), void *arg) = 0;

    // *path is set to a temporary directory that can be used for testing. It may
    // or may not have just been created. The directory may or may not differ
    // between runs of the same process, but subsequent calls will return the
    // same directory.
    virtual Status GetTestDirectory(std::string *path) = 0;

    // Create and return a log file for storing informational messages.
    virtual Status NewLogger(const std::string &fname, Logger **result) = 0;

    // Returns the number of micro-seconds since some fixed point in time. Only
    // useful for computing deltas of time.
    virtual uint64_t NowMicros() = 0;

    // Sleep/delay the thread for the prescribed number of micro-seconds.
    virtual void SleepForMicroseconds(int micros) = 0;
  };

  // A file abstraction for reading sequentially through a file
  class SequentialFile
  {
  public:
    SequentialFile() = default;

    SequentialFile(const SequentialFile &) = delete;
    SequentialFile &operator=(const SequentialFile &) = delete;

    virtual ~SequentialFile();

    // Read up to "n" bytes from the file.  "scratch[0..n-1]" may be
    // written by this routine.  Sets "*result" to the data that was
    // read (including if fewer than "n" bytes were successfully read).
    // May set "*result" to point at data in "scratch[0..n-1]", so
    // "scratch[0..n-1]" must be live when "*result" is used.
    // If an error was encountered, returns a non-OK status.
    //
    // REQUIRES: External synchronization
    virtual Status Read(size_t n, Slice *result, char *scratch) = 0;

    // Skip "n" bytes from the file. This is guaranteed to be no
    // slower that reading the same data, but may be faster.
    //
    // If end of file is reached, skipping will stop at the end of the
    // file, and Skip will return OK.
    //
    // REQUIRES: External synchronization
    virtual Status Skip(uint64_t n) = 0;
  };

  // A file abstraction for randomly reading the contents of a file.
  class RandomAccessFile
  {
  public:
    RandomAccessFile() = default;

    RandomAccessFile(const RandomAccessFile &) = delete;
    RandomAccessFile &operator=(const RandomAccessFile &) = delete;

    virtual ~RandomAccessFile();

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

  // A file abstraction for sequential writing.  The implementation
  // must provide buffering since callers may append small fragments
  // at a time to the file.
  class WritableFile
  {
  public:
    WritableFile() = default;

    WritableFile(const WritableFile &) = delete;
    WritableFile &operator=(const WritableFile &) = delete;

    virtual ~WritableFile();

    virtual Status Append(const Slice &data) = 0;
    virtual Status Close() = 0;
    virtual Status Flush() = 0;
    virtual Status Sync() = 0;
  };

  // An interface for writing log messages.
  class Logger
  {
  public:
    Logger() = default;

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    virtual ~Logger();

    // Write an entry to the log file with the specified format.
    virtual void Logv(const char *format, std::va_list ap) = 0;
  };

  // Identifies a locked file.
  class FileLock
  {
  public:
    FileLock() = default;

    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;

    virtual ~FileLock();
  };

  // Log the specified data to *info_log if info_log is non-null.
  void Log(Logger *info_log, const char *format, ...)
      __attribute__((__format__(__printf__, 2, 3)));

  // A utility routine: write "data" to the named file.
  Status WriteStringToFile(Env *env, const Slice &data,
                           const std::string &fname);

  // A utility routine: read contents of named file into *data
  Status ReadFileToString(Env *env, const std::string &fname,
                          std::string *data);
}