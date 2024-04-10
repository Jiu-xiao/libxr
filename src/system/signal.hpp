#include "libxr.hpp"
#include "libxr_def.hpp"
#include "thread.hpp"

namespace LibXR {
class Signal {
public:
  static ErrorCode Action(Thread &thread, int signal);
  static ErrorCode Wait(int signal, uint32_t timeout);
};
} // namespace LibXR