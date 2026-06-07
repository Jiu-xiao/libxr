#pragma once

#include <cstddef>
#include <type_traits>

#include "spsc_queue_core.hpp"

namespace LibXR
{
/**
 * @class SPSCQueue
 * @brief 单生产者单消费者无锁队列 / Single-producer single-consumer lock-free queue
 *
 * 模板壳只把 `Data` 映射为固定大小的字节 payload 并复用 `SPSCQueueCore`，
 * 不在队列内部管理 `Data` 对象生命周期。调用方必须保证 payload 可以按该
 * 项目的队列契约进行字节搬运。
 *
 * This template wrapper maps `Data` to a fixed-size byte payload and reuses
 * `SPSCQueueCore`. It does not manage `Data` object lifetime inside the queue.
 * The caller must ensure that the payload is valid for this project's
 * byte-moving queue contract.
 */
template <typename Data>
class SPSCQueue
{
 public:
  using ValueType = Data;  ///< 队列元素类型 / Queue element type.

  /**
   * @brief 构造一个 SPSC 队列 / Construct one SPSC queue
   * @param length 队列容量 / Queue capacity
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  explicit SPSCQueue(size_t length) : core_(sizeof(Data), length)
  {
  }

  /**
   * @brief 析构一个 SPSC 队列 / Destroy one SPSC queue
   */
  ~SPSCQueue() = default;

  /**
   * @brief 向队列尾部推入一个 payload / Push one payload into the queue tail
   * @param item 待入队 payload / Payload to enqueue
   * @return 成功返回 `ErrorCode::OK`；队列满返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         the queue is full
   */
  ErrorCode Push(const Data& item)
  {
    return core_.PushBytes(&item);
  }

  /**
   * @brief 从队列头部弹出一个 payload / Pop one payload from the queue head
   * @param item 用于接收 payload / Receives the dequeued payload
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
  ErrorCode Pop(Data& item)
  {
    return core_.PopBytes(&item);
  }

  /**
   * @brief 丢弃一个队头 payload / Discard one front payload
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
  ErrorCode Pop()
  {
    return core_.PopBytes(nullptr);
  }

  /**
   * @brief 查看一个队头 payload 但不出队 / Peek one front payload without dequeuing it
   * @param item 用于接收 payload / Receives the peeked payload
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue is empty
   */
  ErrorCode Peek(Data& item)
  {
    return core_.PeekBytes(&item);
  }

  /**
   * @brief 批量推入多个 payload / Push multiple payloads into the queue
   * @param data payload 数组指针 / Pointer to the payload array
   * @param size payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；队列满返回 `ErrorCode::FULL`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when
   *         the queue is full
   */
  ErrorCode PushBatch(const Data* data, size_t size)
  {
    return core_.PushBatchBytes(data, size);
  }

  /**
   * @brief 通过写入器回调推入一个 payload / Push one payload via a writer callback
   * @tparam Writer 写入器类型 / Writer callback type
   * @param writer 写入器回调，签名为 `ErrorCode(Data* buffer, size_t count)`
   *        / Writer callback with signature `ErrorCode(Data* buffer, size_t count)`
   * @return 成功返回 `ErrorCode::OK`；队列满返回 `ErrorCode::FULL`；否则返回写入器错误码
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::FULL` when the
   *         queue is full; otherwise returns the writer error code
   *
   * @note 回调中的 `count` 固定为 1，成功写入后才提交到队列。
   *       The callback always receives `count == 1`; the payload is committed
   *       only after the writer succeeds.
   */
  template <typename Writer>
  ErrorCode PushWithWriter(Writer&& writer)
  {
    static_assert(std::is_default_constructible_v<Data>,
                  "SPSCQueue::PushWithWriter requires default-constructible payloads");
    static_assert(std::is_destructible_v<Data>,
                  "SPSCQueue::PushWithWriter requires destructible payloads");
    static_assert(std::is_invocable_v<Writer&, Data*, size_t>,
                  "PushWithWriter writer must be callable as "
                  "ErrorCode(Data* buffer, size_t count)");
    using WriterRet = std::invoke_result_t<Writer&, Data*, size_t>;
    static_assert(std::is_convertible_v<WriterRet, ErrorCode>,
                  "PushWithWriter writer return type must be convertible to ErrorCode");

    if (core_.EmptySize() == 0)
    {
      return ErrorCode::FULL;
    }

    Data tmp{};
    Writer& writer_ref = writer;
    const ErrorCode ec = writer_ref(&tmp, 1);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
    return core_.PushBytes(&tmp);
  }

  /**
   * @brief 通过读取器回调弹出一个 payload / Pop one payload via a reader callback
   * @tparam Reader 读取器类型 / Reader callback type
   * @param reader 读取器回调，签名为 `ErrorCode(const Data* buffer, size_t count)`
   *        / Reader callback with signature `ErrorCode(const Data* buffer, size_t count)`
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`；否则返回读取器错误码
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when the
   *         queue is empty; otherwise returns the reader error code
   *
   * @note 回调中的 `count` 固定为 1，读取器成功后才提交出队。
   *       The callback always receives `count == 1`; the pop is committed only
   *       after the reader succeeds.
   */
  template <typename Reader>
  ErrorCode PopWithReader(Reader&& reader)
  {
    static_assert(std::is_default_constructible_v<Data>,
                  "SPSCQueue::PopWithReader requires default-constructible payloads");
    static_assert(std::is_destructible_v<Data>,
                  "SPSCQueue::PopWithReader requires destructible payloads");
    static_assert(std::is_invocable_v<Reader&, const Data*, size_t>,
                  "PopWithReader reader must be callable as "
                  "ErrorCode(const Data* buffer, size_t count)");
    using ReaderRet = std::invoke_result_t<Reader&, const Data*, size_t>;
    static_assert(std::is_convertible_v<ReaderRet, ErrorCode>,
                  "PopWithReader reader return type must be convertible to ErrorCode");

    Data tmp{};
    auto ec = core_.PeekBytes(&tmp);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }

    Reader& reader_ref = reader;
    ec = reader_ref(&tmp, 1);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
    return core_.PopBytes(nullptr);
  }

  /**
   * @brief 批量弹出多个 payload / Pop multiple payloads from the queue
   * @param data 用于接收 payload 的数组 / Array receiving dequeued payloads
   * @param size payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue does not contain enough payloads
   */
  ErrorCode PopBatch(Data* data, size_t size)
  {
    return core_.PopBatchBytes(data, size);
  }

  /**
   * @brief 批量查看多个 payload 但不出队 / Peek multiple payloads without dequeuing them
   * @param data 用于接收 payload 的数组 / Array receiving peeked payloads
   * @param size payload 个数 / Number of payloads
   * @return 成功返回 `ErrorCode::OK`；队列空返回 `ErrorCode::EMPTY`
   *         Returns `ErrorCode::OK` on success; returns `ErrorCode::EMPTY` when
   *         the queue does not contain enough payloads
   */
  ErrorCode PeekBatch(Data* data, size_t size)
  {
    return core_.PeekBatchBytes(data, size);
  }

  /**
   * @brief 重置队列状态 / Reset the queue state
   */
  void Reset() { core_.Reset(); }

  /**
   * @brief 获取当前已用元素数 / Get the current element count
   * @return 当前元素个数 / Current number of stored payloads
   */
  size_t Size() const { return core_.Size(); }

  /**
   * @brief 获取剩余空槽数 / Get the current free-slot count
   * @return 当前空槽个数 / Current number of free slots
   */
  size_t EmptySize() const { return core_.EmptySize(); }

  /**
   * @brief 获取队列最大容量 / Get the maximum queue capacity
   * @return 队列容量 / Queue capacity
   */
  size_t MaxSize() const { return core_.MaxSize(); }

 private:
  SPSCQueueCore core_;  ///< 共享单生产者字节内核 / Shared single-producer byte core.
};
}  // namespace LibXR
