#pragma once

#include <string>
#include "env.h"
#include "status.h"

namespace QuasDB
{
  Status DumpFile(Env *env, const std::string &fname, WritableFile *dst);
}