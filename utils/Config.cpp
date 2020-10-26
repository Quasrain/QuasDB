#include "Config.h"

#include <string.h>
#include <iostream>
#include <boost/property_tree/ini_parser.hpp>

namespace Config
{
  Conf::Conf(std::string path)
  {
    conf_path = path;

    boost::property_tree::ini_parser::read_ini(conf_path, conf);
  }
} // namespace Config