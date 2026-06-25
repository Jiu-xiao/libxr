#include "thread.hpp"

#include "libxr_system.hpp"

using namespace LibXR;

Thread Thread::Current(void) { return Thread(xTaskGetCurrentTaskHandle()); }
