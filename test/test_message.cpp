#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_message()
{
  auto domain = LibXR::Topic::Domain("test_domain");
  auto topic = LibXR::Topic::CreateTopic<double>("test_tp", &domain, false, true);
  static double msg[4];
  auto sync_suber = LibXR::Topic::SyncSubscriber<double>("test_tp", msg[1], &domain);
  LibXR::LockQueue<double> msg_queue(10);
  auto queue_suber = LibXR::Topic::QueuedSubscriber(topic, msg_queue);

  auto msg_cb = LibXR::Callback<LibXR::RawData &>::Create(
      [](bool, void *, LibXR::RawData &data)
      { msg[3] = *reinterpret_cast<const double *>(data.addr_); },
      reinterpret_cast<void *>(0));

  topic.RegisterCallback(msg_cb);

  msg[0] = 16.16;
  topic.Publish(msg[0]);
  ASSERT(sync_suber.Wait(10) == ErrorCode::OK);
  ASSERT(msg[1] == msg[0]);
  ASSERT(msg_queue.Size() == 1);
  msg_queue.Pop(msg[2], 0);
  ASSERT(msg[2] == msg[0]);
  ASSERT(msg[3] == msg[0]);

  topic.Publish(msg[0]);
  msg[1] = -1.0f;
  LibXR::Topic::PackedData<double> packed_data;
  LibXR::Topic::Server topic_server(512);

  topic.DumpData(packed_data);
  topic_server.Register(topic);

  topic_server.ParseData(LibXR::ConstRawData(packed_data));

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
      auto tmp = reinterpret_cast<uint8_t *>(&packed_data);
      topic_server.ParseData(LibXR::ConstRawData(tmp[j]));
    }
    ASSERT(msg[1] == msg[0]);
  }
}