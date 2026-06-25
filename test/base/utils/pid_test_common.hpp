/**
 * @file pid_test_common.hpp
 * @brief PID 测试共用 helper。 Shared helpers for PID tests.
 */
#pragma once

#include <cmath>
#include <limits>

#include "libxr_def.hpp"
#include "pid.hpp"
#include "test.hpp"

/**
 * @brief 辅助函数 `near`。 Helper function `near`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
static inline bool near(double a, double b, double eps = 1e-6)
{
  return std::abs(a - b) <= eps;
}

void RunPidResponseTests();
void RunPidStateTests();
void RunPidCycleTests();
