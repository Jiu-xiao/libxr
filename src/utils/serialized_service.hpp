#pragma once

#include <atomic>
#include <cstdint>

namespace LibXR
{

/**
 * @brief 合并通知并串行执行服务函数 / Coalesce notifications and serialize service
 * execution
 *
 * 每次调用先把事件合并进 32 位 pending mask，再竞争唯一 owner。未取得 owner 的调用
 * 立即返回；owner 原子取走事件快照并执行 handler，直到没有待处理事件，或者把后续
 * 服务交给另一个 owner。handler 执行期间再次调用 `Invoke()` 只会留下下一轮事件，
 * 不会递归执行 handler。
 * Each invocation first merges events into a 32-bit pending mask and then competes for
 * sole ownership. A caller that does not acquire ownership returns immediately. The
 * owner atomically consumes event snapshots and runs the handler until no work remains
 * or another owner takes over. Reentrant `Invoke()` calls only schedule another round;
 * they never invoke the handler recursively.
 *
 * @note 事件是可合并的 level-triggered 通知，不记录发生次数或 payload。所有入口必须
 * 提供同一个逻辑 handler；实际 handler 在取得 owner 的调用上下文中执行。
 * Events are coalesced level-triggered notifications and do not preserve occurrence
 * counts or payloads. Every entry must provide the same logical handler; execution uses
 * the context of the caller that acquires ownership.
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
   * @param events 要合并的非零事件位图 / Nonzero event mask to merge
   * @param handler 处理每轮事件快照的服务函数 / Service function handling each snapshot
   * @return 当前调用取得过 owner 时返回 true；事件交给已有 owner 时返回 false / True
   * if this invocation acquired ownership; false if an existing owner will service it
   */
  template <typename Handler>
  bool Invoke(uint32_t events, Handler&& handler) noexcept
  {
    if (events == 0U)
    {
      return false;
    }

    pending_.fetch_or(events, std::memory_order_release);

    OwnerState expected = OwnerState::IDLE;
    if (!owner_.compare_exchange_strong(expected, OwnerState::RUNNING,
                                        std::memory_order_acquire,
                                        std::memory_order_relaxed))
    {
      return false;
    }

    while (true)
    {
      const uint32_t snapshot = pending_.exchange(0U, std::memory_order_acq_rel);
      // Another owner may have consumed the event between our release-side check and
      // reacquire CAS. Ownership is still valid, but an empty snapshot is not work.
      if (snapshot != 0U)
      {
        handler(snapshot);
      }

      owner_.store(OwnerState::IDLE, std::memory_order_release);

      if (pending_.load(std::memory_order_acquire) == 0U)
      {
        return true;
      }

      expected = OwnerState::IDLE;
      if (!owner_.compare_exchange_strong(expected, OwnerState::RUNNING,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed))
      {
        return true;
      }
    }
  }

  SerializedService(const SerializedService&) = delete;
  SerializedService& operator=(const SerializedService&) = delete;

 private:
  enum class OwnerState : uint32_t
  {
    IDLE = 0U,
    RUNNING = 1U,
  };

  std::atomic<uint32_t> pending_{0U};
  std::atomic<OwnerState> owner_{OwnerState::IDLE};
};

}  // namespace LibXR
