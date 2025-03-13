#pragma once

#include "libxr_system.hpp"
#include "libxr_time.hpp"

#define LIBXR_PRIORITY_STEP ((configMAX_PRIORITIES - 1) / 5)

namespace LibXR {
class Thread {
 public:
  enum class Priority : uint8_t {
    IDLE = 0,
    LOW = LIBXR_PRIORITY_STEP * 1,
    MEDIUM = LIBXR_PRIORITY_STEP * 2,
    HIGH = LIBXR_PRIORITY_STEP * 3,
    REALTIME = LIBXR_PRIORITY_STEP * 4,
    NUMBER = 5,
  };

  Thread() {};

  Thread(libxr_thread_handle handle) : thread_handle_(handle) {};

  template <typename ArgType>
  void Create(ArgType arg, void (*function)(ArgType arg), const char *name,
              size_t stack_depth, Thread::Priority priority) {
    ASSERT(configMAX_PRIORITIES >= 6);

    class ThreadBlock {
     public:
      ThreadBlock(typeof(function) fun, ArgType arg) : fun_(fun), arg_(arg) {}

      static void Port(void *arg) {
        ThreadBlock *block = static_cast<ThreadBlock *>(arg);
        block->fun_(block->arg_);
      }

      typeof(function) fun_;
      ArgType arg_;
    };

    auto block = new ThreadBlock(function, arg);

    UNUSED(block);

    ASSERT(xTaskCreate(block->Port, name, stack_depth, block,
                       static_cast<uint32_t>(priority),
                       &(this->thread_handle_)) == pdPASS);
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
