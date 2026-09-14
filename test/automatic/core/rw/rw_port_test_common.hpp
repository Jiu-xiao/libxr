/**
 * @file rw_port_test_common.hpp
 * @brief 读写端口测试使用的手动完成后端。 /
 * Manual-completion backend helper for read/write port tests.
 *
 * 绑定空写回调，让测试决定何时取走请求并发出完成通知。
 * Binds an idle write callback so each test controls request consumption and completion.
 */

#pragma once

#include <cstring>
#include <vector>

#include "rw_thread_test_common.hpp"
#include "test_assert.hpp"

namespace
{
void PendingWriteFun(LibXR::WritePort&, bool) {}

}  // namespace
