#pragma once

#include "message/message.hpp"

namespace LibXR
{
#ifndef __DOXYGEN__
#pragma pack(push, 1)
/**
 * @class Topic::PackedData
 * @brief  主题数据包，包含数据和校验码
 *         Packed data structure containing data and checksum
 * @tparam Data 数据类型
 *         Type of the contained data
 * \addtogroup LibXR
 */
template <typename Data>
class Topic::PackedData
{
 public:
#pragma pack(push, 1)
  /**
   * @struct raw
   * @brief 内部数据结构，包含数据包头和实际数据。Internal structure containing data
   * header and actual data.
   */
  struct
  {
    PackedDataHeader header_;     ///< 数据包头。Data packet header.
    uint8_t data_[sizeof(Data)];  ///< 主题数据。Topic data.
  } raw;

  uint8_t crc8_;  ///< 数据包的 CRC8 校验码。CRC8 checksum of the data packet.

#pragma pack(pop)

  /**
   * @brief 赋值运算符，设置数据并计算 CRC8 校验值。Assignment operator setting data and
   * computing CRC8 checksum.
   * @param data 要赋值的数据。Data to be assigned.
   * @return 赋值后的数据。The assigned data.
   */
  PackedData& operator=(const Data& data)
  {
    LibXR::Memory::FastCopy(raw.data_, &data, sizeof(Data));
    crc8_ = CRC8::Calculate(&raw, sizeof(raw));
    return *this;
  }

  /**
   * @brief 类型转换运算符，返回数据内容。Type conversion operator returning the data
   * content.
   */
  operator Data() { return *reinterpret_cast<Data*>(raw.data_); }

  /**
   * @brief 指针运算符，访问数据成员。Pointer operator for accessing data members.
   */
  Data* operator->() { return reinterpret_cast<Data*>(raw.data_); }

  /**
   * @brief 指针运算符，访问数据成员（常量版本）。Pointer operator for accessing data
   * members (const version).
   */
  const Data* operator->() const { return reinterpret_cast<const Data*>(raw.data_); }
};
#pragma pack(pop)
#endif

template <typename Data>
ErrorCode Topic::DumpData(PackedData<Data>& data)
{
  if (block_->data_.data.addr_ == nullptr)
  {
    return ErrorCode::EMPTY;
  }

  ASSERT(sizeof(Data) == block_->data_.data.size_);

  return DumpData<SizeLimitMode::NONE>(RawData(data), true);
}
}  // namespace LibXR
