#pragma once

#include "libxr_def.hpp"
#include "libxr_platform.hpp"
#include "libxr_time.hpp"

namespace LibXR {
class Thread {
 public:
  enum class Priority {
    IDLE = 0,
    LOW = 0,
    MEDIUM = 0,
    HIGH = 0,
    REALTIME = 0,
    NUMBER = 1,
  };

  Thread(){};

  Thread(libxr_thread_handle handle) : thread_handle_(handle){};

  template <typename ArgType>
  void Create(ArgType arg, void (*function)(ArgType arg), const char *name,
              size_t stack_depth, Thread::Priority priority) {
    UNUSED(name);
    UNUSED(stack_depth);
    UNUSED(priority);

    static bool created = false;
    ASSERT(created == false);
    created = true;
    UNUSED(created);

    function(arg);
  }

  static Thread Current(void);

  static uint32_t GetTime();

  static void Sleep(uint32_t milliseconds);

  static void SleepUntil(TimestampMS &last_waskup_time, uint32_t time_to_sleep);

  static void Yield();

  operator libxr_thread_handle() { return thread_handle_; }

 private:
  libxr_thread_handle thread_handle_;
};

}  // namespace LibXR
