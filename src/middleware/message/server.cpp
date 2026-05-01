#include "message/message.hpp"

using namespace LibXR;

Topic::Server::Server(size_t buffer_length)
    : topic_map_([](const uint32_t& a, const uint32_t& b)
                 { return static_cast<int>(a) - static_cast<int>(b); }),
      queue_(1, buffer_length)
{
  /* Minimum size: header8 + crc32 + length24 + crc8 + data +  crc8 = 10 */
  ASSERT(buffer_length > PACK_BASE_SIZE);
  parse_buff_.size_ = buffer_length;
  parse_buff_.addr_ = new uint8_t[buffer_length];
}

void Topic::Server::Register(TopicHandle topic)
{
  auto node = new RBTree<uint32_t>::Node<TopicHandle>(topic);
  topic_map_.Insert(*node, topic->key);
}

size_t Topic::Server::ParseData(ConstRawData data)
{
  return ParseDataRaw(data, false, false);
}

size_t Topic::Server::ParseDataFromCallback(ConstRawData data, bool in_isr)
{
  return ParseDataRaw(data, true, in_isr);
}

size_t Topic::Server::ParseDataRaw(ConstRawData data, bool from_callback, bool in_isr)
{
  size_t count = 0;

  /* Preserve legacy backpressure behavior: if the parser queue is full, this batch is
   * not appended and parsing continues with bytes that were already buffered. */
  queue_.PushBatch(data.addr_, data.size_);

  while (true)
  {
    if (status_ == Status::WAIT_START && !SyncToPacketStart())
    {
      return count;
    }

    if (status_ == Status::WAIT_TOPIC && !ReadHeader())
    {
      return count;
    }

    if (status_ == Status::WAIT_DATA_CRC)
    {
      switch (ReadPayload(from_callback, in_isr))
      {
        case ParseResult::NEED_MORE:
          return count;
        case ParseResult::DROPPED:
          continue;
        case ParseResult::DELIVERED:
          count++;
          continue;
      }
    }
  }
  return count;
}

bool Topic::Server::SyncToPacketStart()
{
  auto queue_size = queue_.Size();
  for (uint32_t i = 0; i < queue_size; i++)
  {
    uint8_t prefix = 0;
    queue_.Peek(&prefix);
    if (prefix == PACKET_PREFIX)
    {
      status_ = Status::WAIT_TOPIC;
      return true;
    }
    queue_.Pop();
  }

  return false;
}

bool Topic::Server::ReadHeader()
{
  if (queue_.Size() < sizeof(PackedDataHeader))
  {
    return false;
  }

  queue_.PopBatch(parse_buff_.addr_, sizeof(PackedDataHeader));
  if (!CRC8::Verify(parse_buff_.addr_, sizeof(PackedDataHeader)))
  {
    ResetParser();
    return true;
  }

  auto header = reinterpret_cast<PackedDataHeader*>(parse_buff_.addr_);
  auto node = topic_map_.Search<TopicHandle>(header->topic_name_crc32);
  if (node == nullptr)
  {
    ResetParser();
    return true;
  }

  data_len_ = header->GetDataLen();
  current_topic_ = *node;
  if (data_len_ + PACK_BASE_SIZE > queue_.length_)
  {
    ResetParser();
    return true;
  }

  status_ = Status::WAIT_DATA_CRC;
  return true;
}

Topic::Server::ParseResult Topic::Server::ReadPayload(bool from_callback, bool in_isr)
{
  if (queue_.Size() < data_len_ + sizeof(uint8_t))
  {
    return ParseResult::NEED_MORE;
  }

  auto data = reinterpret_cast<uint8_t*>(parse_buff_.addr_) + sizeof(PackedDataHeader);
  queue_.PopBatch(data, data_len_ + sizeof(uint8_t));

  if (!CRC8::Verify(parse_buff_.addr_,
                    data_len_ + sizeof(PackedDataHeader) + sizeof(uint8_t)))
  {
    ResetParser();
    return ParseResult::DROPPED;
  }

  if (data_len_ > current_topic_->data_.max_length)
  {
    data_len_ = current_topic_->data_.max_length;
  }
  auto topic = Topic(current_topic_);
  if (from_callback)
  {
    topic.PublishFromCallback(data, data_len_, in_isr);
  }
  else
  {
    topic.Publish(data, data_len_);
  }
  ResetParser();
  return ParseResult::DELIVERED;
}

void Topic::Server::ResetParser()
{
  status_ = Status::WAIT_START;
  data_len_ = 0;
  current_topic_ = nullptr;
}
