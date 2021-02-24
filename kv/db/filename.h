#pragma once

#include <cstdint>
#include <string>

#include "kv/include/slice.h"
#include "kv/include/status.h"

namespace QuasDB
{
  class Env;

  enum FileType
  {
    kLogFile,
    kDBLockFile,
    kTableFile,
    kDescriptorFile,
    kCurrentFile,
    kTempFile,
    kInfoLogFile
  };

  std::string LogFileName(const std::string &dbname, uint64_t number);

  std::string TableFileName(const std::string &dbname, uint64_t number);

  std::string SSTTableFileName(const std::string &dbname, uint64_t number);

  std::string DescriptorFileName(const std::string &dbname, uint64_t number);

  std::string CurrentFileName(const std::string &dbname);

  std::string LockFileName(const std::string &dbname);

  std::string TempFileName(const std::string &dbname, uint64_t number);

  std::string InfoLogFileName(const std::string &dbname);

  std::string OldInfoLogFileName(const std::string &dbname);

  bool ParseFileName(const std::string &filename, uint64_t *number,
                     FileType *type);

  Status SetCurrentFile(Env *env, const std::string &dbname,
                        uint64_t descriptor_number);

} // namespace QuasDB