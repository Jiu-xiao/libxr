#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_message()
{
  constexpr size_t PACKET_SIZE = LibXR::Topic::PACK_BASE_SIZE + sizeof(double);
  ASSERT(LibXR::Topic::Find("missing_default_topic") == nullptr);

  auto domain = LibXR::Topic::Domain("test_domain");
  auto topic = LibXR::Topic::CreateTopic<double>("test_tp", &domain, false, true);
  static double msg[4];
  auto sync_suber = LibXR::Topic::SyncSubscriber<double>("test_tp", msg[1], &domain);
  auto async_suber = LibXR::Topic::ASyncSubscriber<double>(topic);
  LibXR::LockFreeQueue<double> msg_queue(10);
  auto queue_suber = LibXR::Topic::QueuedSubscriber(topic, msg_queue);
  static bool cb_in_isr = false;

  auto msg_cb = LibXR::Topic::Callback::Create(
      [](bool in_isr, void*, LibXR::RawData& data)
      {
        cb_in_isr = in_isr;
        msg[3] = *reinterpret_cast<const double*>(data.addr_);
      },
      reinterpret_cast<void*>(0));

  topic.RegisterCallback(msg_cb);

  ASSERT(!async_suber.Available());

  msg[0] = 16.16;
  async_suber.StartWaiting();
  topic.Publish(msg[0]);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(async_suber.Available());
  ASSERT(msg[1] == msg[0]);
  ASSERT(async_suber.GetData() == msg[0]);
  ASSERT(!async_suber.Available());
  ASSERT(msg_queue.Size() == 1);
  msg_queue.Pop(msg[2]);
  ASSERT(msg[2] == msg[0]);
  ASSERT(msg[3] == msg[0]);
  ASSERT(!cb_in_isr);

  msg[0] = 32.32;
  msg[1] = -1.0f;
  msg[2] = -1.0f;
  msg[3] = -1.0f;
  async_suber.StartWaiting();
  topic.PublishFromCallback(msg[0], true);
  ASSERT(sync_suber.Wait(10) == LibXR::ErrorCode::OK);
  ASSERT(async_suber.Available());
  ASSERT(msg[1] == msg[0]);
  ASSERT(async_suber.GetData() == msg[0]);
  ASSERT(msg_queue.Size() == 1);
  msg_queue.Pop(msg[2]);
  ASSERT(msg[2] == msg[0]);
  ASSERT(msg[3] == msg[0]);
  ASSERT(cb_in_isr);

  topic.Publish(msg[0]);
  ASSERT(!async_suber.Available());
  msg[1] = -1.0f;
  LibXR::Topic::PackedData<double> packed_data;
  LibXR::Topic::Server topic_server(512);

  topic.DumpData(packed_data);
  topic_server.Register(topic);

  topic_server.ParseData(LibXR::ConstRawData(packed_data));

  ASSERT(msg[1] == msg[0]);

  msg[0] = 64.64;
  topic.Publish(msg[0]);
  topic.DumpData(packed_data);
  msg[1] = -1.0f;
  auto* packet = reinterpret_cast<uint8_t*>(&packed_data);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packet, 3)) == 0);
  ASSERT(msg[1] == -1.0f);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packet + 3, PACKET_SIZE - 3)) == 1);
  ASSERT(msg[1] == msg[0]);

  LibXR::Topic::Server exact_size_server(PACKET_SIZE);
  exact_size_server.Register(topic);
  msg[1] = -1.0f;
  ASSERT(exact_size_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(msg[1] == msg[0]);

  auto borrowed_topic =
      LibXR::Topic::CreateTopic<double>("borrowed_tp", &domain, false, false, true);
  double borrowed_source = 11.11;
  double borrowed_dump = 0.0;
  borrowed_topic.Publish(borrowed_source);
  ASSERT(borrowed_topic.DumpData(borrowed_dump) == LibXR::ErrorCode::OK);
  ASSERT(borrowed_dump == borrowed_source);
  borrowed_source = 22.22;
  ASSERT(borrowed_topic.DumpData(borrowed_dump) == LibXR::ErrorCode::OK);
  ASSERT(borrowed_dump == borrowed_source);

  auto queue_drop_topic =
      LibXR::Topic::CreateTopic<int>("queue_drop_tp", &domain, false, false, true);
  LibXR::LockFreeQueue<int> drop_queue(1);
  auto drop_suber = LibXR::Topic::QueuedSubscriber(queue_drop_topic, drop_queue);
  for (size_t i = 0; i < drop_queue.MaxSize(); i++)
  {
    auto value = static_cast<int>(i);
    queue_drop_topic.Publish(value);
  }
  ASSERT(drop_queue.Size() == drop_queue.MaxSize());
  int dropped_value = -123;
  queue_drop_topic.Publish(dropped_value);
  ASSERT(drop_queue.Size() == drop_queue.MaxSize());
  for (size_t i = 0; i < drop_queue.MaxSize(); i++)
  {
    int value = -1;
    ASSERT(drop_queue.Pop(value) == LibXR::ErrorCode::OK);
    ASSERT(value == static_cast<int>(i));
  }
  ASSERT(drop_queue.Pop(dropped_value) == LibXR::ErrorCode::EMPTY);

  auto unknown_topic_packet = packed_data;
  unknown_topic_packet.raw.header_.topic_name_crc32 ^= 0x13572468;
  unknown_topic_packet.raw.header_.pack_header_crc8 = LibXR::CRC8::Calculate(
      &unknown_topic_packet.raw,
      sizeof(LibXR::Topic::PackedDataHeader) - sizeof(uint8_t));
  unknown_topic_packet.crc8_ =
      LibXR::CRC8::Calculate(&unknown_topic_packet, PACKET_SIZE - sizeof(uint8_t));
  msg[1] = -1.0f;
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(unknown_topic_packet)) == 0);
  ASSERT(msg[1] == -1.0f);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(msg[1] == msg[0]);

  auto bad_header_packet = packed_data;
  bad_header_packet.raw.header_.pack_header_crc8 ^= 0x5A;
  msg[1] = -1.0f;
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(bad_header_packet)) == 0);
  ASSERT(msg[1] == -1.0f);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(msg[1] == msg[0]);

  auto bad_payload_packet = packed_data;
  bad_payload_packet.crc8_ ^= 0xA5;
  msg[1] = -1.0f;
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(bad_payload_packet)) == 0);
  ASSERT(msg[1] == -1.0f);
  ASSERT(topic_server.ParseData(LibXR::ConstRawData(packed_data)) == 1);
  ASSERT(msg[1] == msg[0]);

  for (int i = 0; i < 1000; i++)
  {
    msg[0] = i * 0.1;
    topic.Publish(msg[0]);
    topic.DumpData(packed_data);
    msg[1] = -1;
    topic_server.ParseData(LibXR::ConstRawData(packed_data));
    ASSERT(msg[1] == msg[0]);
  }

  for (int i = 0; i < 1000; i++)
  {
    msg[0] = i * 0.1;
    topic.Publish(msg[0]);
    topic.DumpData(packed_data);
    msg[1] = -1;
    for (uint8_t j = 0; j < 255; j++)
    {
      topic_server.ParseData(LibXR::ConstRawData(j));
    }
    for (int j = 0; j < sizeof(packed_data); j++)
    {
      auto tmp = reinterpret_cast<uint8_t*>(&packed_data);
      topic_server.ParseData(LibXR::ConstRawData(tmp[j]));
    }
    ASSERT(msg[1] == msg[0]);
  }
}
