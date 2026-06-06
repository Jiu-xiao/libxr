/**
 * @file test_raw.cpp
 * @brief binding `DatabaseRaw` 测试聚合入口。 Aggregation entry for split binding `DatabaseRaw` tests.
 */
#include "raw_binding_test_groups.hpp"

/**
 * @brief 测试入口函数 `test_database_binding_raw`。 Test entry function `test_database_binding_raw`.
 * @details 测试内容：聚合执行拆分后的 binding `DatabaseRaw` 子测试组。 Aggregate and execute the split binding `DatabaseRaw` subtest groups.
 *          测试原理：在拆开 smoke / failure / recovery 之后，继续保留原 runner 使用的单入口名称。 Preserve the original runner-visible entry name after splitting smoke, failure, and recovery paths.
 */
void test_database_binding_raw()
{
  RunDatabaseBindingRawSmokeTests();
  RunDatabaseBindingRawFailureTests();
  RunDatabaseBindingRawRecoveryTests();
}
