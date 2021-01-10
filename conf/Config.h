#pragma once

#include <iostream>
#include <string>
#include <boost/property_tree/ini_parser.hpp>
#include "error/error_all.h"

namespace QuasDB
{
  namespace Config
  {
    class Conf
    {
    public:
      Conf(std::string path);

      template <typename T>
      const int GetConfig(std::string key, T &value)
      {
        try
        {
          value = conf.get<T>(key);
          return Error::kSucc;
        }
        catch (const std::exception &e)
        {
          std::cerr << e.what() << std::endl;
          return Error::kConfKeyNotExist;
        }
      }

    private:
      boost::property_tree::ptree conf;
      std::string conf_path;
    };
  } // namespace Config
} // namespace QuasDB