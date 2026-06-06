/**
 * @file test_packet_validation.cpp
 * @brief message packet 失败校验子测试。 Split test unit for message packet validation-failure scenarios.
 */
#include "message_packet_test_common.hpp"

namespace
{
/**
 * @brief 测试项函数 `TestPacketValidationFailures`。 Test-item function `TestPacketValidationFailures`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestPacketValidationFailures()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  constexpr size_t PACKET_SIZE = LibXR::Topic::PACK_BASE_SIZE + sizeof(double);

  auto domain = LibXR::Topic::Domain("message_packet_validation_domain");
  auto topic = LibXR::Topic::CreateTopic<double>("message_packet_validation_tp", &domain);

  static double rx_value = 0.0;
  auto msg_cb = LibXR::Topic::Callback::Create(
      [](bool, void*, LibXR::MicrosecondTimestamp, double& data) { rx_value = data; },
      reinterpret_cast<void*>(0));
  topic.RegisterCallback(msg_cb);

  LibXR::Topic::PackedData<double> packed_data;
  ASSERT(topic.PackData(77.77, packed_data, LibXR::MicrosecondTimestamp(123456)) ==
         LibXR::ErrorCode::OK);

  LibXR::Topic::Server topic_server(512);
  topic_server.Register(topic);

  auto unknown_topic_packet = packed_data;
  unknown_topic_packet.raw.header_.topic_name_crc32 ^= 0x13572468;
  unknown_topic_packet.raw.header_.pack_header_crc8 =
      LibXR::CRC8::Calculate(&unknown_topic_packet.raw,
                             sizeof(LibXR::Topic::PackedDataHeader) - sizeof(uint8_t));
  unknown_topic_packet.crc8_ =
      LibXR::CRC8::Calculate(&unknown_topic_packet, PACKET_SIZE - sizeof(uint8_t));
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(unknown_topic_packet)) == 0);

  auto unknown_version_packet = packed_data;
  unknown_version_packet.raw.header_.version ^= 0x5A;
  unknown_version_packet.raw.header_.pack_header_crc8 =
      LibXR::CRC8::Calculate(&unknown_version_packet.raw,
                             sizeof(LibXR::Topic::PackedDataHeader) - sizeof(uint8_t));
  unknown_version_packet.crc8_ =
      LibXR::CRC8::Calculate(&unknown_version_packet, PACKET_SIZE - sizeof(uint8_t));
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(unknown_version_packet)) == 0);

  auto truncated_packet = packed_data;
  truncated_packet.raw.header_.SetDataLen(sizeof(double) - 1);
  truncated_packet.raw.header_.pack_header_crc8 =
      LibXR::CRC8::Calculate(&truncated_packet.raw,
                             sizeof(LibXR::Topic::PackedDataHeader) - sizeof(uint8_t));
  truncated_packet.crc8_ =
      LibXR::CRC8::Calculate(&truncated_packet, PACKET_SIZE - sizeof(uint8_t) - 1);
  rx_value = -1.0;
  ASSERT(topic_server.ParseData(
             LibXR::ConstRawData(&truncated_packet, PACKET_SIZE - 1)) == 1);
  ASSERT(rx_value != 77.77);

  auto legacy_prefix_packet = packed_data;
  legacy_prefix_packet.raw.header_.prefix = 0xA5;
  legacy_prefix_packet.raw.header_.pack_header_crc8 =
      LibXR::CRC8::Calculate(&legacy_prefix_packet.raw,
                             sizeof(LibXR::Topic::PackedDataHeader) - sizeof(uint8_t));
  legacy_prefix_packet.crc8_ =
      LibXR::CRC8::Calculate(&legacy_prefix_packet, PACKET_SIZE - sizeof(uint8_t));
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(legacy_prefix_packet)) == 0);

  auto bad_header_packet = packed_data;
  bad_header_packet.raw.header_.pack_header_crc8 ^= 0x5A;
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(bad_header_packet)) == 0);

  auto bad_payload_packet = packed_data;
  bad_payload_packet.crc8_ ^= 0xA5;
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(bad_payload_packet)) == 0);

  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(rx_value == 77.77);
}

}  // namespace

/**
 * @brief 测试项函数 `RunMessagePacketValidationTests`。 Test-item function `RunMessagePacketValidationTests`.
 * @details 测试内容：执行当前分组里的 message packet 子场景。 Execute the grouped message-packet sub-scenarios for this split file.
 *          测试原理：把解析、失败校验和对齐兼容语义拆开，降低 packet 测试文件复杂度。 Split parsing, validation-failure, and alignment-compatibility semantics into separate files to reduce packet-test complexity.
 */
void RunMessagePacketValidationTests()
{
  TestPacketValidationFailures();
}
