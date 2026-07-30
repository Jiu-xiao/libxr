#pragma once

#include <atomic>
#include <cstdint>

namespace LibXR
{

/**
 * @brief 合并通知并串行执行服务函数 / Coalesce notifications and serialize service
 * execution
 *
 * 每次调用先把事件合并进一个 32 位状态字，再竞争同一状态字中的唯一 owner 位。未取得
 * owner 的调用立即返回；owner 原子取走事件快照并执行 handler，直到没有待处理事件。
 * handler 执行期间再次调用 `Invoke()` 只会留下下一轮事件，不会递归执行 handler。
 * Each invocation first merges events into one 32-bit state word and then competes for
 * the sole owner bit in that same word. A caller that does not acquire ownership returns
 * immediately. The owner atomically consumes event snapshots and runs the handler until
 * no work remains. Reentrant `Invoke()` calls only schedule another round; they never
 * invoke the handler recursively.
 *
 * @note 低 31 位是可合并的 level-triggered 事件；最高位保留给 owner。事件不记录
 * 发生次数或 payload。所有入口必须提供同一个逻辑 handler；实际 handler 在取得 owner
 * 的调用上下文中执行。
 * Events are coalesced level-triggered notifications and do not preserve occurrence
 * counts or payloads. The low 31 bits are available for events and the high bit is
 * reserved for ownership. Every entry must provide the same logical handler; execution
 * uses the context of the caller that acquires ownership.
 * @warning handler 不得抛出异常；可能由 ISR 取得 owner 时，handler 还必须有界且
 * 非阻塞。The handler must not throw. If an ISR may acquire ownership, the handler must
 * also be bounded and non-blocking.
 */
class SerializedService
{
 public:
  SerializedService() = default;

  /**
   * @brief 发布事件并尝试执行服务 / Publish events and try to run the service
   * @tparam Handler 可调用对象类型，签名为 `void(uint32_t)` / Callable type with the
   * signature `void(uint32_t)`
   * @param events 要合并的低 31 位非零事件位图 / Nonzero low-31-bit event mask to merge
   * @param handler 处理每轮事件快照的服务函数 / Service function handling each snapshot
   * @return 当前调用取得过 owner 时返回 true；事件交给已有 owner 时返回 false / True
   * if this invocation acquired ownership; false if an existing owner will service it
   */
  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    if ((events == 0U) || ((events & OWNER_BIT) != 0U))
    {
      return false;
    }

    uint32_t observed = state_.fetch_or(events, std::memory_order_release) | events;

    while ((observed & OWNER_BIT) == 0U)
    {
      // Another owner may have consumed this invocation's event and released the state
      // before our CAS. An empty state means there is no remaining work to claim.
      if ((observed & EVENT_MASK) == 0U)
      {
        return false;
      }

      const uint32_t desired = observed | OWNER_BIT;
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      {
        break;
      }
    }

    if ((observed & OWNER_BIT) != 0U)
    {
      return false;
    }

    while (true)
    {
      const uint32_t snapshot =
          state_.exchange(OWNER_BIT, std::memory_order_acq_rel) & EVENT_MASK;
      if (snapshot != 0U)
      {
        handler(snapshot);
      }

      uint32_t expected = OWNER_BIT;
      if (state_.compare_exchange_strong(expected, 0U, std::memory_order_release,
                                         std::memory_order_acquire))
      {
        return true;
      }
      // A publisher ORed new events while OWNER_BIT was set. Keep ownership and consume
      // the next snapshot; release cannot race past an already-published event.
    }
  }

  SerializedService(const SerializedService&) = delete;
  SerializedService& operator=(const SerializedService&) = delete;

 private:
  static constexpr uint32_t OWNER_BIT = 1U << 31U;
  static constexpr uint32_t EVENT_MASK = ~OWNER_BIT;

  std::atomic<uint32_t> state_{0U};
};

}  // namespace LibXR
