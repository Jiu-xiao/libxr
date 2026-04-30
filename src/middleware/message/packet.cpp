#include "libxr_mem.hpp"
#include "message/message.hpp"

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

void Topic::PackData(uint32_t topic_name_crc32, RawData buffer, RawData source)
{
  PackedData<uint8_t>* pack = reinterpret_cast<PackedData<uint8_t>*>(buffer.addr_);

  LibXR::Memory::FastCopy(&pack->raw.data_, source.addr_, source.size_);

  pack->raw.header_.prefix = PACKET_PREFIX;
  pack->raw.header_.topic_name_crc32 = topic_name_crc32;
  pack->raw.header_.SetDataLen(source.size_);
  pack->raw.header_.pack_header_crc8 =
      CRC8::Calculate(&pack->raw, sizeof(PackedDataHeader) - sizeof(uint8_t));
  uint8_t* crc8_pack = reinterpret_cast<uint8_t*>(
      reinterpret_cast<uint8_t*>(pack) + PACK_BASE_SIZE + source.size_ - sizeof(uint8_t));
  *crc8_pack = CRC8::Calculate(pack, PACK_BASE_SIZE - sizeof(uint8_t) + source.size_);
}
