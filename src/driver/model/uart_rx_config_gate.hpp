#pragma once

#include <atomic>
#include <cstdint>

#include "libxr_assert.hpp"

namespace LibXR
{

/**
 * @brief Serialize UART RX hardware callbacks against configuration / 串行化 UART RX
 * 硬件回调与配置事务
 *
 * RX remains a single-producer data path. This gate only protects hardware state shared
 * with configuration, such as DMA descriptors, positions, and stop/start operations.
 * All transitions use one 32-bit atomic word so an RX exit cannot lose a concurrent
 * configuration request.
 * RX 仍是单生产者数据路径。本门仅保护与配置共享的 DMA 描述符、位置和启停等硬件状态。
 * 所有转换共用一个 32 位原子字，避免 RX 退出时丢失并发配置请求。
 */
class UartRxConfigGate
{
 public:
  void RequestConfig() { state_.fetch_or(CONFIG_PENDING, std::memory_order_release); }

  [[nodiscard]] bool TryEnterRx()
  {
    uint32_t expected = 0U;
    return state_.compare_exchange_strong(expected, RX_ACTIVE, std::memory_order_acquire,
                                          std::memory_order_relaxed);
  }

  /**
   * @return true when the caller must republish CONFIG after releasing RX ownership.
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
        return (observed & CONFIG_PENDING) != 0U;
      }
    }
  }

  [[nodiscard]] bool TryEnterConfig()
  {
    uint32_t expected = CONFIG_PENDING;
    return state_.compare_exchange_strong(
        expected, CONFIG_ACTIVE, std::memory_order_acquire, std::memory_order_relaxed);
  }

  void ConsumePendingConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONFIG_ACTIVE) != 0U);
      const uint32_t desired = observed & ~CONFIG_PENDING;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  void LeaveConfig()
  {
    uint32_t observed = state_.load(std::memory_order_relaxed);
    while (true)
    {
      ASSERT((observed & CONFIG_ACTIVE) != 0U);
      const uint32_t desired = observed & ~CONFIG_ACTIVE;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                       std::memory_order_relaxed))
      {
        return;
      }
    }
  }

  [[nodiscard]] bool ConfigRequested() const
  {
    return (state_.load(std::memory_order_acquire) & (CONFIG_PENDING | CONFIG_ACTIVE)) !=
           0U;
  }

  UartRxConfigGate() = default;
  UartRxConfigGate(const UartRxConfigGate&) = delete;
  UartRxConfigGate& operator=(const UartRxConfigGate&) = delete;

 private:
  static constexpr uint32_t RX_ACTIVE = 1U << 0U;
  static constexpr uint32_t CONFIG_PENDING = 1U << 1U;
  static constexpr uint32_t CONFIG_ACTIVE = 1U << 2U;

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
