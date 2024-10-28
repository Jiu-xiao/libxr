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

void LibXR::PlatformInit() {}

void *operator new(std::size_t size) { return pvPortMalloc(size); }

void operator delete(void *ptr) noexcept { vPortFree(ptr); }
void operator delete(void *ptr, std::size_t size) noexcept { vPortFree(ptr); }
