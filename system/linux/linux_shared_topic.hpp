#pragma once

// 平台 wrapper 先选择本平台 MonotonicTime，shared impl 只复用接口。
#include "monotonic_time.hpp"
#include "linux_shared_topic_impl.hpp"
