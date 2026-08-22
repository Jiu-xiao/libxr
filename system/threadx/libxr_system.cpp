#include "libxr_system.hpp"

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "list.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "timer.hpp"

void LibXR::PlatformInit(uint32_t timer_pri, uint32_t timer_stack_depth)
{
  LibXR::Timer::priority_ = static_cast<LibXR::Thread::Priority>(timer_pri);
  LibXR::Timer::stack_depth_ = timer_stack_depth;
}
