#pragma once

#include <string>

namespace FileSystem
{
  class Writer
  {
    int WriteAsNewFile(std::string filename, std::string text);
    int WriteInEnd(std::string filename, std::string text);
  };
} // namespace FileSystem