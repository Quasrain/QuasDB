#pragma once

#include <string>
#include <boost/property_tree/ini_parser.hpp>

namespace Config
{
  class Conf
  {
  public:
    Conf(std::string path);

    template<typename T>
    T GetConfig(std::string key)
    {
      return conf.get<T>(key);
    }

  private:
    boost::property_tree::ptree conf;
    std::string conf_path;
  };
} // namespace Config