#pragma once

#include "../topic.hpp"

namespace LibXR
{
/**
 * @class Topic::Server
 * @brief 把收到的字节流一点点拼成完整消息，再发布到已注册 topic / Parser that assembles
 *        incoming byte streams into complete messages and publishes them into
 *        registered topics
 */
class Topic::Server
{
 public:
  /**
   * @enum Status
   * @brief 解析器现在卡在哪一步 / Which step the parser is currently in
   */
  enum class Status : uint8_t
  {
    WAIT_START,    ///< 正在寻找下一个合法包前缀。Currently searching for the next valid packet prefix.
    WAIT_TOPIC,    ///< 已经对到包前缀，正在等剩下的包头字节。Packet prefix matched and the parser is waiting for the remaining header bytes.
    WAIT_DATA_CRC  ///< 包头已接受，正在等待负载加尾部 CRC8。Header accepted and waiting for the payload plus trailing CRC8.
  };

  /**
   * @brief 构造解析器并分配内部字节队列 / Construct the parser and allocate its internal
   *        byte queue
   * @param buffer_length 缓冲区长度 / Buffer length
   * @note 包含初始化期动态内存分配，server 应长期存在 / Contains initialization-time dynamic allocation; servers are expected to be long-lived
   * @note `buffer_length` 必须大于 `PACK_BASE_SIZE`，这样才能至少容纳一个空 payload
   *       包 /
   *       `buffer_length` must be greater than `PACK_BASE_SIZE` so the parser
   *       can hold at least one empty-payload packet
   */
  Server(size_t buffer_length);

  /**
   * @brief 注册一个 topic，让解析出来的包可以投递到它 / Register one topic so parsed
   *        packets can be delivered into it
   * @param topic 需要注册的主题句柄 / Topic handle to register
   * @note 包含初始化期动态内存分配，注册关系应长期存在 / Contains initialization-time dynamic allocation; registrations are expected to be long-lived
   * @note 这里只保存 `topic` 句柄，不复制 topic 内容；topic 本身必须至少活到
   *       `Server` 不再使用这条注册关系为止 /
   *       This stores only the `topic` handle and does not copy topic contents;
   *       the topic itself must outlive the server's use of this registration
   */
  void Register(TopicHandle topic);

  /**
   * @brief 把这一批新字节喂给解析器 / Feed one new batch of bytes into the parser
   * @param data 新收到的原始字节 / Newly received raw bytes
   * @return 成功解析并发布的消息数量 / Number of messages parsed and published
   *         successfully
   * @note 解析使用构造时分配的固定队列和缓冲区；热路径不分配 / Parsing uses the fixed queue and buffer allocated by the constructor; the hot path does not allocate
   * @note 若内部队列空间不足，这一批新字节根本不会写进队列，解析器只会继续消费之前已经
   *       缓存的内容 /
   *       If the internal queue does not have enough space, this new byte batch
   *       is not written into the queue at all and the parser only keeps
   *       consuming bytes that were already buffered
   * @note 若收到的 packet payload 长于注册 topic 的 `max_length`，当前实现会截断到
   *       `max_length` 后再发布 /
   *       If an incoming packet payload is longer than the registered topic's
   *       `max_length`, the current implementation truncates it to
   *       `max_length` before publishing
   * @note 头 CRC、尾 CRC、未知 topic 或超出内部队列上限的包会被直接丢掉，解析器随后继续
   *       寻找下一包 /
   *       Packets with bad header CRC, bad trailing CRC, unknown topics, or
   *       sizes beyond the internal queue limit are dropped directly, then the
   *       parser continues searching for the next packet
   */
  size_t ParseData(ConstRawData data);

  /**
   * @brief 在回调路径里把这一批新字节喂给解析器 / Feed one new batch of bytes into the
   *        parser from a callback path
   * @param data 新收到的原始字节 / Newly received raw bytes
   * @param in_isr 当前回调是否运行在中断上下文 / Whether the callback runs in ISR context
   * @return 成功解析并发布的消息数量 / Number of messages parsed and published
   *         successfully
   * @note 解析本身仍使用构造时分配的固定队列和缓冲区 / Parsing still uses the fixed queue and buffer allocated by the constructor
   * @note 成功拼出的消息会直接走 `Topic::PublishFromCallback()`，所以订阅者看到的仍然是
   *       这次回调路径自己的上下文 /
   *       Successfully assembled messages go straight through
   *       `Topic::PublishFromCallback()`, so subscribers still see the same
   *       callback-path context of this invocation
   * @note 注册到这个 `Server` 的 topic 必须本来就允许走 `Topic::PublishFromCallback()`
   *       这条路径 /
   *       Topics registered to this `Server` must already be valid for the
   *       `Topic::PublishFromCallback()` path
   * @note 若收到的 packet payload 长于注册 topic 的 `max_length`，当前实现会截断到
   *       `max_length` 后再发布 /
   *       If an incoming packet payload is longer than the registered topic's
   *       `max_length`, the current implementation truncates it to
   *       `max_length` before publishing
   * @note 头 CRC、尾 CRC、未知 topic 或超出内部队列上限的包会被直接丢掉，解析器随后继续
   *       寻找下一包 /
   *       Packets with bad header CRC, bad trailing CRC, unknown topics, or
   *       sizes beyond the internal queue limit are dropped directly, then the
   *       parser continues searching for the next packet
   */
  size_t ParseDataFromCallback(ConstRawData data, bool in_isr);

 private:
  /**
   * @enum ParseResult
   * @brief 单次负载解析结果 / Result of one payload parse step
   */
  enum class ParseResult : uint8_t
  {
    NEED_MORE,  ///< 当前包尚未收全，需要更多输入字节。The current packet is incomplete and needs more input bytes.
    DROPPED,    ///< 当前包被丢弃，解析器继续寻找下一个起点。The current packet is dropped and the parser keeps searching for the next start.
    DELIVERED   ///< 当前包已成功发布给对应主题。The current packet has been published to its topic successfully.
  };

  /**
   * @brief 真正执行解析循环，把一批新字节推进状态机 / Internal parsing loop that
   *        pushes one new byte batch through the state machine
   * @param data 原始输入批次 / Raw input batch
   * @param from_callback 是否来自回调发布路径 / Whether parsing is invoked from a callback path
   * @param in_isr 当前回调是否运行在中断上下文 / Whether the current callback runs in ISR context
   * @return 成功解析并发布的消息数量 / Number of messages parsed and published
   *         successfully
   */
  size_t ParseDataRaw(ConstRawData data, bool from_callback, bool in_isr);

  /**
   * @brief 丢掉队列前面的杂字节，直到看到下一个合法包前缀 / Drop leading garbage
   *        bytes until the next valid packet prefix is found
   * @return 找到新的包起点返回 `true`，否则返回 `false` / Returns `true` when a new packet start is found, otherwise `false`
   */
  bool SyncToPacketStart();

  /**
   * @brief 读完整包头并检查它能不能继续收 payload / Read the full packet header and
   *        check whether payload reception can continue
   * @return 成功读取并接受当前包头返回 `true`，否则返回 `false` / Returns `true` when the current packet header is accepted, otherwise `false`
   */
  bool ReadHeader();

  /**
   * @brief 读出当前 payload 加尾 CRC8，并决定下一步 / Read the current payload plus
   *        trailing CRC8 and decide what to do next
   * @param from_callback 是否来自回调发布路径 / Whether parsing is invoked from a callback path
   * @param in_isr 当前回调是否运行在中断上下文 / Whether the current callback runs in ISR context
   * @return 当前这包的处理结果 / Result of handling the current packet payload
   */
  ParseResult ReadPayload(bool from_callback, bool in_isr);

  /**
   * @brief 把解析器退回“重新找包前缀”的初始状态 / Reset the parser back to the initial
   *        "search for packet prefix" state
   */
  void ResetParser();

  Status status_ = Status::WAIT_START;  ///< 当前解析器停留在哪一步。Current parser step.
  uint32_t data_len_ = 0;  ///< 当前待接收的数据长度 / Current pending payload length
  RBTree<uint32_t> topic_map_;  ///< 从 topic CRC32 到已注册主题句柄的映射。Map from topic CRC32 to registered topic handles.
  BaseQueue queue_;             ///< 输入字节队列 / Input byte queue
  RawData parse_buff_;          ///< 当前正在组包的临时缓冲区。Temporary buffer holding the packet currently being assembled.
  TopicHandle current_topic_ = nullptr;  ///< 当前这包准备投递到的主题句柄。Topic handle targeted by the current packet.
  MicrosecondTimestamp current_timestamp_;  ///< 当前这包头里带的消息时间戳。Message timestamp carried by the current packet header.
};
}  // namespace LibXR
