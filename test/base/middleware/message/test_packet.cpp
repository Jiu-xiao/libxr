/**
 * @file test_packet.cpp
 * @brief 消息总线打包报文与 server 解析测试。 Packed topic packet and server parsing tests.
 *
 * 测试项目 / Test items:
 * 1. 头部编码与回调上下文解析。 Header encoding and callback parse: verify packet prefix/version/timestamp encoding and callback-context propagation through `Server` parsing.
 * 2. 版本/topic/CRC/截断等失败分支。 Validation failures: verify unknown topic/version, truncated payload, legacy prefix and CRC corruption are handled on the intended branches.
 * 3. 对齐和短 payload 长度兼容。 Alignment and short-length compatibility: verify over-aligned payload parsing and shorter packet payload lengths still dispatch correctly under the supported contract.
 *
 * 测试原理 / Test principles:
 * 1. 先走真实 `Topic::PackData()`，再精确篡改序列化字节去击中特定分支。 Build packets through the real `Topic::PackData()` path, then mutate the serialized bytes to drive specific parser branches deliberately.
 * 2. 同时检查解包后的 payload 和元数据字段，因为这个子系统既是传输契约也是线格式契约。 Check both decoded payloads and metadata fields, because this subsystem's contract is part data transport and part wire-format compatibility.
 */
#include <cstdint>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "message_test_payloads.hpp"
#include "test.hpp"

namespace
{

/**
 * @brief 辅助函数 `TimestampUs`。 Helper function `TimestampUs`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
uint64_t TimestampUs(const LibXR::MicrosecondTimestamp& timestamp)
{
  return static_cast<uint64_t>(timestamp);
}

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
 * @brief 测试入口函数 `test_message_packet`。 Test entry function `test_message_packet`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_message_packet()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestPacketHeaderAndServerParse();
  TestPacketValidationFailures();
  TestPacketAlignmentAndLengthCompatibility();
}
