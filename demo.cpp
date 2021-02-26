#include "kv/include/db.h"

#include <iostream>

int main()
{
  QuasDB::DB *db;
  QuasDB::Options options;
  options.create_if_missing = true;
  QuasDB::Status status = QuasDB::DB::Open(options, "/tmp/testdb", &db);
  assert(status.ok());

  std::string k1 = "QAQ";
  std::string v1 = "QwQ";
  status = db->Put(QuasDB::WriteOptions(), k1, v1);
  assert(status.ok());
  status = db->Get(QuasDB::ReadOptions(), k1, &v1);
  assert(status.ok());
  std::cout << "k1:" << k1 << "; v1:" << v1 << std::endl;

  std::string k2 = "QvQ";
  std::string v2 = "QvQ";
  status = db->Put(QuasDB::WriteOptions(), k2, v2);
  assert(status.ok());
  status = db->Get(QuasDB::ReadOptions(), k2, &v2);
  assert(status.ok());
  std::cout << "k2:" << k2 << "; v2:" << v2 << std::endl;

  status = db->Delete(QuasDB::WriteOptions(), k2);
  assert(status.ok());
  std::cout << "Delete k2.." << std::endl;
  status = db->Get(QuasDB::ReadOptions(), k2, &v2);
  if (!status.ok())
    std::cerr << "k2:" << k2 << "; " << status.ToString() << std::endl;
  else
    std::cout << "k2:" << k2 << "; v2:" << v2 << std::endl;

  delete db;
  return 0;
}