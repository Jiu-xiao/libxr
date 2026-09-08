#include "cdc_to_uart.hpp"
#include "linux_shared_topic.hpp"
#include "object_pool.hpp"

#if defined(LIBXR_DEV_ASSERT_BUILD) || defined(LIBXR_DEBUG_BUILD)
#error "This target checks public headers with product assertions disabled."
#endif

template <typename Pool>
void ExercisePool()
{
  Pool pool(4);
  typename Pool::Handle handle;
  (void)pool.Acquire(handle);
  handle.Reset();
}

void TestAssertDisabledHeaders()
{
  ExercisePool<LibXR::ObjectPool<uint32_t>>();
  ExercisePool<LibXR::SPSCObjectPool<uint32_t>>();
  ExercisePool<LibXR::MPMCObjectPool<uint32_t>>();
}

template class LibXR::LinuxSharedTopic<uint32_t>;
