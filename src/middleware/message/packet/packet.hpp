#pragma once

#include "../topic.hpp"

namespace LibXR
{
#ifndef __DOXYGEN__
#pragma pack(push, 1)
/**
 * @struct Topic::PackedDataHeader
 * @brief 固定 17 字节的消息包头 / Fixed 17-byte message header
 */
struct Topic::PackedDataHeader
{
  uint8_t prefix;  ///< 数据包前缀（固定为 0x5A）。Packet prefix (fixed at 0x5A).
  uint8_t data_len_raw[3];  ///< 大端 24 位无符号负载长度字段。Big-endian 24-bit unsigned payload length field.
  uint32_t
      topic_name_crc32;  ///< 主题名称的 CRC32 校验码。CRC32 checksum of the topic name.
  uint8_t timestamp_us_raw[8];  ///< 大端 64 位微秒时间戳字段。Big-endian 64-bit microsecond timestamp field.
  uint8_t pack_header_crc8;     ///< 头部 CRC8 校验码。CRC8 checksum of the header.

  /**
   * @brief 设置数据区长度 / Set the payload length
   * @param len 数据区长度 / Payload length
   */
  void SetDataLen(uint32_t len);

  /**
   * @brief 获取数据区长度 / Get the payload length
   * @return 数据区长度 / Returns the payload length
   */
  uint32_t GetDataLen() const;

  /**
   * @brief 设置报文时间戳 / Set the packet timestamp
   * @param timestamp 报文时间戳 / Packet timestamp
   */
  void SetTimestamp(MicrosecondTimestamp timestamp);

  /**
   * @brief 获取报文时间戳 / Get the packet timestamp
   * @return 报文时间戳 / Returns the packet timestamp
   */
  MicrosecondTimestamp GetTimestamp() const;
};

static_assert(sizeof(Topic::PackedDataHeader) == 17);
static_assert(offsetof(Topic::PackedDataHeader, prefix) == 0);
static_assert(offsetof(Topic::PackedDataHeader, data_len_raw) == 1);
static_assert(offsetof(Topic::PackedDataHeader, topic_name_crc32) == 4);
static_assert(offsetof(Topic::PackedDataHeader, timestamp_us_raw) == 8);
static_assert(offsetof(Topic::PackedDataHeader, pack_header_crc8) == 16);

/**
 * @class Topic::PackedData
 * @brief 一块内存：前 17 字节是包头，中间是 payload，最后 1 字节是 CRC8 / One memory
 *        object whose first 17 bytes are the header, whose middle part is the
 *        payload, and whose last byte is the trailing CRC8
 * @tparam Data 数据类型 / Type of the contained data
 */
template <typename Data>
class Topic::PackedData
{
  static_assert(TopicPayload<Data>);

 public:
#pragma pack(push, 1)
  /**
   * @struct raw
   * @brief 这一段就是“包头 + payload” / This block is exactly "header + payload"
   */
  struct
  {
    PackedDataHeader header_;     ///< 数据包头。Data packet header.
    uint8_t data_[sizeof(Data)];  ///< 主题数据。Topic data.
  } raw;

  uint8_t crc8_;  ///< 紧跟在负载后的尾部 CRC8。Trailing CRC8 stored after the payload.

#pragma pack(pop)

  /**
   * @brief 类型转换运算符，返回一份负载副本 / Type conversion operator returning one
   *        payload copy
   * @return 数据内容副本 / Returns the payload as a value
   */
  operator Data() { return *reinterpret_cast<Data*>(raw.data_); }

  /**
   * @brief 指针运算符，把负载当作 `Data` 访问 / Pointer operator exposing the payload
   *        as `Data`
   * @return 指向报文数据的指针 / Returns a pointer to the payload
   */
  Data* operator->() { return reinterpret_cast<Data*>(raw.data_); }

  /**
   * @brief 指针运算符，把负载当作只读 `Data` 访问 / Pointer operator exposing the
   *        payload as const `Data`
   * @return 指向常量报文数据的指针 / Returns a pointer to the const payload
   */
  const Data* operator->() const { return reinterpret_cast<const Data*>(raw.data_); }

  /**
   * @brief 获取打包消息的时间戳 / Get the timestamp stored in the packet
   * @return 打包消息的时间戳 / Returns the timestamp stored in the packet
   */
  MicrosecondTimestamp GetTimestamp() const { return raw.header_.GetTimestamp(); }

  /**
   * @brief 获取带时间戳的类型化消息副本 / Get a typed message copy with timestamp
   * @return 带时间戳的类型化消息副本 / Returns a typed message copy with timestamp
   */
  Message<Data> GetMessage() const
  {
    return Message<Data>{GetTimestamp(), *reinterpret_cast<const Data*>(raw.data_)};
  }
};
#pragma pack(pop)
#endif

/**
 * @brief 把当前 topic 里的最近一次数据拷成 `PackedData` / Copy the latest data kept by
 *        the current topic into `PackedData`
 * @tparam Data 数据类型 / Data type
 * @param data 用来接收结果的 `PackedData` / `PackedData` object that receives the result
 * @return 操作结果错误码 / Error code indicating the operation result
 * @note 这里要求 `sizeof(Data)` 和当前 topic 保存的 payload 大小完全一致；不一致会触发
 *       `ASSERT` /
 *       This requires `sizeof(Data)` to match the currently retained topic
 *       payload size exactly; a mismatch triggers `ASSERT`
 */
template <typename Data>
ErrorCode Topic::DumpData(PackedData<Data>& data)
{
  CheckTopicPayload<Data>();
  if (block_->data_.data.addr_ == nullptr)
  {
    return ErrorCode::EMPTY;
  }

  ASSERT(sizeof(Data) == block_->data_.data.size_);

  return DumpPacket<SizeLimitMode::NONE>(RawData(data));
}

/**
 * @brief 按尺寸规则把当前 topic 里的最近一次数据拷成打包报文 / Copy the latest data
 *        kept by the current topic into a packed message under one size rule
 * @tparam Mode 尺寸检查模式 / Size check mode
 * @param buffer 存储打包报文的原始缓冲区 / Raw buffer receiving the packed message
 * @return 操作结果错误码 / Error code indicating the operation result
 * @note `buffer` 的尺寸关系按 `Mode` 通过 `ASSERT` 检查，而不是通过返回错误码协商 /
 *       The required size relation of `buffer` is checked by `ASSERT` under
 *       `Mode`, not negotiated through a runtime error code
 */
template <SizeLimitMode Mode>
ErrorCode Topic::DumpPacket(RawData buffer)
{
  if (block_->data_.data.addr_ == nullptr)
  {
    return ErrorCode::EMPTY;
  }

  ASSERT(LibXR::SizeLimitCheck(Mode, PACK_BASE_SIZE + block_->data_.data.size_,
                               buffer.size_));
  Lock(block_);
  PackData(block_->data_.crc32, buffer, block_->data_.timestamp, block_->data_.data);
  Unlock(block_);

  return ErrorCode::OK;
}
}  // namespace LibXR
