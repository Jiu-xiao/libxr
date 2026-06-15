/**
 * @file print_test_common.hpp
 * @brief `print` 测试聚合 helper 入口。 Aggregation helper entry for `print` tests.
 * @details 作用：
 *          1. 聚合字面量解析约束、sink/坏格式探针和字符串对照 helper。
 *          2. 让各个 `print` 子测试文件只依赖一个薄包装头。
 *          Purpose:
 *          1. Aggregate literal-resolution constraints, sink/malformed-format probes, and comparison helpers.
 *          2. Keep split `print` test files depending on one thin wrapper header.
 */
#pragma once

#include "print_compare_test_common.hpp"
