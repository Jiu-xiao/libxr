/**
 * @file test_transform.cpp
 * @brief transform 测试聚合入口。 Aggregation entry for split transform tests.
 */
#include "transform_test_common.hpp"

/**
 * @brief 测试入口函数 `test_transform`。 Test entry function `test_transform`.
 * @details 测试内容：聚合执行拆分后的 transform 子测试组。 Aggregate and execute the split transform subtest groups.
 *          测试原理：保留原入口名字，同时把一个大文件拆成构造、互操作和欧拉顺序三个语义组。 Preserve the original entry name while splitting one large file into construction, interoperability, and Euler-order groups.
 */
void test_transform()
{
  RunTransformConstructionTests();
  RunTransformRotationInteropTests();
  RunTransformEulerOrderTests();
}
