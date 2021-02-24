#pragma once

#include <mutex>

namespace QuasDB
{

  // Helper class that locks a mutex on construction and unlocks the mutex when
  // the destructor of the MutexLock object is invoked.
  //
  // Typical usage:
  //
  //   void MyClass::MyMethod() {
  //     MutexLock l(&mu_);       // mu_ is an instance variable
  //     ... some complex code, possibly with multiple return paths ...
  //   }
  class MutexLock
  {
  public:
    explicit MutexLock(std::mutex *mu) : mu_(mu)
    {
      this->mu_->lock();
    }

    ~MutexLock() { this->mu_->unlock(); }

    MutexLock(const MutexLock &) = delete;
    MutexLock &operator=(const MutexLock &) = delete;

  private:
    std::mutex *const mu_;
  };
} // namespace QuasDB