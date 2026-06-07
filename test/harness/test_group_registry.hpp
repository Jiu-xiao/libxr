/**
 * @file test_group_registry.hpp
 * @brief base/runtime 测试主矩阵包装器。 Wrapper around the unified main-test matrix.
 * @details 作用：
 *          1. 保留历史 `run_libxr_tests()` 名称，减少 runner 改动面。
 *          2. 实际执行逻辑转交给统一入口矩阵。
 *          Purpose:
 *          1. Preserve the historical `run_libxr_tests()` name to minimize runner churn.
 *          2. Delegate the actual execution logic to the unified entry matrix.
 */
#pragma once

#include "test_matrix.hpp"

inline void run_libxr_tests() { (void)RunMainTestBinary(); }
