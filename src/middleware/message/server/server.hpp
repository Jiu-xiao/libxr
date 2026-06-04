#pragma once

#include "../topic.hpp"

#include "queue.hpp"

namespace LibXR
{
class Topic::Server
{
 public:
  enum class Status : uint8_t
  {
    WAIT_START,
    WAIT_TOPIC,
    WAIT_DATA_CRC,
  };

  Server(size_t buffer_length);
  void Register(TopicHandle topic);
  size_t ParseData(ConstRawData data);
  size_t ParseDataFromCallback(ConstRawData data, bool in_isr);

 private:
  enum class ParseResult : uint8_t
  {
    NEED_MORE,
    DROPPED,
    DELIVERED
  };

  size_t ParseDataRaw(ConstRawData data, bool from_callback, bool in_isr);
  bool SyncToPacketStart();
  bool ReadHeader();
  ParseResult ReadPayload(bool from_callback, bool in_isr);
  void ResetParser();

  Status status_ = Status::WAIT_START;
  uint32_t data_len_ = 0;
  RBTree<uint32_t> topic_map_;
  BaseQueue queue_;
  RawData parse_buff_;
  TopicHandle current_topic_ = nullptr;
  MicrosecondTimestamp current_timestamp_;
};
}  // namespace LibXR
