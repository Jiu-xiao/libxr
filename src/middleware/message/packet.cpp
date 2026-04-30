#include "libxr_mem.hpp"
#include "message.hpp"

using namespace LibXR;

void Topic::PackedDataHeader::SetDataLen(uint32_t len)
{
  data_len_raw[0] = static_cast<uint8_t>(len >> 16);
  data_len_raw[1] = static_cast<uint8_t>(len >> 8);
  data_len_raw[2] = static_cast<uint8_t>(len);
}

uint32_t Topic::PackedDataHeader::GetDataLen() const
{
  return static_cast<uint32_t>(data_len_raw[0]) << 16 |
         static_cast<uint32_t>(data_len_raw[1]) << 8 |
         static_cast<uint32_t>(data_len_raw[2]);
}

void Topic::PackedDataHeader::SetTimestamp(MicrosecondTimestamp timestamp)
{
  uint64_t value = static_cast<uint64_t>(timestamp);
  for (size_t i = 0; i < sizeof(timestamp_us_raw); i++)
  {
    timestamp_us_raw[i] =
        static_cast<uint8_t>(value >> ((sizeof(timestamp_us_raw) - 1U - i) * 8U));
  }
}

MicrosecondTimestamp Topic::PackedDataHeader::GetTimestamp() const
{
  uint64_t value = 0;
  for (uint8_t byte : timestamp_us_raw)
  {
    value = (value << 8U) | static_cast<uint64_t>(byte);
  }
  return MicrosecondTimestamp(value);
}

void Topic::PackData(uint32_t topic_name_crc32, RawData buffer,
                     MicrosecondTimestamp timestamp, ConstRawData data)
{
  PackedData<uint8_t>* pack = reinterpret_cast<PackedData<uint8_t>*>(buffer.addr_);

  LibXR::Memory::FastCopy(&pack->raw.data_, data.addr_, data.size_);

  pack->raw.header_.prefix = PACKET_PREFIX;
  pack->raw.header_.topic_name_crc32 = topic_name_crc32;
  pack->raw.header_.SetDataLen(data.size_);
  pack->raw.header_.SetTimestamp(timestamp);
  pack->raw.header_.pack_header_crc8 =
      CRC8::Calculate(&pack->raw, sizeof(PackedDataHeader) - sizeof(uint8_t));
  uint8_t* crc8_pack = reinterpret_cast<uint8_t*>(
      reinterpret_cast<uint8_t*>(pack) + PACK_BASE_SIZE + data.size_ - sizeof(uint8_t));
  *crc8_pack = CRC8::Calculate(pack, PACK_BASE_SIZE - sizeof(uint8_t) + data.size_);
}
