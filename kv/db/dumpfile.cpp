#include "kv/include/dumpfile.h"

#include <cstdio>

#include "dbformat.h"
#include "filename.h"
#include "log_reader.h"
#include "version_edit.h"
#include "write_batch_internal.h"
#include "kv/include/env.h"
#include "kv/include/iterator.h"
#include "kv/include/options.h"
#include "kv/include/status.h"
#include "kv/include/table.h"
#include "kv/include/write_batch.h"
#include "kv/util/logging.h"

namespace QuasDB
{
  namespace
  {

    bool GuessType(const std::string &fname, FileType *type)
    {
      size_t pos = fname.rfind('/');
      std::string basename;
      if (pos == std::string::npos)
      {
        basename = fname;
      }
      else
      {
        basename = std::string(fname.data() + pos + 1, fname.size() - pos - 1);
      }
      uint64_t ignored;
      return ParseFileName(basename, &ignored, type);
    }

    // Notified when log reader encounters corruption.
    class CorruptionReporter : public log::Reader::Reporter
    {
    public:
      void Corruption(size_t bytes, const Status &status) override
      {
        std::string r = "corruption: ";
        AppendNumberTo(&r, bytes);
        r += " bytes; ";
        r += status.ToString();
        r.push_back('\n');
        dst_->Append(r);
      }

      WritableFile *dst_;
    };

    // Print contents of a log file. (*func)() is called on every record.
    Status PrintLogContents(Env *env, const std::string &fname,
                            void (*func)(uint64_t, Slice, WritableFile *),
                            WritableFile *dst)
    {
      SequentialFile *file;
      Status s = env->NewSequentialFile(fname, &file);
      if (!s.ok())
      {
        return s;
      }
      CorruptionReporter reporter;
      reporter.dst_ = dst;
      log::Reader reader(file, &reporter, true, 0);
      Slice record;
      std::string scratch;
      while (reader.ReadRecord(&record, &scratch))
      {
        (*func)(reader.LastRecordOffset(), record, dst);
      }
      delete file;
      return Status::OK();
    }

    // Called on every item found in a WriteBatch.
    class WriteBatchItemPrinter : public WriteBatch::Handler
    {
    public:
      void Put(const Slice &key, const Slice &value) override
      {
        std::string r = "  put '";
        AppendEscapedStringTo(&r, key);
        r += "' '";
        AppendEscapedStringTo(&r, value);
        r += "'\n";
        dst_->Append(r);
      }
      void Delete(const Slice &key) override
      {
        std::string r = "  del '";
        AppendEscapedStringTo(&r, key);
        r += "'\n";
        dst_->Append(r);
      }

      WritableFile *dst_;
    };

    // Called on every log record (each one of which is a WriteBatch)
    // found in a kLogFile.
    static void WriteBatchPrinter(uint64_t pos, Slice record, WritableFile *dst)
    {
      std::string r = "--- offset ";
      AppendNumberTo(&r, pos);
      r += "; ";
      if (record.size() < 12)
      {
        r += "log record length ";
        AppendNumberTo(&r, record.size());
        r += " is too small\n";
        dst->Append(r);
        return;
      }
      WriteBatch batch;
      WriteBatchInternal::SetContents(&batch, record);
      r += "sequence ";
      AppendNumberTo(&r, WriteBatchInternal::Sequence(&batch));
      r.push_back('\n');
      dst->Append(r);
      WriteBatchItemPrinter batch_item_printer;
      batch_item_printer.dst_ = dst;
      Status s = batch.Iterate(&batch_item_printer);
      if (!s.ok())
      {
        dst->Append("  error: " + s.ToString() + "\n");
      }
    }

    Status DumpLog(Env *env, const std::string &fname, WritableFile *dst)
    {
      return PrintLogContents(env, fname, WriteBatchPrinter, dst);
    }

    // Called on every log record (each one of which is a WriteBatch)
    // found in a kDescriptorFile.
    static void VersionEditPrinter(uint64_t pos, Slice record, WritableFile *dst)
    {
      std::string r = "--- offset ";
      AppendNumberTo(&r, pos);
      r += "; ";
      VersionEdit edit;
      Status s = edit.DecodeFrom(record);
      if (!s.ok())
      {
        r += s.ToString();
        r.push_back('\n');
      }
      else
      {
        r += edit.DebugString();
      }
      dst->Append(r);
    }

    Status DumpDescriptor(Env *env, const std::string &fname, WritableFile *dst)
    {
      return PrintLogContents(env, fname, VersionEditPrinter, dst);
    }

    Status DumpTable(Env *env, const std::string &fname, WritableFile *dst)
    {
      uint64_t file_size;
      RandomAccessFile *file = nullptr;
      Table *table = nullptr;
      Status s = env->GetFileSize(fname, &file_size);
      if (s.ok())
      {
        s = env->NewRandomAccessFile(fname, &file);
      }
      if (s.ok())
      {
        // We use the default comparator, which may or may not match the
        // comparator used in this database. However this should not cause
        // problems since we only use Table operations that do not require
        // any comparisons.  In particular, we do not call Seek or Prev.
        s = Table::Open(Options(), file, file_size, &table);
      }
      if (!s.ok())
      {
        delete table;
        delete file;
        return s;
      }

      ReadOptions ro;
      ro.fill_cache = false;
      Iterator *iter = table->NewIterator(ro);
      std::string r;
      for (iter->SeekToFirst(); iter->Valid(); iter->Next())
      {
        r.clear();
        ParsedInternalKey key;
        if (!ParseInternalKey(iter->key(), &key))
        {
          r = "badkey '";
          AppendEscapedStringTo(&r, iter->key());
          r += "' => '";
          AppendEscapedStringTo(&r, iter->value());
          r += "'\n";
          dst->Append(r);
        }
        else
        {
          r = "'";
          AppendEscapedStringTo(&r, key.user_key);
          r += "' @ ";
          AppendNumberTo(&r, key.sequence);
          r += " : ";
          if (key.type == kTypeDeletion)
          {
            r += "del";
          }
          else if (key.type == kTypeValue)
          {
            r += "val";
          }
          else
          {
            AppendNumberTo(&r, key.type);
          }
          r += " => '";
          AppendEscapedStringTo(&r, iter->value());
          r += "'\n";
          dst->Append(r);
        }
      }
      s = iter->status();
      if (!s.ok())
      {
        dst->Append("iterator error: " + s.ToString() + "\n");
      }

      delete iter;
      delete table;
      delete file;
      return Status::OK();
    }
  }

  Status DumpFile(Env *env, const std::string &fname, WritableFile *dst)
  {
    FileType ftype;
    if (!GuessType(fname, &ftype))
    {
      return Status::InvalidArgument(fname + ": unknown file type");
    }
    switch (ftype)
    {
    case kLogFile:
      return DumpLog(env, fname, dst);
    case kDescriptorFile:
      return DumpDescriptor(env, fname, dst);
    case kTableFile:
      return DumpTable(env, fname, dst);
    default:
      break;
    }
    return Status::InvalidArgument(fname + ": not a dump-able file type");
  }
}