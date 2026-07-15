#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace LibXR
{

/**
 * @brief SPSC mailbox retaining only the latest fully published value.
 *
 * The producer owns one back slot, the consumer owns one front slot, and an atomic
 * middle slot carries the latest completed publication. Repeated stores may overwrite
 * an unconsumed middle value, but never the value currently being copied by the
 * consumer.
 *
 * @tparam T Copy-assignable snapshot type.
 * @warning Exactly one producer may call Store(), and exactly one serialized consumer
 * may call LoadLatest(). Producer and consumer calls may overlap on different cores or
 * in thread/ISR contexts.
 */
template <typename T>
class LatestSnapshot
{
  static_assert(std::is_copy_constructible_v<T>,
                "LatestSnapshot requires a copy-constructible value type");
  static_assert(std::is_copy_assignable_v<T>,
                "LatestSnapshot requires a copy-assignable value type");

 public:
  /** Construct all three slots with the same initial value. */
  explicit LatestSnapshot(const T& initial) noexcept(
      std::is_nothrow_copy_constructible_v<T>)
      : slots_{initial, initial, initial}
  {
  }

  /**
   * @brief Publish one complete value.
   *
   * The value is copied into the producer-owned back slot before that slot is released
   * to the consumer as the newest middle slot.
   */
  void Store(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
  {
    slots_[back_] = value;

    const uint32_t previous =
        state_.exchange(Pack(back_, true), std::memory_order_acq_rel);
    back_ = Index(previous);
  }

  /**
   * @brief Copy the latest complete value into output.
   * @return true when this call acquired a newer publication; false when output was
   * copied from the consumer's previously acquired snapshot.
   */
  bool LoadLatest(T& output) noexcept(std::is_nothrow_copy_assignable_v<T>)
  {
    bool updated = false;
    uint32_t observed = state_.load(std::memory_order_acquire);

    while (HasNew(observed))
    {
      const uint32_t desired = Pack(front_, false);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      {
        front_ = Index(observed);
        updated = true;
        break;
      }
    }

    output = slots_[front_];
    return updated;
  }

  LatestSnapshot(const LatestSnapshot&) = delete;
  LatestSnapshot& operator=(const LatestSnapshot&) = delete;
  LatestSnapshot(LatestSnapshot&&) = delete;
  LatestSnapshot& operator=(LatestSnapshot&&) = delete;

 private:
  static constexpr uint32_t INDEX_MASK = 0x3U;
  static constexpr uint32_t HAS_NEW_BIT = 1U << 2U;

  static constexpr uint32_t Pack(uint32_t index, bool has_new)
  {
    return index | (has_new ? HAS_NEW_BIT : 0U);
  }

  static constexpr uint32_t Index(uint32_t state) { return state & INDEX_MASK; }

  static constexpr bool HasNew(uint32_t state) { return (state & HAS_NEW_BIT) != 0U; }

  T slots_[3];
  std::atomic<uint32_t> state_{Pack(1U, false)};
  uint32_t front_ = 0U;
  uint32_t back_ = 2U;
};

}  // namespace LibXR
