#include "Config.h"
#include <string.h>
#include <iostream>
#include <boost/property_tree/ini_parser.hpp>
namespace QuasDB
{
  namespace Config
  {
    Conf::Conf(std::string path)
    {
      conf_path = path;

      try
      {
        boost::property_tree::ini_parser::read_ini(conf_path, conf);
      }
      catch (const std::exception &e)
      {
        std::cerr << e.what() << std::endl;
        std::cout << "QAQ" << std::endl;
      }
    }
  } // namespace Config
} // namespace QuasDB