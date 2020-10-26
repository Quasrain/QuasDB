#include "writer.h"

#include <string>
#include <fstream>
#include <iostream>
#include <boost/interprocess/sync/file_lock.hpp>

namespace filesystem
{
  int Writer::WriteInEnd(std::string filename, std::string text)
  {
    std::ofstream file(filename, std::ios::app);
    if (!file.is_open() || file.bad())
    {
      std::cout << "Open file failed" << std::endl;
      return -1;
    }

    boost::interprocess::file_lock f_lock(filename.c_str());
    f_lock.lock();
    file << text;
    file.flush();
    f_lock.unlock();
    return 0;
  }

  int Writer::WriteAsNewFile(std::string filename, std::string text)
  {
    std::ofstream file(filename, std::ios::out);
    if (!file.is_open() || file.bad())
    {
      std::cout << "Open file failed" << std::endl;
      return -1;
    }

    boost::interprocess::file_lock f_lock(filename.c_str());
    f_lock.lock();
    file << text;
    file.flush();
    f_lock.unlock();
    return 0;
  }
}