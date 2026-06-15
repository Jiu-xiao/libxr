/**
 * @file topic_test_common.hpp
 * @brief `Topic` 测试共用时间戳辅助函数。 Shared timestamp helper for `Topic` tests.
 * @details 测试项目：
 *          1. 汇总 `Topic` 子测试所需头文件。
 *          2. 提供统一的时间戳转整数辅助函数。
 *          Test items:
 *          1. Gather the headers used by split `Topic` tests.
 *          2. Provide one shared integer-conversion helper for timestamps.
 */
#pragma once

#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "message_test_payloads.hpp"
#include "test.hpp"

namespace
{

/**
 * @brief 辅助函数 `TimestampUs`。 Helper function `TimestampUs`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
uint64_t TimestampUs(const LibXR::MicrosecondTimestamp& timestamp)
{
  return static_cast<uint64_t>(timestamp);
}

}  // namespace
