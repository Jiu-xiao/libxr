#include "libxr_def.hpp"
#include "libxr_system.hpp"

namespace LibXR {
class ConditionVar {
private:
  condition_var_handle handle_;

public:
  ConditionVar();

  ~ConditionVar();

  ErrorCode Wait(uint32_t timeout);

  void Signal();

  void Broadcast();
};
} // namespace LibXR