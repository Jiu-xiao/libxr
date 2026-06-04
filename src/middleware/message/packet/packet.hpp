#pragma once

#include "../topic.hpp"

namespace LibXR
{
#ifndef __DOXYGEN__
#pragma pack(push, 1)
struct Topic::PackedDataHeader
{
  uint8_t prefix;
  uint8_t data_len_raw[3];
  uint32_t topic_name_crc32;
  uint8_t timestamp_us_raw[8];
  uint8_t pack_header_crc8;

  void SetDataLen(uint32_t len);
  uint32_t GetDataLen() const;
  void SetTimestamp(MicrosecondTimestamp timestamp);
  MicrosecondTimestamp GetTimestamp() const;
};

static_assert(sizeof(Topic::PackedDataHeader) == 17);
static_assert(offsetof(Topic::PackedDataHeader, prefix) == 0);
static_assert(offsetof(Topic::PackedDataHeader, data_len_raw) == 1);
static_assert(offsetof(Topic::PackedDataHeader, topic_name_crc32) == 4);
static_assert(offsetof(Topic::PackedDataHeader, timestamp_us_raw) == 8);
static_assert(offsetof(Topic::PackedDataHeader, pack_header_crc8) == 16);

template <typename Data>
class Topic::PackedData
{
  static_assert(TopicPayload<Data>);

 public:
  struct
  {
    PackedDataHeader header_;
    uint8_t data_[sizeof(Data)];
  } raw;

  uint8_t crc8_;

  MicrosecondTimestamp GetTimestamp() const { return raw.header_.GetTimestamp(); }
};
#pragma pack(pop)
#endif

template <typename Data>
ErrorCode Topic::PackData(const Data& data, PackedData<Data>& packet,
                          MicrosecondTimestamp timestamp)
{
  CheckTopicPayload<Data>();
  ASSERT(block_ != nullptr);
  ASSERT(block_->data_.payload_type_id == TypeID::GetID<Data>());
  ASSERT(block_->data_.payload_size == sizeof(Data));

  PackBytes(block_->data_.crc32, RawData(packet), timestamp, ConstRawData(data));
  return ErrorCode::OK;
}
}  // namespace LibXR
