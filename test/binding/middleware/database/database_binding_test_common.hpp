/**
 * @file database_binding_test_common.hpp
 * @brief database binding 测试聚合 helper 入口。 Aggregation helper entry for database binding tests.
 * @details 共享职责：
 *          1. 聚合 flash/fatal helper 与 flash-image/reopen helper。
 *          2. 让 sequential/raw 子测试只依赖一个薄包装头。
 *          Shared responsibilities:
 *          1. Aggregate flash/fatal helpers with flash-image/reopen helpers.
 *          2. Keep sequential/raw subtests depending on one thin wrapper header.
 */
#pragma once

#include "database_binding_reopen_test_common.hpp"
