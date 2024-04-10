#pragma once

#include "libxr_platform.hpp"
#include "libxr_time.hpp"

namespace LibXR {
class Thread {
public:
  typedef enum {
    PRIORITY_IDLE,
    PRIORITY_LOW,
    PRIORITY_MEDIUM,
    PRIORITY_HIGH,
    PRIORITY_REALTIME
  } Priority;

  Thread(libxr_thread_handle handle) : thread_handle_(handle){};

  template <typename ArgType>
  Thread Create(ArgType arg, void (*function)(ArgType arg), const char *name,
                size_t stack_depth, Priority priority);

  static Thread Current(void);

  static uint32_t GetTime();

  static void Sleep(uint32_t milliseconds);

  static void SleepUntil(TimestampMS milliseconds);

  static void Yield();

  operator libxr_thread_handle() { return thread_handle_; }

private:
  libxr_thread_handle thread_handle_;
};
} // namespace LibXR