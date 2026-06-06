/**
 * @file test_packet_alignment.cpp
 * @brief message packet 对齐与长度兼容子测试。 Split test unit for message packet alignment and length compatibility.
 */
#include "message_packet_test_common.hpp"

namespace
{
/**
 * @brief 测试项函数 `TestPacketAlignmentAndLengthCompatibility`。 Test-item function `TestPacketAlignmentAndLengthCompatibility`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestPacketAlignmentAndLengthCompatibility()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  auto domain = LibXR::Topic::Domain("message_packet_alignment_domain");

  auto aligned_topic =
      LibXR::Topic::CreateTopic<WideAlignedPayload>("aligned_packet_tp", &domain);
  static uint64_t aligned_view_value = 0;
  auto aligned_cb = LibXR::Topic::Callback::Create(
      [](bool, void*, const LibXR::Topic::MessageView<WideAlignedPayload>& message)
      {
        ASSERT(message.data != nullptr);
        aligned_view_value = message.data->right;
      },
      reinterpret_cast<void*>(0));
  aligned_topic.RegisterCallback(aligned_cb);
  LibXR::Topic::Server aligned_server(512);
  aligned_server.Register(aligned_topic);
  WideAlignedPayload aligned_tx{0x1122334455667788ULL, 0x8877665544332211ULL};
  LibXR::Topic::PackedData<WideAlignedPayload> aligned_packet;
  ASSERT(aligned_topic.PackData(aligned_tx, aligned_packet,
                                LibXR::MicrosecondTimestamp(6106)) ==
         LibXR::ErrorCode::OK);
  aligned_view_value = 0;
  ASSERT(aligned_server.ParseData(LibXR::ConstRawData(aligned_packet)) == 1);
  ASSERT(aligned_view_value == aligned_tx.right);

  auto prefix_topic =
      LibXR::Topic::CreateTopic<PrefixIntPayload>("prefix_int_tp", &domain);
  static PrefixIntPayload prefix_rx{};
  auto prefix_cb = LibXR::Topic::Callback::Create(
      [](bool, void*, PrefixIntPayload& data) { prefix_rx = data; },
      reinterpret_cast<void*>(0));
  prefix_topic.RegisterCallback(prefix_cb);
  LibXR::Topic::Server prefix_server(512);
  prefix_server.Register(prefix_topic);
  PrefixIntPayload prefix_tx{0x11223344, 0x55667788};
  LibXR::Topic::PackedData<PrefixIntPayload> prefix_packet;
  ASSERT(prefix_topic.PackData(prefix_tx, prefix_packet,
                               LibXR::MicrosecondTimestamp(6116)) ==
         LibXR::ErrorCode::OK);
  prefix_packet.raw.header_.SetDataLen(sizeof(int32_t));
  prefix_packet.raw.header_.pack_header_crc8 =
      LibXR::CRC8::Calculate(&prefix_packet.raw,
                             sizeof(LibXR::Topic::PackedDataHeader) - sizeof(uint8_t));
  prefix_packet.crc8_ =
      LibXR::CRC8::Calculate(&prefix_packet, LibXR::Topic::PACK_BASE_SIZE - 1 +
                                                 sizeof(int32_t));
  prefix_rx = PrefixIntPayload{-1, -1};
  ASSERT(prefix_server.ParseData(
             LibXR::ConstRawData(&prefix_packet, LibXR::Topic::PACK_BASE_SIZE +
                                                     sizeof(int32_t))) == 1);
  ASSERT(prefix_rx.value == prefix_tx.value);
}

}  // namespace

/**
 * @brief 测试项函数 `RunMessagePacketAlignmentTests`。 Test-item function `RunMessagePacketAlignmentTests`.
 * @details 测试内容：执行当前分组里的 message packet 子场景。 Execute the grouped message-packet sub-scenarios for this split file.
 *          测试原理：把解析、失败校验和对齐兼容语义拆开，降低 packet 测试文件复杂度。 Split parsing, validation-failure, and alignment-compatibility semantics into separate files to reduce packet-test complexity.
 */
void RunMessagePacketAlignmentTests()
{
  TestPacketAlignmentAndLengthCompatibility();
}
