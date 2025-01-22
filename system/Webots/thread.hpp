#pragma once

#include "condition_var.hpp"
#include "libxr_system.hpp"
#include "libxr_time.hpp"

extern LibXR::ConditionVar *_libxr_webots_time_notify;

namespace LibXR {
class Thread {
public:
  enum class Priority {
    IDLE,
    LOW,
    MEDIUM,
    HIGH,
    REALTIME,
    NUMBER,
  };

  Thread() {};

  Thread(libxr_thread_handle handle) : thread_handle_(handle) {};

  template <typename ArgType>
  void Create(ArgType arg, void (*function)(ArgType arg), const char *name,
              size_t stack_depth, Thread::Priority priority) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack_depth > 256) {
      pthread_attr_setstacksize(&attr, stack_depth / 1024 * 4);
    } else {
      pthread_attr_setstacksize(&attr, 1);
    }

    class ThreadBlock {
    public:
      ThreadBlock(typeof(function) fun, ArgType arg, const char *name)
          : fun_(fun), arg_(arg),
            name_(reinterpret_cast<char *>(malloc(strlen(name) + 1))) {
        strcpy(name_, name);
      }

      static void *Port(void *arg) {
        while (_libxr_webots_time_notify == NULL) {
        }
        ThreadBlock *block = static_cast<ThreadBlock *>(arg);
        const char *thread_name = block->name_;
        block->fun_(block->arg_);

        UNUSED(thread_name);

        return (void *)0;
      }

      typeof(function) fun_;
      ArgType arg_;
      char *name_;
    };

    auto block = new ThreadBlock(function, arg, name);

    pthread_create(&this->thread_handle_, &attr, block->Port, block);

    if (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR) >=
        static_cast<int>(Priority::REALTIME)) {
      struct sched_param sp;
      bzero((void *)&sp, sizeof(sp));
      sp.sched_priority =
          sched_get_priority_min(SCHED_RR) + static_cast<int>(priority);
      pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
    }
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
} // namespace LibXR