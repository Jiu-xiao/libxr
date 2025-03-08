#include "timer.hpp"

void LibXR::Timer::Add(TimerHandle handle) {
  ASSERT(!handle->next_);

  if (!LibXR::Timer::list_) {
    LibXR::Timer::list_ = new LibXR::List();
#ifdef LIBXR_NOT_SUPPORT_MUTI_THREAD
#else
    thread_handle_.Create<void *>(nullptr, RefreshThreadFunction,
                                  "libxr_timer_task", 512,
                                  LIBXR_TIMER_PRIORITY);
#endif
  }
  list_->Add(*handle);
}
