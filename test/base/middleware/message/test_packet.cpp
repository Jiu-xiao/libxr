/**
 * @file test_packet.cpp
 * @brief message packet 测试聚合入口。 Aggregation entry for split message packet tests.
 */
#include "message_packet_test_common.hpp"

/**
 * @brief 测试入口函数 `test_message_packet`。 Test entry function `test_message_packet`.
 * @details 测试内容：聚合执行拆分后的 message packet 子测试组。 Aggregate and execute the split message-packet subtest groups.
 *          测试原理：保持原 runner 入口不变，同时把 packet 大文件按语义拆分。 Preserve the original runner entry while splitting the oversized packet test by semantic groups.
 */
void test_message_packet()
{
  RunMessagePacketParseTests();
  RunMessagePacketValidationTests();
  RunMessagePacketAlignmentTests();
}
