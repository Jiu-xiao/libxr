#pragma once

#include "message/message.hpp"

namespace LibXR
{
/**
 * @class Topic::Server
 * @brief  服务器类，负责解析数据并将其分发到相应的主题
 *         Server class responsible for parsing data and distributing it to
 * corresponding topics
 */
class Topic::Server
{
 public:
  /**
   * @enum Status
   * @brief  服务器解析状态枚举
   *         Enumeration of server parsing states
   */
  enum class Status : uint8_t
  {
    WAIT_START,    ///< 等待起始标志 Waiting for start flag
    WAIT_TOPIC,    ///< 等待主题信息 Waiting for topic information
    WAIT_DATA_CRC  ///< 等待数据校验 Waiting for data CRC validation
  };

  /**
   * @brief  构造函数，初始化服务器并分配缓冲区
   *         Constructor to initialize the server and allocate buffer
   * @param  buffer_length 缓冲区长度 Buffer length
   *
   * @note 包含初始化期动态内存分配，server 应长期存在。
   *       Contains initialization-time dynamic allocation; servers are expected to be
   *       long-lived.
   */
  Server(size_t buffer_length);

  /**
   * @brief  注册一个主题
   *         Registers a topic
   * @param  topic 需要注册的主题句柄 The topic handle to register
   *
   * @note 包含初始化期动态内存分配，注册关系应长期存在。
   *       Contains initialization-time dynamic allocation; registrations are expected
   *       to be long-lived.
   */
  void Register(TopicHandle topic);

  /**
   * @brief  解析接收到的数据
   *         Parses received data
   * @param  data 接收到的原始数据 Received raw data
   * @return 接收到的话题数量 Received topic count
   *
   * @note 解析使用构造时分配的固定队列和缓冲区；热路径不分配。若内部队列空间不足，
   *       新输入批次按 BaseQueue::PushBatch() 的现有语义不写入。
   *       Parsing uses the fixed queue and buffer allocated by the constructor; the hot
   *       path does not allocate. If the internal queue has insufficient space, the new
   *       input batch is not written according to BaseQueue::PushBatch() semantics.
   */
  size_t ParseData(ConstRawData data);

 private:
  enum class ParseResult : uint8_t
  {
    NEED_MORE,
    DROPPED,
    DELIVERED
  };

  bool SyncToPacketStart();
  bool ReadHeader();
  ParseResult ReadPayload();
  void ResetParser();

  Status status_ =
      Status::WAIT_START;  ///< 服务器的当前解析状态 Current parsing state of the server
  uint32_t data_len_ = 0;  ///< 当前数据长度 Current data length
  RBTree<uint32_t> topic_map_;           ///< 主题映射表 Topic mapping table
  BaseQueue queue_;                      ///< 数据队列 Data queue
  RawData parse_buff_;                   ///< 解析数据缓冲区 Data buffer for parsing
  TopicHandle current_topic_ = nullptr;  ///< 当前主题句柄 Current topic handle
};
}  // namespace LibXR
