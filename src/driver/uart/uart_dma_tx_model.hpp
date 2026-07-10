#pragma once

#include <cstddef>
#include <cstdint>

#include "double_buffer.hpp"
#include "flag.hpp"
#include "libxr_assert.hpp"
#include "libxr_rw.hpp"

namespace LibXR
{

/**
 * @brief UART 单次 DMA 发送执行模型 / UART one-shot DMA TX execution model
 *
 * 模型管理 active/pending 请求和双缓冲状态。平台后端只需实现
 * `StartDmaTx(data, size, block)`，并通过 `OnTransferDone()` 上报 DMA 完成或错误。
 * The model owns the active/pending requests and double-buffer state. The platform
 * backend only implements `StartDmaTx(data, size, block)` and reports DMA completion
 * or errors through `OnTransferDone()`.
 *
 * @tparam Backend 静态绑定的平台后端类型 / Statically bound platform backend type
 * @tparam StateFlag 与平台并发模型匹配的标志类型 / Flag type selected for the platform
 * concurrency model
 */
template <typename Backend, typename StateFlag = Flag::Plain>
class UartDmaTxModel
{
 public:
  /**
   * @brief 绑定平台后端、写端口和 DMA 双缓冲区 / Bind the backend, write port, and
   * DMA double buffer
   * @param backend 提供 DMA 启动操作的平台后端 / Platform backend providing DMA start
   * @param port 提供请求队列和完成通知的写端口 / Write port providing request queues and
   * completion notification
   * @param storage 划分为两个等长块的 DMA 存储区 / DMA storage split into two equal
   * blocks
   */
  UartDmaTxModel(Backend& backend, WritePort& port, RawData storage)
      : backend_(backend), port_(port), buffers_(storage)
  {
  }

  /**
   * @brief 提交或预装 `WritePort` 队首请求 / Submit or stage the request at the head of
   * `WritePort`
   *
   * 空闲时，请求在 DMA 成功启动后同步返回 `OK`。DMA 忙时，请求预装到 pending 缓冲区并
   * 返回 `PENDING`，随后由 `OnTransferDone()` 提升、启动并完成通知。
   * When idle, the request returns `OK` after DMA starts successfully. While DMA is
   * active, the request is staged in the pending buffer and returns `PENDING`; it is
   * promoted, started, and completed by `OnTransferDone()`.
   *
   * @return `OK` 表示 DMA 已启动，`PENDING` 表示请求仍在排队，其他值表示启动失败 /
   * `OK` if DMA started, `PENDING` if the request remains queued, or another code on
   * start failure
   */
  ErrorCode Submit()
  {
    if (in_completion_.IsSet() || pending_valid_.IsSet())
    {
      return ErrorCode::PENDING;
    }

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return ErrorCode::PENDING;
    }

    if (info.data.size_ > buffers_.Size())
    {
      ASSERT(false);
      return ErrorCode::FAILED;
    }

    if (busy_.IsSet())
    {
      if (!StagePending(info))
      {
        return ErrorCode::FAILED;
      }

      // DMA may have completed before the pending state was published.
      if (busy_.IsSet())
      {
        return ErrorCode::PENDING;
      }

      if (!PromotePending())
      {
        return ErrorCode::PENDING;
      }

      WriteInfoBlock claimed_info{};
      if (!PopActiveInfo(claimed_info))
      {
        ClearActive();
        return ErrorCode::FAILED;
      }

      return StartActive() ? ErrorCode::OK : FailSynchronousStart();
    }

    WriteInfoBlock claimed_info{};
    if (!PopPayload(buffers_.ActiveBuffer(), info.data.size_) ||
        port_.queue_info_->Pop(claimed_info) != ErrorCode::OK)
    {
      ASSERT(false);
      return ErrorCode::FAILED;
    }

    active_length_ = info.data.size_;
    return StartActive() ? ErrorCode::OK : FailSynchronousStart();
  }

  /**
   * @brief 处理一次 DMA 完成或错误事件 / Handle one DMA completion or error event
   * @param in_isr 事件是否来自中断上下文 / Whether the event is handled in interrupt
   * context
   * @param result DMA 传输结果 / DMA transfer result
   *
   * 重复事件或没有 active 请求时不产生完成通知。错误只终止已预装的 pending 请求；active
   * 请求已经在 DMA 启动时完成上层提交。
   * Duplicate events and events without an active request do not emit completion. An
   * error fails only the staged pending request because the active request completed at
   * the upper layer when DMA was started.
   */
  void OnTransferDone(bool in_isr, ErrorCode result)
  {
    if (in_completion_.TestAndSet())
    {
      return;
    }

    if (!busy_.TestAndClear())
    {
      in_completion_.Clear();
      return;
    }

    ClearActive();

    if (result != ErrorCode::OK)
    {
      FailPending(in_isr, result);
      in_completion_.Clear();
      return;
    }

    WriteInfoBlock completed_info{};
    if (!LoadNextActive(completed_info))
    {
      in_completion_.Clear();
      return;
    }

    const bool started = StartActive();
    if (!started)
    {
      ClearActive();
    }
    port_.Finish(in_isr, started ? ErrorCode::OK : ErrorCode::FAILED, completed_info);

    if (started)
    {
      (void)StageNextPending();
    }

    in_completion_.Clear();
  }

  /**
   * @brief 平台重新配置后重启 active DMA / Restart active DMA after reconfiguration
   * @return active 请求存在且后端接受重启时返回 true / True when an active request exists
   * and the backend accepts the restart
   */
  bool RestartActive()
  {
    if (!busy_.IsSet() || (active_length_ == 0U))
    {
      return false;
    }
    return backend_.StartDmaTx(buffers_.ActiveBuffer(), active_length_,
                               buffers_.ActiveBlock());
  }

  /**
   * @brief 查询 DMA 是否持有 active 请求 / Check whether DMA owns an active request
   * @return DMA 正在处理 active 请求时返回 true / True while DMA owns an active request
   */
  [[nodiscard]] bool IsBusy() const { return busy_.IsSet(); }

  /**
   * @brief 获取一个固定 DMA 缓冲块 / Get one fixed DMA buffer block
   * @param block 缓冲块索引，只能为 0 或 1 / Buffer block index, either 0 or 1
   * @return 指定缓冲块的起始地址 / Start address of the selected buffer block
   */
  [[nodiscard]] uint8_t* Buffer(int block) const { return buffers_.Buffer(block); }

  /**
   * @brief 获取单个 DMA 缓冲块容量 / Get the capacity of one DMA buffer block
   * @return 单个缓冲块的字节数 / Capacity of one buffer block in bytes
   */
  [[nodiscard]] size_t BufferSize() const { return buffers_.Size(); }

 private:
  bool PopPayload(uint8_t* destination, size_t size)
  {
    const ErrorCode result = port_.queue_data_->PopBatch(destination, size);
    ASSERT(result == ErrorCode::OK);
    return result == ErrorCode::OK;
  }

  bool StagePending(const WriteInfoBlock& info)
  {
    if (pending_valid_.IsSet() || !PopPayload(buffers_.PendingBuffer(), info.data.size_))
    {
      return false;
    }

    pending_length_ = info.data.size_;
    pending_valid_.Set();
    return true;
  }

  bool StageNextPending()
  {
    if (pending_valid_.IsSet())
    {
      return false;
    }

    WriteInfoBlock info{};
    if (port_.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return false;
    }

    if (info.data.size_ > buffers_.Size())
    {
      ASSERT(false);
      return false;
    }
    return StagePending(info);
  }

  bool PromotePending()
  {
    if (!pending_valid_.TestAndClear())
    {
      return false;
    }

    buffers_.FlipActiveBlock();
    active_length_ = pending_length_;
    pending_length_ = 0U;
    return true;
  }

  bool PopActiveInfo(WriteInfoBlock& info)
  {
    const ErrorCode result = port_.queue_info_->Pop(info);
    ASSERT(result == ErrorCode::OK);
    return result == ErrorCode::OK;
  }

  bool LoadNextActive(WriteInfoBlock& info)
  {
    if (PromotePending())
    {
      return PopActiveInfo(info);
    }

    WriteInfoBlock next_info{};
    if (port_.queue_info_->Peek(next_info) != ErrorCode::OK)
    {
      return false;
    }
    if (next_info.data.size_ > buffers_.Size() ||
        !PopPayload(buffers_.ActiveBuffer(), next_info.data.size_) ||
        !PopActiveInfo(info))
    {
      ASSERT(false);
      return false;
    }

    active_length_ = next_info.data.size_;
    return true;
  }

  bool StartActive()
  {
    ASSERT(active_length_ > 0U);
    busy_.Set();
    if (backend_.StartDmaTx(buffers_.ActiveBuffer(), active_length_,
                            buffers_.ActiveBlock()))
    {
      return true;
    }

    busy_.Clear();
    return false;
  }

  ErrorCode FailSynchronousStart()
  {
    ClearActive();
    return ErrorCode::FAILED;
  }

  void FailPending(bool in_isr, ErrorCode result)
  {
    if (!pending_valid_.TestAndClear())
    {
      return;
    }

    pending_length_ = 0U;
    WriteInfoBlock pending_info{};
    if (port_.queue_info_->Pop(pending_info) != ErrorCode::OK)
    {
      ASSERT(false);
      return;
    }
    port_.Finish(in_isr, result, pending_info);
  }

  void ClearActive() { active_length_ = 0U; }

  Backend& backend_;
  WritePort& port_;
  DoubleBuffer buffers_;
  size_t active_length_ = 0U;
  size_t pending_length_ = 0U;
  StateFlag busy_{};
  StateFlag pending_valid_{};
  StateFlag in_completion_{};
};

}  // namespace LibXR
