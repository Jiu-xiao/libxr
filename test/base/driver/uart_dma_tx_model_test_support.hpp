#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

#include "../../common/rw/rw_mode_test_common.hpp"
#include "test.hpp"
#include "model/uart_dma_tx_model.hpp"

namespace LibXRTest::UartDmaTx
{

inline constexpr size_t DMA_BLOCK_SIZE = 8U;
inline constexpr uint32_t WAIT_TIMEOUT_MS = 500U;

template <typename Predicate>
bool WaitUntil(Predicate&& predicate, uint32_t timeout_ms = WAIT_TIMEOUT_MS)
{
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!predicate())
  {
    if (std::chrono::steady_clock::now() >= deadline)
    {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

class FakeUartDmaBackend
{
 public:
  struct StartRecord
  {
    uint8_t data[DMA_BLOCK_SIZE]{};
    size_t size = 0U;
    int block = 0;
  };

  FakeUartDmaBackend()
      : port_(8U, 64U), model_(*this, port_, LibXR::RawData(storage_, sizeof(storage_)))
  {
    port_ = WriteFun;
  }

  static LibXR::ErrorCode WriteFun(LibXR::WritePort& port, bool in_isr)
  {
    auto* backend = LibXR::ContainerOf(&port, &FakeUartDmaBackend::port_);
    return backend->model_.Submit(in_isr);
  }

  LibXR::ErrorCode Write(const uint8_t* data, size_t size, LibXR::WriteOperation& op,
                         bool in_isr = false)
  {
    return port_(LibXR::ConstRawData(data, size), op, in_isr);
  }

  LibXR::WritePort& Port() { return port_; }

  void Complete(bool in_isr = false) { model_.OnTransferDone(in_isr); }
  void Error(bool in_isr = false) { model_.OnTransferError(in_isr); }

  bool RestartActive() { return model_.RestartActive(); }
  [[nodiscard]] bool IsBusy() const { return model_.IsBusy(); }
  [[nodiscard]] size_t BufferSize() const { return model_.BufferSize(); }

  [[nodiscard]] const StartRecord& StartAt(size_t index) const
  {
    ASSERT(index < StartCount());
    return starts_[index];
  }

  [[nodiscard]] size_t StartCount() const
  {
    return start_count_.load(std::memory_order_acquire);
  }

  [[nodiscard]] size_t QueuedInfoCount() const { return port_.queue_info_->Size(); }
  [[nodiscard]] size_t QueuedDataSize() const { return port_.queue_data_->Size(); }

  void SetStartAllowed(bool allowed)
  {
    allow_start_.store(allowed ? 1U : 0U, std::memory_order_release);
  }

  void BlockNextStart()
  {
    start_blocked_.store(0U, std::memory_order_release);
    release_start_.store(0U, std::memory_order_release);
    block_start_at_.store(static_cast<uint32_t>(StartCount()), std::memory_order_release);
  }

  bool WaitUntilStartBlocked()
  {
    return WaitUntil([&]
                     { return start_blocked_.load(std::memory_order_acquire) != 0U; });
  }

  void ReleaseBlockedStart() { release_start_.store(1U, std::memory_order_release); }

 private:
  friend class LibXR::UartDmaTxModel<FakeUartDmaBackend>;

  bool StartDmaTx(uint8_t* data, size_t size, int block)
  {
    const uint32_t start_index = static_cast<uint32_t>(StartCount());
    if (block_start_at_.load(std::memory_order_acquire) == start_index)
    {
      start_blocked_.store(1U, std::memory_order_release);
      const bool released =
          WaitUntil([&] { return release_start_.load(std::memory_order_acquire) != 0U; });
      ASSERT(released);
      block_start_at_.store(UINT32_MAX, std::memory_order_release);
    }

    if (allow_start_.load(std::memory_order_acquire) == 0U)
    {
      return false;
    }

    ASSERT(start_index < (sizeof(starts_) / sizeof(starts_[0])));
    ASSERT(size <= sizeof(starts_[start_index].data));
    StartRecord& record = starts_[start_index];
    std::memcpy(record.data, data, size);
    record.size = size;
    record.block = block;
    start_count_.store(start_index + 1U, std::memory_order_release);
    return true;
  }

  alignas(size_t) uint8_t storage_[DMA_BLOCK_SIZE * 2U]{};
  LibXR::WritePort port_;
  LibXR::UartDmaTxModel<FakeUartDmaBackend> model_;
  StartRecord starts_[32]{};
  std::atomic<uint32_t> start_count_{0U};
  std::atomic<uint32_t> allow_start_{1U};
  std::atomic<uint32_t> block_start_at_{UINT32_MAX};
  std::atomic<uint32_t> start_blocked_{0U};
  std::atomic<uint32_t> release_start_{0U};
};

inline void AssertStart(const FakeUartDmaBackend& backend, size_t index,
                        const uint8_t* data, size_t size, int block)
{
  const auto& record = backend.StartAt(index);
  ASSERT(record.size == size);
  ASSERT(record.block == block);
  ASSERT(std::memcmp(record.data, data, size) == 0);
}

struct CallbackProbe
{
  std::atomic<uint32_t> count{0U};
  std::atomic<uint32_t> in_isr{UINT32_MAX};
  std::atomic<uint32_t> result{static_cast<uint32_t>(LibXR::ErrorCode::OK)};
  LibXR::WriteOperation::Callback callback =
      LibXR::WriteOperation::Callback::Create(OnComplete, this);

  static void OnComplete(bool in_isr, CallbackProbe* self, LibXR::ErrorCode result)
  {
    self->in_isr.store(in_isr ? 1U : 0U, std::memory_order_release);
    self->result.store(static_cast<uint32_t>(result), std::memory_order_release);
    self->count.fetch_add(1U, std::memory_order_acq_rel);
  }

  bool WaitForCount(uint32_t expected)
  {
    return WaitUntil([&] { return count.load(std::memory_order_acquire) == expected; });
  }
};

void RunStateTests();
void RunBoundaryTests();
void RunOperationTests();
void RunConcurrencyTests();

}  // namespace LibXRTest::UartDmaTx
