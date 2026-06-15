/**
 * @file test_raw.cpp
 * @brief linux file-backed `DatabaseRaw` 测试聚合入口。 Aggregation entry for split linux file-backed `DatabaseRaw` tests.
 */
#include "raw_database_test_groups.hpp"

/**
 * @brief 测试入口函数 `test_linux_database_raw`。 Test entry function `test_linux_database_raw`.
 * @details 测试内容：聚合执行拆分后的 linux file-backed `DatabaseRaw` 子测试组。 Aggregate and execute the split linux file-backed `DatabaseRaw` subtest groups.
 *          测试原理：在拆开 smoke / failure / recovery 之后，继续保留原 runner 使用的单入口名称。 Preserve the original runner-visible entry name after splitting smoke, failure, and recovery paths.
 */
void test_linux_database_raw()
{
  RunLinuxDatabaseRawSmokeTests();
  RunLinuxDatabaseRawFailureTests();
  RunLinuxDatabaseRawRecoveryTests();
}
