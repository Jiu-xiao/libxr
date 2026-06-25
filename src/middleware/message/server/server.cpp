#include "server.hpp"

#include <cstddef>
#include <cstdint>

#include "../packet/packet.hpp"
#include "crc.hpp"
#include "libxr_mem.hpp"

using namespace LibXR;

Topic::Server::Server(size_t buffer_length)
    : topic_map_([](const uint32_t& a, const uint32_t& b)
                 { return static_cast<int>(a) - static_cast<int>(b); }),
      queue_(1, buffer_length)
{
  ASSERT(buffer_length > PACK_BASE_SIZE);
  parse_buff_.size_ = buffer_length;
  parse_buff_.addr_ =
      new (std::align_val_t(LibXR::CACHE_LINE_SIZE)) uint8_t[parse_buff_.size_];
}

void Topic::Server::Register(TopicHandle topic)
{
  ASSERT(topic != nullptr);
  ASSERT(topic->data_.payload_size != 0);
  ASSERT(topic->data_.payload_alignment != 0);
  ASSERT(topic->data_.payload_alignment <= LibXR::CACHE_LINE_SIZE);

  ASSERT(topic->data_.payload_size + PACK_BASE_SIZE <= parse_buff_.size_);

  auto* node = new RBTree<uint32_t>::Node<TopicHandle>(topic);
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

  (void)queue_.PushBatchBytes(data.addr_, data.size_);

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
}

bool Topic::Server::SyncToPacketStart()
{
  auto queue_size = queue_.Size();
  for (uint32_t i = 0; i < queue_size; i++)
  {
    uint8_t prefix = 0;
    queue_.PeekBytes(&prefix);
    if (prefix == PACKET_PREFIX)
    {
      status_ = Status::WAIT_TOPIC;
      return true;
    }
    queue_.PopBytes();
  }

  return false;
}

bool Topic::Server::ReadHeader()
{
  if (queue_.Size() < sizeof(PackedDataHeader))
  {
    return false;
  }

  queue_.PopBatchBytes(parse_buff_.addr_, sizeof(PackedDataHeader));
  if (!CRC8::Verify(parse_buff_.addr_, sizeof(PackedDataHeader)))
  {
    ResetParser();
    return true;
  }

  auto* header = reinterpret_cast<PackedDataHeader*>(parse_buff_.addr_);
  if (header->version != PACKET_VERSION)
  {
    ResetParser();
    return true;
  }

  auto* node = topic_map_.Search<TopicHandle>(header->topic_name_crc32);
  if (node == nullptr)
  {
    ResetParser();
    return true;
  }

  data_len_ = header->GetDataLen();
  current_timestamp_ = header->GetTimestamp();
  current_topic_ = *node;
  const auto target_size = current_topic_->data_.payload_size;

  if (target_size + PACK_BASE_SIZE > parse_buff_.size_)
  {
    ResetParser();
    return true;
  }

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

  auto* payload_addr =
      reinterpret_cast<uint8_t*>(parse_buff_.addr_) + sizeof(PackedDataHeader);
  queue_.PopBatchBytes(payload_addr, data_len_ + sizeof(uint8_t));

  if (!CRC8::Verify(parse_buff_.addr_,
                    data_len_ + sizeof(PackedDataHeader) + sizeof(uint8_t)))
  {
    ResetParser();
    return ParseResult::DROPPED;
  }

  const auto target_size = current_topic_->data_.payload_size;
  void* publish_addr = payload_addr;
  if (reinterpret_cast<uintptr_t>(payload_addr) %
          current_topic_->data_.payload_alignment !=
      0)
  {
    publish_addr = parse_buff_.addr_;
    if (data_len_ >= target_size)
    {
      LibXR::Memory::FastMove(publish_addr, payload_addr, target_size);
    }
    else
    {
      LibXR::Memory::FastMove(publish_addr, payload_addr, data_len_);
    }
  }

  auto topic = Topic(current_topic_);
  if (from_callback)
  {
    topic.PublishBytesFromServerCallback(publish_addr, target_size, current_timestamp_,
                                         in_isr);
  }
  else
  {
    topic.PublishBytesFromServer(publish_addr, target_size, current_timestamp_);
  }

  ResetParser();
  return ParseResult::DELIVERED;
}

void Topic::Server::ResetParser()
{
  status_ = Status::WAIT_START;
  data_len_ = 0;
  current_topic_ = nullptr;
  current_timestamp_ = MicrosecondTimestamp();
}
