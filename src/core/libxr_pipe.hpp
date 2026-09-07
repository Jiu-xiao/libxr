#pragma once

/**
 * @file
 * @brief 共享字节队列的单向管道 / Unidirectional pipe with a shared byte queue.
 */

#include "libxr_def.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief 由读写端口组成的单向管道
 *        / Unidirectional pipe connecting read and write ports.
 *
 * 写端分配一条 SPSC 字节队列，读端借用该队列，两端之间无需再次复制数据。
 * 写操作在数据入队并通知读端后完成，无需等待读端消费全部数据。
 * The writer allocates one SPSC byte queue shared with the reader, without another
 * copy between endpoints. Writes complete after admission and reader notification,
 * without waiting for all bytes to be read.
 *
 * @note Stream 追加的字节在 Commit 前即可被读端读取，Commit 负责通知挂起读。
 *       Stream appends are readable before Commit, which notifies pending reads.
 * @pre 两端遵守 ReadPort 和 WritePort 的调用约定。读端只负责消费共享队列。
 *      Both endpoints follow their port contracts; the reader only consumes the queue.
 */
class Pipe
{
 public:
  /**
   * @brief 构造管道并绑定共享队列 / Construct a pipe and bind its shared queue.
   * @param buffer_size 共享队列容量，单位为字节，必须大于零。
   *        Shared queue capacity in bytes, which must be positive.
   * @note 仅分配字节队列 / Allocates only the byte queue.
   */
  explicit Pipe(size_t buffer_size) : read_port_(0), write_port_(0, buffer_size)
  {
    REQUIRE(write_port_.queue_data_ != nullptr);
    read_port_.BindQueue(write_port_.queue_data_);
    write_port_ = &WriteFun;
  }

  /**
   * @brief 析构管道 / Destroy the pipe.
   * @pre 相关请求、调用、回调及持有写入权的 Stream 已结束。
   *      Related requests, calls, callbacks, and owning Streams have ended.
   * @note 不取消请求，也不释放队列存储 / Does not cancel requests or free queue storage.
   */
  ~Pipe() = default;

  /// 端口状态与共享队列不可复制 / Endpoint state and the shared queue cannot be copied.
  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;

  /**
   * @brief 获取读端口 / Get the read endpoint.
   * @return 管道内部读端口的引用 / Reference to the pipe's read port.
   */
  ReadPort& GetReadPort() { return read_port_; }

  /**
   * @brief 获取写端口 / Get the write endpoint.
   * @return 管道内部写端口的引用 / Reference to the pipe's write port.
   */
  WritePort& GetWritePort() { return write_port_; }

 private:
  /**
   * @brief 通知读端检查共享队列 / Notify the reader to check the shared queue.
   * @param port 本管道的写端口 / This pipe's write port.
   * @param in_isr 当前是否在中断中 / Whether the current call is in an ISR.
   * @note 此时写端仍持有写入权；可能同步完成挂起读并执行读回调。
   *       Holds producer access; may complete a read and run its callback inline.
   */
  static void WriteFun(WritePort& port, bool in_isr)
  {
    auto* pipe = LibXR::ContainerOf(&port, &Pipe::write_port_);
    pipe->read_port_.NotifyDataAvailable(in_isr);
  }

  ReadPort read_port_;    ///< 共享队列的消费者 / Shared queue consumer.
  WritePort write_port_;  ///< 分配队列的生产者 / Producer allocating the queue.
};

}  // namespace LibXR
