#pragma once

#include <string>

namespace http
{
  namespace server
  {
    struct header
    {
      std::string name;
      std::string value;
    };
  }
}