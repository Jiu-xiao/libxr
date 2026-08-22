#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief 将单一 direct RX producer 和短 TX admission 与 CONFIG/recovery control 排他 /
 * Serialize direct RX and short TX admissions against CONFIG and recovery
 *
 * RX 在读取 DMA 位置或 descriptor、以及搬运字节前取得 gate。由于 CONFIG 明确允许丢弃
 * 过渡窗口 RX 数据，IRQ adapter 可以先读并确认中断状态。CONFIG reservation 和 runtime
 * recovery 都会关闭后续 RX admission；已进入的 RX 片段可以完成，其释放会通知等待中的
 * control 重试。 / RX claims the gate before reading DMA position or descriptors and
 * before moving bytes. An IRQ adapter may acknowledge status first because transition
 * RX data may be discarded. CONFIG reservation and recovery close later RX admission;
 * an admitted fragment may finish and wakes waiting control on release.
 *
 * CONFIG reservation 与 payload publication 是两个独立转换，避免 RX release 在 payload
 * 写完前唤醒 CONFIG consumer。 / CONFIG reservation and payload publication are
 * separate transitions so RX release cannot wake CONFIG before its payload is complete.
 *
 * TX 只在公共队列记录复制到 pending 或 pending 提升到硬件期间取得 gate。CONFIG 已
 * reserved/pending 时，TX 不触碰队列并失败；若 TX 先取得 admission，该记录线性化在
 * CONFIG 前，CONFIG 等短 TX admission 离开后按 active/pending 规则处理。 / TX claims
 * only while staging or promoting one record. If CONFIG already won, TX fails without
 * touching queues. If TX wins first, that record is linearized before CONFIG.
 *
 * 同一时刻最多有一个 reserved/pending CONFIG；pending 位在其整个生命周期保持置位，
 * 后续请求在完成前均失败。通用 CONTROL_ACTIVE 位跨异步 stop 表示 CONFIG 或 recovery。
 * / At most one CONFIG may be reserved or pending. Its pending bit remains set until
 * completion, and one CONTROL_ACTIVE bit covers CONFIG or recovery across async stop.
 */
class UartRxConfigGate
{
 public:
  /**
   * @brief 保留唯一 CONFIG 槽并关闭 RX admission / Reserve the only CONFIG slot and
   * close RX admission
   * @return 保留成功时为 true；已有 CONFIG 未完成时为 false / True when reserved;
   * false while another CONFIG is outstanding
   */
  [[nodiscard]] bool TryReserveConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while ((observed & CONFIG_MASK) == 0U)
    {
      const uint32_t desired = observed | CONFIG_RESERVED;
      if (state_.compare_exchange_strong(observed, desired, std::memory_order_acquire,
                                         std::memory_order_relaxed))
      {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 在通知 serialized service 前发布完整 CONFIG payload / Publish the complete
   * CONFIG payload before notifying the serialized service
   * @pre 当前调用者已成功执行 `TryReserveConfig()` 并写完 payload / The caller has
   * reserved CONFIG and completed its payload
   */
  void PublishConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONFIG_RESERVED) != 0U);
      ASSERT((observed & CONFIG_PENDING) == 0U);
      const uint32_t desired = (observed & ~CONFIG_RESERVED) | CONFIG_PENDING;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  /**
   * @brief 尝试取得 direct RX 硬件片段；失败时本次数据应丢弃 / Claim the direct RX
   * hardware fragment or reject it for dropping
   * @return 取得 RX admission 时为 true / True when RX admission was acquired
   */
  [[nodiscard]] bool TryEnterRx()
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while ((observed & (RX_ACTIVE | CONFIG_MASK | CONTROL_PENDING | CONTROL_ACTIVE)) ==
           0U)
    {
      const uint32_t desired = observed | RX_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 在触碰 TX 队列状态前取得短 staging/start admission / Claim a short TX
   * staging/start admission before touching TX queue state
   *
   * false 表示 CONFIG 已赢得边界；调用者在本次 owner 快照中不得 pop payload、提升
   * pending 或启动硬件。 / False means CONFIG already won; this owner snapshot must
   * not pop payload, promote pending state, or start hardware.
   *
   * @return 取得短 TX admission 时为 true / True when TX admission was acquired
   */
  [[nodiscard]] bool TryEnterTx()
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while ((observed & CONFIG_MASK) == 0U)
    {
      ASSERT((observed & TX_ACTIVE) == 0U);
      const uint32_t desired = observed | TX_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 释放短 TX staging/start admission / Release a short TX staging/start
   * admission
   * @pre 当前调用者持有 TX admission / The caller owns TX admission
   */
  void LeaveTx()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & TX_ACTIVE) != 0U);
      const uint32_t desired = observed & ~TX_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  /**
   * @brief 释放 RX，并报告是否必须发布 CONTROL_READY / Release RX and report whether
   * CONTROL_READY must be published
   * @return CONFIG 或 recovery 正在等待时为 true / True when CONFIG or recovery is
   * waiting
   */
  [[nodiscard]] bool LeaveRx()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & RX_ACTIVE) != 0U);
      const uint32_t desired = observed & ~RX_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return (observed & (CONFIG_PENDING | CONTROL_PENDING)) != 0U;
      }
    }
  }

  /**
   * @brief 进入或恢复唯一 serialized CONFIG transaction / Enter or resume the
   * serialized CONFIG transaction
   * @return CONFIG 可以推进时为 true；RX/TX admission 尚未退出时为 false / True when
   * CONFIG may advance; false while RX or TX admission is active
   */
  [[nodiscard]] bool TryEnterConfig()
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while ((observed & CONFIG_PENDING) != 0U)
    {
      if ((observed & (RX_ACTIVE | TX_ACTIVE | CONFIG_RESERVED)) != 0U)
      {
        return false;
      }

      const uint32_t desired = (observed | CONTROL_ACTIVE) & ~CONTROL_PENDING;
      if (desired == observed)
      {
        return true;
      }
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 完成唯一 CONFIG transaction 并重新开放 admission / Complete CONFIG and
   * reopen admission
   * @pre CONFIG 持有 CONTROL_ACTIVE，且 RX/TX admission 已退出 / CONFIG owns
   * CONTROL_ACTIVE and RX/TX admissions are inactive
   */
  void LeaveConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & (CONFIG_PENDING | CONTROL_ACTIVE)) ==
             (CONFIG_PENDING | CONTROL_ACTIVE));
      ASSERT((observed & (RX_ACTIVE | TX_ACTIVE | CONFIG_RESERVED | CONTROL_PENDING)) ==
             0U);
      const uint32_t desired = observed & ~(CONFIG_PENDING | CONTROL_ACTIVE);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  /**
   * @brief 进入或恢复 runtime recovery；RX active 时留下持久等待 / Enter or resume
   * runtime recovery, leaving a durable wait behind active RX
   * @return recovery 取得 CONTROL_ACTIVE 时为 true；等待 RX 或 CONFIG 优先时为 false /
   * True when recovery owns CONTROL_ACTIVE; false while waiting for RX or CONFIG
   */
  [[nodiscard]] bool TryEnterRecovery()
  {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while ((observed & CONFIG_MASK) == 0U)
    {
      if ((observed & CONTROL_ACTIVE) != 0U)
      {
        return true;
      }
      if (((observed & RX_ACTIVE) != 0U) && ((observed & CONTROL_PENDING) != 0U))
      {
        return false;
      }

      const uint32_t desired = ((observed & RX_ACTIVE) != 0U)
                                   ? (observed | CONTROL_PENDING)
                                   : ((observed | CONTROL_ACTIVE) & ~CONTROL_PENDING);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        return (desired & CONTROL_ACTIVE) != 0U;
      }
    }
    return false;
  }

  /**
   * @brief 完成 runtime recovery，同时保留并发 reserved CONFIG / Complete recovery
   * while preserving a concurrently reserved CONFIG
   * @pre recovery 持有 CONTROL_ACTIVE，且 RX 已退出 / Recovery owns CONTROL_ACTIVE
   * and RX is inactive
   */
  void LeaveRecovery()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONTROL_ACTIVE) != 0U);
      ASSERT((observed & RX_ACTIVE) == 0U);
      const uint32_t desired = observed & ~(CONTROL_PENDING | CONTROL_ACTIVE);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  /**
   * @brief 查询 CONFIG 是否已 reserved 或 pending / Report whether CONFIG is reserved
   * or pending
   * @return 存在未完成 CONFIG 请求时为 true / True while a CONFIG request is outstanding
   */
  [[nodiscard]] bool ConfigRequested() const
  {
    return (state_.load(std::memory_order_acquire) & CONFIG_MASK) != 0U;
  }

  /** @brief 构造一个开放 admission 的 gate / Construct a gate with open admission. */
  UartRxConfigGate() = default;
  UartRxConfigGate(const UartRxConfigGate&) = delete;
  UartRxConfigGate& operator=(const UartRxConfigGate&) = delete;

 private:
  static constexpr uint32_t RX_ACTIVE = 1U << 0U;
  static constexpr uint32_t CONFIG_RESERVED = 1U << 1U;
  static constexpr uint32_t CONFIG_PENDING = 1U << 2U;
  static constexpr uint32_t CONTROL_PENDING = 1U << 3U;
  static constexpr uint32_t CONTROL_ACTIVE = 1U << 4U;
  static constexpr uint32_t TX_ACTIVE = 1U << 5U;
  static constexpr uint32_t CONFIG_MASK = CONFIG_RESERVED | CONFIG_PENDING;

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
