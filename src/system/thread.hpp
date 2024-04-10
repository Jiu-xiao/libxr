#include "libxr.hpp"
#include "libxr_platform_def.hpp"
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
  static Thread Create(ArgType arg, Callback<void>, const char *name,
                       size_t stack_depth, Priority priority);

  static Thread Current(void) { return Thread(pthread_self()); }

  static void Sleep(uint32_t millisecond);

  static void SleepUntil(TimestampMS millisecond);

  void Yield();

private:
  libxr_thread_handle thread_handle_;
};
} // namespace LibXR