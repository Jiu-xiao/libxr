#pragma once
/**
 * @file
 * @brief Pipe：将 WritePort 已提交的字节搬运到 ReadPort 的单向管道。
 * @brief Pipe: single-direction pipe moving committed WritePort bytes into ReadPort.
 *
 * 两端各自持有字节队列。只有已经发布请求 metadata 的 payload 才会在 Pipe owner turn
 * 中搬入读队列；Stream 尚未 Commit 的字节不会对 ReadPort 可见。 / Each endpoint owns
 * its byte queue. A Pipe owner turn moves only payload whose request metadata is already
 * published, so bytes from an uncommitted Stream are not visible to ReadPort.
 */

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "serialized_service.hpp"

namespace LibXR
{
/**
 * @class Pipe
 * @brief 由 ReadPort + WritePort 组成的单向管道。
 * @brief Single-direction pipe built from ReadPort + WritePort.
 *
 */
class Pipe
{
 private:
  class PipeReadPort : public ReadPort
  {
   public:
    PipeReadPort(Pipe& pipe, size_t buffer_size) : ReadPort(buffer_size), pipe_(pipe) {}

   protected:
    void OnRxDequeue(bool in_isr) override { pipe_.OnReadDataDequeue(in_isr); }

   private:
    Pipe& pipe_;
  };

 public:
  /**
   * @brief 使用指定的单端队列容量构造 Pipe。
   * @brief Construct a Pipe with the given per-endpoint queue capacity.
   *
   * @param buffer_size 每个端口的字节队列容量。 Byte queue capacity of each endpoint.
   */
  Pipe(size_t buffer_size) : write_port_(1, buffer_size), read_port_(*this, buffer_size)
  {
    write_port_.write_fun_ = WriteFun;
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
  void OnReadDataDequeue(bool in_isr) { Invoke(in_isr); }

  void Invoke(bool in_isr)
  {
    execution_policy_.Invoke(
        1U,
        [this, in_isr](uint32_t) noexcept
        {
          // Keep the WriteQueue alive until the ReadQueue has been published and
          // destroyed. Write completion callbacks must never run while a producer scope
          // for this ReadPort is still active.
          auto write_queue = write_port_.GetWriteQueue(in_isr);
          auto read_queue = read_port_.GetReadQueue(in_isr);
          Service(in_isr, write_queue, read_queue);
          read_queue.Publish();
        });
  }

  void Service(bool in_isr, WritePort::WriteQueue& write_queue,
               ReadPort::ReadQueue& read_queue)
  {
    const size_t expected = write_queue.front_size;
    if (expected == 0U || read_queue.EmptySize() < expected)
    {
      return;
    }

    const size_t moved = write_queue.PopWithWriter(
        expected,
        [&read_queue, in_isr](const uint8_t* first, size_t first_size,
                              const uint8_t* second, size_t second_size) -> size_t
        {
          REQUIRE_FROM_CALLBACK(read_queue.PushBatch(first, first_size) == ErrorCode::OK,
                                in_isr);
          if (second_size != 0U)
          {
            REQUIRE_FROM_CALLBACK(
                read_queue.PushBatch(second, second_size) == ErrorCode::OK, in_isr);
          }
          return first_size + second_size;
        });
    REQUIRE_FROM_CALLBACK(moved == expected, in_isr);
  }

  /**
   * @brief 写端 doorbell：串行接受已发布 metadata 并推动读侧 / Write-side
   * doorbell: serialize published-metadata acceptance and reader progress
   *
   * 只有已发布 metadata 的 payload 会被搬入读队列；WriteQueue 完成写 Operation，
   * ReadQueue 在 Pipe owner 释放后推动挂起读取。 / Only payload with published metadata
   * is moved into the read queue. WriteQueue completes write Operations, while ReadQueue
   * advances pending reads after the Pipe owner is released.
   *
   * @param port 触发本回调的 WritePort。 The WritePort invoking this callback.
   * @param in_isr 是否在中断上下文中运行。 Whether running in ISR context.
   */
  static void WriteFun(WritePort& port, bool in_isr)
  {
    auto* pipe = LibXR::ContainerOf(&port, &Pipe::write_port_);
    pipe->Invoke(in_isr);
  }

  WritePort write_port_;    ///< Pipe 写端 / Pipe write endpoint.
  PipeReadPort read_port_;  ///< Pipe 读端 / Pipe read endpoint.
  SerializedService
      execution_policy_;  ///< Pipe-local progress owner. / Pipe 本地推进 owner。
};
}  // namespace LibXR
