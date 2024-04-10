#include "libxr.hpp"
#include "libxr_platform_def.hpp"

namespace LibXR {
class Semaphore {
public:
  Semaphore(uint32_t init_count);

  void Post();

  template <typename ResultType, typename ArgType, typename... Args>
  void PostFromCallback(bool in_isr);

  ErrorCode Wait(uint32_t timeout);

  size_t Value();

private:
  libxr_semaphore_handle semaphore_handle_;
};
} // namespace LibXR