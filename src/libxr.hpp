#pragma once

#include "libxr_assert.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_string.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"

#include "libxr_platform.hpp"

#include "mutex.hpp"
#include "semaphore.hpp"
#include "signal.hpp"
#include "thread.hpp"
#include "timer.hpp"

#include "queue.hpp"

namespace LibXR {
void LibXR_Init();
}