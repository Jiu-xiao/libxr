/**
 * @file test_topic.cpp
 * @brief 类型化 `Topic` 测试聚合入口。 Aggregation entry for split typed-`Topic` tests.
 * @details 测试项目：
 *          1. 聚合分发 fan-out 子场景。
 *          2. 聚合可变 payload 与队列背压子场景。
 *          Test items:
 *          1. Aggregate dispatch fan-out sub-scenarios.
 *          2. Aggregate mutable-payload and queue-backpressure sub-scenarios.
 */
#include "topic_test_common.hpp"

void RunTopicDispatchTests();
void RunTopicMutationTests();

/**
 * @brief 测试入口函数 `test_message_topic`。 Test entry function `test_message_topic`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_message_topic()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  RunTopicDispatchTests();
  RunTopicMutationTests();
}
