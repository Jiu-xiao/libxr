#include "libxr_def.hpp"
#include "libxr_platform.hpp"

namespace LibXR {
class ConditionVar {
private:
  condition_var_handle handle_;

public:
  ConditionVar();

  ~ConditionVar();

  void Wait(uint32_t timeout);

  void Signal();

  void Broadcast();
};
}; // namespace LibXR