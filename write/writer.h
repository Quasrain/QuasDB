#pragma once

#include <string>

namespace filesystem
{
class Writer
{
  int WriteAsNewFile(std::string filename, std::string text);
  int WriteInEnd(std::string filename, std::string text);
};
}