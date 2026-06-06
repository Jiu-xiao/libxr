/**
 * @file test_packet_parse.cpp
 * @brief message packet 头部编码与解析子测试。 Split test unit for message packet header encoding and parsing.
 */
#include "message_packet_test_common.hpp"

namespace
{
/**
 * @brief 测试项函数 `TestPacketHeaderAndServerParse`。 Test-item function `TestPacketHeaderAndServerParse`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestPacketHeaderAndServerParse()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  constexpr size_t PACKET_SIZE = LibXR::Topic::PACK_BASE_SIZE + sizeof(double);

  auto domain = LibXR::Topic::Domain("message_packet_domain");
  auto topic = LibXR::Topic::CreateTopic<double>("message_packet_tp", &domain);

  static bool cb_in_isr = false;
  static LibXR::MicrosecondTimestamp cb_timestamp;
  static double rx_value = 0.0;

  auto msg_cb = LibXR::Topic::Callback::Create(
      [](bool in_isr, void*, LibXR::MicrosecondTimestamp timestamp, double& data)
      {
        cb_in_isr = in_isr;
        cb_timestamp = timestamp;
        rx_value = data;
      },
      reinterpret_cast<void*>(0));
  topic.RegisterCallback(msg_cb);

  LibXR::Topic::PackedData<double> packed_data;
  LibXR::Topic::Server topic_server(512);
  topic_server.Register(topic);

  const double value0 = 48.48;
  const LibXR::MicrosecondTimestamp timestamp0(4004);
  ASSERT(topic.PackData(value0, packed_data, timestamp0) == LibXR::ErrorCode::OK);
  rx_value = -1.0;
  cb_in_isr = false;
  ASSERT(topic_server.ParseDataFromCallback(LibXR::ConstRawData(packed_data), true) == 1);
  ASSERT(rx_value == value0);
  ASSERT(TimestampUs(cb_timestamp) == TimestampUs(timestamp0));
  ASSERT(cb_in_isr);

  const double value1 = 56.56;
  const LibXR::MicrosecondTimestamp timestamp1(5005);
  ASSERT(topic.PackData(value1, packed_data, timestamp1) == LibXR::ErrorCode::OK);
  rx_value = -1.0;
  cb_in_isr = true;
  ASSERT(topic_server.ParseDataFromCallback(LibXR::ConstRawData(packed_data), false) ==
         1);
  ASSERT(rx_value == value1);
  ASSERT(TimestampUs(cb_timestamp) == TimestampUs(timestamp1));
  ASSERT(!cb_in_isr);

  const double value2 = 64.64;
  const LibXR::MicrosecondTimestamp timestamp2(0x010203040506ULL);
  ASSERT(topic.PackData(value2, packed_data, timestamp2) == LibXR::ErrorCode::OK);
  ASSERT(packed_data.raw.header_.prefix == LibXR::Topic::PACKET_PREFIX);
  ASSERT(packed_data.raw.header_.version == LibXR::Topic::PACKET_VERSION);
  ASSERT(packed_data.raw.header_.data_len_raw[0] == sizeof(double));
  ASSERT(packed_data.raw.header_.data_len_raw[1] == 0);
  ASSERT(packed_data.raw.header_.data_len_raw[2] == 0);
  ASSERT(packed_data.raw.header_.timestamp_us_raw[0] == 0x06);
  ASSERT(packed_data.raw.header_.timestamp_us_raw[1] == 0x05);
  ASSERT(packed_data.raw.header_.timestamp_us_raw[2] == 0x04);
  ASSERT(packed_data.raw.header_.timestamp_us_raw[3] == 0x03);
  ASSERT(packed_data.raw.header_.timestamp_us_raw[4] == 0x02);
  ASSERT(packed_data.raw.header_.timestamp_us_raw[5] == 0x01);
  ASSERT(TimestampUs(packed_data.GetTimestamp()) == TimestampUs(timestamp2));

  auto* packet = reinterpret_cast<uint8_t*>(&packed_data);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packet, 3)) == 0);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packet + 3, PACKET_SIZE - 3)) == 1);
  ASSERT(rx_value == value2);
  ASSERT(TimestampUs(cb_timestamp) == TimestampUs(timestamp2));

  LibXR::Topic::Server exact_size_server(PACKET_SIZE);
  exact_size_server.Register(topic);
  ASSERT(exact_size_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(rx_value == value2);
  ASSERT(TimestampUs(cb_timestamp) == TimestampUs(timestamp2));
}

}  // namespace

/**
 * @brief 测试项函数 `RunMessagePacketParseTests`。 Test-item function `RunMessagePacketParseTests`.
 * @details 测试内容：执行当前分组里的 message packet 子场景。 Execute the grouped message-packet sub-scenarios for this split file.
 *          测试原理：把解析、失败校验和对齐兼容语义拆开，降低 packet 测试文件复杂度。 Split parsing, validation-failure, and alignment-compatibility semantics into separate files to reduce packet-test complexity.
 */
void RunMessagePacketParseTests()
{
  TestPacketHeaderAndServerParse();
}
