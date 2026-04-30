#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_message()
{
  constexpr size_t PACKET_SIZE = LibXR::Topic::PACK_BASE_SIZE + sizeof(double);
  ASSERT(LibXR::Topic::Find("missing_default_topic") == nullptr);

  auto timestamp_us = [](const LibXR::MicrosecondTimestamp& timestamp)
  { return static_cast<uint64_t>(timestamp); };

  auto domain = LibXR::Topic::Domain("test_domain");
  auto topic = LibXR::Topic::CreateTopic<double>("test_tp", &domain, false, true);
  static double msg[4];
  auto sync_suber = LibXR::Topic::SyncSubscriber<double>("test_tp", msg[1], &domain);
  auto async_suber = LibXR::Topic::ASyncSubscriber<double>(topic);
  LibXR::LockFreeQueue<double> msg_queue(10);
  auto queue_suber = LibXR::Topic::QueuedSubscriber(topic, msg_queue);
  LibXR::LockFreeQueue<LibXR::Topic::Message<double>> timed_msg_queue(10);
  auto timed_queue_suber = LibXR::Topic::QueuedSubscriber(topic, timed_msg_queue);
  static bool cb_in_isr = false;
  static LibXR::MicrosecondTimestamp cb_timestamp;
  static LibXR::MicrosecondTimestamp view_cb_timestamp;
  static double view_cb_value = 0.0;
  static bool raw_cb_in_isr = false;
  static LibXR::MicrosecondTimestamp raw_cb_timestamp;
  static size_t raw_cb_size = 0;
  static double raw_cb_value = 0.0;

  auto msg_cb = LibXR::Topic::Callback::Create(
      [](bool in_isr, void*, LibXR::MicrosecondTimestamp timestamp, double& data)
      {
        cb_in_isr = in_isr;
        cb_timestamp = timestamp;
        msg[3] = data;
      },
      reinterpret_cast<void*>(0));

  topic.RegisterCallback(msg_cb);

  auto view_cb = LibXR::Topic::Callback::Create(
      [](bool, void*, const LibXR::Topic::MessageView<double>& message)
      {
        view_cb_timestamp = message.timestamp;
        view_cb_value = message.data;
      },
      reinterpret_cast<void*>(0));

  topic.RegisterCallback(view_cb);

  auto raw_cb = LibXR::Topic::Callback::Create(
      [](bool in_isr, void*, LibXR::RawData& data)
      {
        raw_cb_in_isr = in_isr;
        raw_cb_size = data.size_;
        LibXR::Memory::FastCopy(&raw_cb_value, data.addr_, sizeof(raw_cb_value));
      },
      reinterpret_cast<void*>(0));

  topic.RegisterCallback(raw_cb);

  auto raw_timestamp_cb = LibXR::Topic::Callback::Create(
      [](bool, void*, LibXR::MicrosecondTimestamp timestamp, LibXR::RawData&)
      { raw_cb_timestamp = timestamp; }, reinterpret_cast<void*>(0));

  topic.RegisterCallback(raw_timestamp_cb);

  ASSERT(!async_suber.Available());

  msg[0] = 16.16;
  const LibXR::MicrosecondTimestamp timestamp0(1001);
  async_suber.StartWaiting();
  topic.Publish(msg[0], timestamp0);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(async_suber.Available());
  ASSERT(msg[1] == msg[0]);
  ASSERT(timestamp_us(sync_suber.GetTimestamp()) == timestamp_us(timestamp0));
  ASSERT(async_suber.GetData() == msg[0]);
  ASSERT(timestamp_us(async_suber.GetTimestamp()) == timestamp_us(timestamp0));
  ASSERT(!async_suber.Available());
  ASSERT(msg_queue.Size() == 1);
  double queue_value = 0.0;
  msg_queue.Pop(queue_value);
  ASSERT(queue_value == msg[0]);
  ASSERT(timed_msg_queue.Size() == 1);
  LibXR::Topic::Message<double> queue_msg;
  timed_msg_queue.Pop(queue_msg);
  ASSERT(queue_msg.data == msg[0]);
  ASSERT(timestamp_us(queue_msg.timestamp) == timestamp_us(timestamp0));
  ASSERT(msg[3] == msg[0]);
  ASSERT(timestamp_us(cb_timestamp) == timestamp_us(timestamp0));
  ASSERT(view_cb_value == msg[0]);
  ASSERT(timestamp_us(view_cb_timestamp) == timestamp_us(timestamp0));
  ASSERT(!cb_in_isr);
  ASSERT(raw_cb_value == msg[0]);
  ASSERT(raw_cb_size == sizeof(msg[0]));
  ASSERT(timestamp_us(raw_cb_timestamp) == timestamp_us(timestamp0));
  ASSERT(!raw_cb_in_isr);

  msg[0] = 32.32;
  msg[3] = -1.0f;
  const LibXR::MicrosecondTimestamp timestamp1(2002);
  async_suber.StartWaiting();
  topic.PublishFromCallback(msg[0], timestamp1, true);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(async_suber.Available());
  ASSERT(msg[1] == msg[0]);
  ASSERT(timestamp_us(sync_suber.GetTimestamp()) == timestamp_us(timestamp1));
  ASSERT(async_suber.GetData() == msg[0]);
  ASSERT(timestamp_us(async_suber.GetTimestamp()) == timestamp_us(timestamp1));
  ASSERT(msg_queue.Size() == 1);
  msg_queue.Pop(queue_value);
  ASSERT(queue_value == msg[0]);
  ASSERT(timed_msg_queue.Size() == 1);
  timed_msg_queue.Pop(queue_msg);
  ASSERT(queue_msg.data == msg[0]);
  ASSERT(timestamp_us(queue_msg.timestamp) == timestamp_us(timestamp1));
  ASSERT(msg[3] == msg[0]);
  ASSERT(timestamp_us(cb_timestamp) == timestamp_us(timestamp1));
  ASSERT(cb_in_isr);
  ASSERT(raw_cb_value == msg[0]);
  ASSERT(raw_cb_size == sizeof(msg[0]));
  ASSERT(timestamp_us(raw_cb_timestamp) == timestamp_us(timestamp1));
  ASSERT(raw_cb_in_isr);

  topic.Publish(msg[0], LibXR::MicrosecondTimestamp(3003));
  ASSERT(!async_suber.Available());
  LibXR::Topic::PackedData<double> packed_data;
  LibXR::Topic::Server topic_server(512);

  topic.DumpData(packed_data);
  topic_server.Register(topic);

  topic_server.ParseData(LibXR::ConstRawData(packed_data));
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);
  ASSERT(timestamp_us(sync_suber.GetTimestamp()) ==
         timestamp_us(packed_data.GetTimestamp()));

  msg[0] = 48.48;
  const LibXR::MicrosecondTimestamp timestamp2(4004);
  topic.Publish(msg[0], timestamp2);
  topic.DumpData(packed_data);
  msg[3] = -1.0f;
  cb_in_isr = false;
  ASSERT(topic_server.ParseDataFromCallback(LibXR::ConstRawData(packed_data), true) == 1);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);
  ASSERT(timestamp_us(sync_suber.GetTimestamp()) == timestamp_us(timestamp2));
  ASSERT(msg[3] == msg[0]);
  ASSERT(timestamp_us(cb_timestamp) == timestamp_us(timestamp2));
  ASSERT(cb_in_isr);

  msg[0] = 56.56;
  const LibXR::MicrosecondTimestamp timestamp3(5005);
  topic.Publish(msg[0], timestamp3);
  topic.DumpData(packed_data);
  msg[3] = -1.0f;
  cb_in_isr = true;
  ASSERT(topic_server.ParseDataFromCallback(LibXR::ConstRawData(packed_data), false) ==
         1);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);
  ASSERT(timestamp_us(sync_suber.GetTimestamp()) == timestamp_us(timestamp3));
  ASSERT(msg[3] == msg[0]);
  ASSERT(timestamp_us(cb_timestamp) == timestamp_us(timestamp3));
  ASSERT(!cb_in_isr);

  msg[0] = 64.64;
  const LibXR::MicrosecondTimestamp timestamp4(6006);
  topic.Publish(msg[0], timestamp4);
  topic.DumpData(packed_data);
  ASSERT(packed_data.raw.header_.prefix == LibXR::Topic::PACKET_PREFIX);
  auto* packet = reinterpret_cast<uint8_t*>(&packed_data);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packet, 3)) == 0);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packet + 3, PACKET_SIZE - 3)) == 1);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);
  ASSERT(timestamp_us(sync_suber.GetTimestamp()) == timestamp_us(timestamp4));

  LibXR::Topic::Server exact_size_server(PACKET_SIZE);
  exact_size_server.Register(topic);
  ASSERT(exact_size_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);
  ASSERT(timestamp_us(sync_suber.GetTimestamp()) == timestamp_us(timestamp4));

  auto borrowed_topic =
      LibXR::Topic::CreateTopic<double>("borrowed_tp", &domain, false, false, true);
  double borrowed_source = 11.11;
  double borrowed_dump = 0.0;
  LibXR::MicrosecondTimestamp borrowed_dump_timestamp;
  const LibXR::MicrosecondTimestamp borrowed_timestamp(7007);
  borrowed_topic.Publish(borrowed_source, borrowed_timestamp);
  ASSERT(borrowed_topic.DumpData(borrowed_dump, borrowed_dump_timestamp) ==
         LibXR::ErrorCode::OK);
  ASSERT(borrowed_dump == borrowed_source);
  ASSERT(timestamp_us(borrowed_dump_timestamp) == timestamp_us(borrowed_timestamp));
  borrowed_dump = 0.0;
  ASSERT(borrowed_topic.DumpData(LibXR::RawData(borrowed_dump),
                                 borrowed_dump_timestamp) == LibXR::ErrorCode::OK);
  ASSERT(borrowed_dump == borrowed_source);
  ASSERT(timestamp_us(borrowed_dump_timestamp) == timestamp_us(borrowed_timestamp));
  borrowed_source = 22.22;
  ASSERT(borrowed_topic.DumpData(borrowed_dump, borrowed_dump_timestamp) ==
         LibXR::ErrorCode::OK);
  ASSERT(borrowed_dump == borrowed_source);
  ASSERT(timestamp_us(borrowed_dump_timestamp) == timestamp_us(borrowed_timestamp));

  struct BasePayload
  {
    uint32_t value;
  };

  struct ExtendedPayload
  {
    BasePayload base;
    uint32_t extra;
  };

  auto prefix_topic =
      LibXR::Topic::CreateTopic<ExtendedPayload>("prefix_tp", &domain, false, true);
  ExtendedPayload extended_source{{0x13572468}, 0x24681357};
  BasePayload prefix_dump{};
  LibXR::MicrosecondTimestamp prefix_dump_timestamp;
  const LibXR::MicrosecondTimestamp prefix_timestamp(7017);
  prefix_topic.Publish(extended_source, prefix_timestamp);
  ASSERT(prefix_topic.DumpData(prefix_dump, prefix_dump_timestamp) ==
         LibXR::ErrorCode::OK);
  ASSERT(prefix_dump.value == extended_source.base.value);
  ASSERT(timestamp_us(prefix_dump_timestamp) == timestamp_us(prefix_timestamp));

  auto base_topic =
      LibXR::Topic::CreateTopic<BasePayload>("base_tp", &domain, false, true);
  BasePayload base_source{0x89ABCDEF};
  ExtendedPayload oversized_dump{};
  base_topic.Publish(base_source, LibXR::MicrosecondTimestamp(7027));
  ASSERT(base_topic.DumpData(oversized_dump, prefix_dump_timestamp) ==
         LibXR::ErrorCode::SIZE_ERR);

  auto mutable_topic =
      LibXR::Topic::CreateTopic<int>("mutable_payload_tp", &domain, false, true);
  auto mutable_cb = LibXR::Topic::Callback::Create(
      [](bool, void*, int& data) { data = 5678; }, reinterpret_cast<void*>(0));
  mutable_topic.RegisterCallback(mutable_cb);
  int mutable_payload = 1234;
  int mutable_dump = 0;
  mutable_topic.Publish(mutable_payload, LibXR::MicrosecondTimestamp(7037));
  ASSERT(mutable_topic.DumpData(mutable_dump) == LibXR::ErrorCode::OK);
  ASSERT(mutable_dump == 5678);

  auto queue_drop_topic =
      LibXR::Topic::CreateTopic<int>("queue_drop_tp", &domain, false, false, true);
  LibXR::LockFreeQueue<int> drop_queue(1);
  auto drop_suber = LibXR::Topic::QueuedSubscriber(queue_drop_topic, drop_queue);
  ASSERT(drop_suber.GetDroppedCount() == 0);
  for (size_t i = 0; i < drop_queue.MaxSize(); i++)
  {
    auto value = static_cast<int>(i);
    queue_drop_topic.Publish(value, LibXR::MicrosecondTimestamp(8000 + i));
  }
  ASSERT(drop_queue.Size() == drop_queue.MaxSize());
  int dropped_value = -123;
  queue_drop_topic.Publish(dropped_value, LibXR::MicrosecondTimestamp(9009));
  ASSERT(drop_queue.Size() == drop_queue.MaxSize());
  ASSERT(drop_suber.GetDroppedCount() == 1);
  for (size_t i = 0; i < drop_queue.MaxSize(); i++)
  {
    int value = 0;
    ASSERT(drop_queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == static_cast<int>(i));
  }
  int dropped_message = 0;
  ASSERT(drop_queue.Pop(dropped_message) == LibXR::ErrorCode::EMPTY);
  drop_suber.ResetDroppedCount();
  ASSERT(drop_suber.GetDroppedCount() == 0);

  auto unknown_topic_packet = packed_data;
  unknown_topic_packet.raw.header_.topic_name_crc32 ^= 0x13572468;
  unknown_topic_packet.raw.header_.pack_header_crc8 =
      LibXR::CRC8::Calculate(&unknown_topic_packet.raw,
                             sizeof(LibXR::Topic::PackedDataHeader) - sizeof(uint8_t));
  unknown_topic_packet.crc8_ =
      LibXR::CRC8::Calculate(&unknown_topic_packet, PACKET_SIZE - sizeof(uint8_t));
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(unknown_topic_packet)) == 0);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);

  auto legacy_prefix_packet = packed_data;
  legacy_prefix_packet.raw.header_.prefix = 0xA5;
  legacy_prefix_packet.raw.header_.pack_header_crc8 =
      LibXR::CRC8::Calculate(&legacy_prefix_packet.raw,
                             sizeof(LibXR::Topic::PackedDataHeader) - sizeof(uint8_t));
  legacy_prefix_packet.crc8_ =
      LibXR::CRC8::Calculate(&legacy_prefix_packet, PACKET_SIZE - sizeof(uint8_t));
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(legacy_prefix_packet)) == 0);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);

  auto bad_header_packet = packed_data;
  bad_header_packet.raw.header_.pack_header_crc8 ^= 0x5A;
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(bad_header_packet)) == 0);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);

  auto bad_payload_packet = packed_data;
  bad_payload_packet.crc8_ ^= 0xA5;
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(bad_payload_packet)) == 0);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);

  for (int i = 0; i < 1000; i++)
  {
    msg[0] = i * 0.1;
    const LibXR::MicrosecondTimestamp timestamp(10000 + i);
    topic.Publish(msg[0], timestamp);
    topic.DumpData(packed_data);
    topic_server.ParseData(LibXR::ConstRawData(packed_data));
    ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
    ASSERT(msg[1] == msg[0]);
    ASSERT(timestamp_us(sync_suber.GetTimestamp()) == timestamp_us(timestamp));
  }

  for (int i = 0; i < 1000; i++)
  {
    msg[0] = i * 0.1;
    const LibXR::MicrosecondTimestamp timestamp(20000 + i);
    topic.Publish(msg[0], timestamp);
    topic.DumpData(packed_data);
    for (uint8_t j = 0; j < 255; j++)
    {
      topic_server.ParseData(LibXR::ConstRawData(j));
    }
    for (int j = 0; j < static_cast<int>(sizeof(packed_data)); j++)
    {
      auto tmp = reinterpret_cast<uint8_t*>(&packed_data);
      topic_server.ParseData(LibXR::ConstRawData(tmp[j]));
    }
    ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
    ASSERT(msg[1] == msg[0]);
    ASSERT(timestamp_us(sync_suber.GetTimestamp()) == timestamp_us(timestamp));
  }
}
