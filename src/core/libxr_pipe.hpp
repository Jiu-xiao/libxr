#pragma once
/**
 * @file
 * @brief Pipe：基于共享字节队列将 WritePort → ReadPort 连接起来的单向管道。
 * @brief Pipe: single-direction pipe that bridges WritePort → ReadPort over a shared byte
 * queue.
 *
 * 本类让 WritePort 与 ReadPort 共享同一条无锁字节队列，使写端写入的数据可被读端直接读取，
 * 无需端口间中间拷贝。 This class wires a WritePort and a ReadPort to the same underlying
 * lock-free byte queue so that data written by the writer becomes readable by the reader
 * without intermediate copies.
 */

#include "libxr_def.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{
/**
 * @class Pipe
 * @brief 基于共享队列，由 ReadPort + WritePort 组成的单向管道。
 * @brief Single-direction pipe built from ReadPort + WritePort on a shared queue.
 *
 */
class Pipe
{
 public:
  /**
   * @brief 使用指定数据队列容量构造 Pipe。
   * @brief Construct a Pipe with the given shared data-queue capacity.
   *
   * @param buffer_size 共享数据队列的容量（字节）。 Capacity (in bytes) of the shared
   * data queue.
   */
  Pipe(size_t buffer_size) : read_port_(0), write_port_(1, buffer_size)
  {
    // 绑定回调并共享同一数据队列。
    // Bind callbacks and share the same data queue.
    read_port_.read_fun_ = ReadFun;
    write_port_.write_fun_ = WriteFun;
    read_port_.queue_data_ = write_port_.queue_data_;
  }

  /**
   * @brief 析构函数。
   * @brief Destructor.
   */
  ~Pipe() {}

  /**
   * @brief 禁止拷贝以避免重复绑定状态。
   * @brief Non-copyable to avoid double-binding internal state.
   */
  Pipe(const Pipe&) = delete;

  /**
   * @brief 禁止拷贝赋值以避免重复绑定状态。
   * @brief Non-copy-assignable to avoid double-binding internal state.
   */
  Pipe& operator=(const Pipe&) = delete;

  /**
   * @brief 获取读取端口。
   * @brief Get the read endpoint.
   * @return 返回内部 ReadPort 的引用。 Reference to the internal ReadPort.
   */
  ReadPort& GetReadPort() { return read_port_; }

  /**
   * @brief 获取写入端口。
   * @brief Get the write endpoint.
   * @return 返回内部 WritePort 的引用。 Reference to the internal WritePort.
   */
  WritePort& GetWritePort() { return write_port_; }

 private:
  /**
   * @brief 读端回调（占位，无具体操作）。
   * @brief Read-side callback (no-op placeholder).
   *
   * 仅用于匹配 `ReadPort` 回调签名；实际读取推进通常在 `ProcessPendingReads()` 中进行。
   * Provided to match the `ReadPort` callback signature; reading is typically advanced
   * in `ProcessPendingReads()`.
   *
   * @param port ReadPort 引用（未使用）。 ReadPort reference (unused).
   * @param in_isr 是否在中断上下文中运行。 Whether running in ISR context.
   * @return 返回 `ErrorCode::PENDING`。 Returns `ErrorCode::PENDING`.
   */
  static ErrorCode ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

  /**
   * @brief 写端回调：弹出一次写操作并推动读侧处理。
   * @brief Write-side callback: pop a write op and advance the reader.
   *
   * 从写端操作队列中弹出一个 `WriteInfoBlock`，并调用 `ReadPort::ProcessPendingReads()`，
   * 使挂起的读请求可从共享数据队列中取出字节。
   * Pops a `WriteInfoBlock` from the write op-queue and calls
   * `ReadPort::ProcessPendingReads()` so pending reads can pull bytes from the shared
   * data queue.
   *
   * @param port 触发本回调的 WritePort。 The WritePort invoking this callback.
   * @param in_isr 是否在中断上下文中运行。 Whether running in ISR context.
   * @return 若已推进返回 `ErrorCode::OK`；若无可处理操作返回 `ErrorCode::EMPTY`。
   *         Returns `ErrorCode::OK` if progressed; `ErrorCode::EMPTY` if no op was
   * available.
   */
  static ErrorCode WriteFun(WritePort& port, bool in_isr)
  {
    Pipe* pipe = CONTAINER_OF(&port, Pipe, write_port_);
    WriteInfoBlock info;
    if (port.queue_info_->Pop(info) != ErrorCode::OK)
    {
      ASSERT(false);
      return ErrorCode::EMPTY;
    }

    // 推动读端从共享队列中取数。
    // Drive the reader to consume from the shared queue.
    pipe->read_port_.ProcessPendingReads(in_isr);

    return ErrorCode::OK;
  }

  ReadPort read_port_;    ///< 共享写端数据队列的读端。 Read endpoint sharing the writer's
                          ///< data queue.
  WritePort write_port_;  ///< 持有共享数据队列（容量为构造参数）的写端。 Write endpoint
                          ///< owning the shared queue.
};
}  // namespace LibXR
