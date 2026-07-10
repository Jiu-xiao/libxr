#include <cstddef>
#include <cstdint>
#include <cstring>

#include "test.hpp"
#include "uart_circular_dma_rx_model.hpp"

namespace
{

class FakeCircularDmaRxBackend
{
 public:
  FakeCircularDmaRxBackend()
      : port_(sizeof(storage_)), model_(LibXR::RawData(storage_, sizeof(storage_)))
  {
    port_ = ReadFun;
  }

  static LibXR::ErrorCode ReadFun(LibXR::ReadPort&, bool)
  {
    return LibXR::ErrorCode::PENDING;
  }

  void Start() { model_.Start(*this); }

  void ProduceTo(size_t position, bool in_isr = false)
  {
    ASSERT(position <= sizeof(storage_));
    remaining_ = sizeof(storage_) - position;
    model_.OnDataAvailable(*this, port_, in_isr);
  }

  void ReadQueued(uint8_t* output, size_t size)
  {
    ASSERT(port_.queue_data_->PopBatch(output, size) == LibXR::ErrorCode::OK);
  }

  [[nodiscard]] size_t QueuedSize() const { return port_.queue_data_->Size(); }

  [[nodiscard]] size_t LastPosition() const { return model_.LastPosition(); }

  uint8_t storage_[8]{};
  LibXR::ReadPort port_;
  size_t start_count_ = 0U;
  size_t prepare_count_ = 0U;
  uint8_t* prepared_data_ = nullptr;
  size_t prepared_size_ = 0U;

 private:
  friend class LibXR::UartCircularDmaRxModel;

  void StartCircularDmaRx(uint8_t* data, size_t size)
  {
    ASSERT(data == storage_);
    ASSERT(size == sizeof(storage_));
    ++start_count_;
    remaining_ = size;
  }

  [[nodiscard]] size_t GetCircularDmaRxRemaining() const { return remaining_; }

  void PrepareCircularDmaRxForCpu(uint8_t* data, size_t size)
  {
    ++prepare_count_;
    prepared_data_ = data;
    prepared_size_ = size;
  }

  LibXR::UartCircularDmaRxModel model_;
  size_t remaining_ = sizeof(storage_);
};

void TestLinearProgressAndPendingRead()
{
  FakeCircularDmaRxBackend backend;
  backend.Start();
  ASSERT(backend.start_count_ == 1U);
  ASSERT(backend.LastPosition() == 0U);

  const uint8_t first[] = {1U, 2U, 3U};
  std::memcpy(backend.storage_, first, sizeof(first));

  uint8_t received[sizeof(first)]{};
  auto status = LibXR::ReadOperation::OperationPollingStatus::READY;
  LibXR::ReadOperation operation(status);
  ASSERT(backend.port_(LibXR::RawData(received, sizeof(received)), operation, false) ==
         LibXR::ErrorCode::OK);
  ASSERT(status == LibXR::ReadOperation::OperationPollingStatus::RUNNING);

  backend.ProduceTo(sizeof(first));
  ASSERT(status == LibXR::ReadOperation::OperationPollingStatus::DONE);
  ASSERT(std::memcmp(received, first, sizeof(first)) == 0);
  ASSERT(backend.LastPosition() == sizeof(first));
  ASSERT(backend.QueuedSize() == 0U);
  ASSERT(backend.prepare_count_ == 1U);
  ASSERT(backend.prepared_data_ == backend.storage_);
  ASSERT(backend.prepared_size_ == sizeof(backend.storage_));

  backend.storage_[3] = 4U;
  backend.storage_[4] = 5U;
  backend.ProduceTo(5U);
  uint8_t second[2]{};
  backend.ReadQueued(second, sizeof(second));
  const uint8_t expected[] = {4U, 5U};
  ASSERT(std::memcmp(second, expected, sizeof(expected)) == 0);
}

void TestWrapEndAndRestart()
{
  FakeCircularDmaRxBackend backend;
  backend.Start();

  for (size_t i = 0U; i < 6U; ++i)
  {
    backend.storage_[i] = static_cast<uint8_t>(0x10U + i);
  }

  backend.ProduceTo(6U);
  uint8_t initial[6]{};
  backend.ReadQueued(initial, sizeof(initial));
  ASSERT(std::memcmp(initial, backend.storage_, sizeof(initial)) == 0);

  backend.storage_[6] = 0x20U;
  backend.storage_[7] = 0x21U;
  backend.storage_[0] = 0x22U;
  backend.storage_[1] = 0x23U;
  backend.storage_[2] = 0x24U;
  backend.ProduceTo(3U);
  uint8_t wrapped[5]{};
  backend.ReadQueued(wrapped, sizeof(wrapped));
  const uint8_t expected[] = {0x20U, 0x21U, 0x22U, 0x23U, 0x24U};
  ASSERT(std::memcmp(wrapped, expected, sizeof(expected)) == 0);

  for (size_t i = 3U; i < sizeof(backend.storage_); ++i)
  {
    backend.storage_[i] = static_cast<uint8_t>(0x30U + i);
  }
  backend.ProduceTo(sizeof(backend.storage_));
  ASSERT(backend.LastPosition() == sizeof(backend.storage_));
  uint8_t to_end[5]{};
  backend.ReadQueued(to_end, sizeof(to_end));
  ASSERT(std::memcmp(to_end, &backend.storage_[3], sizeof(to_end)) == 0);

  backend.Start();
  ASSERT(backend.start_count_ == 2U);
  ASSERT(backend.LastPosition() == 0U);
}

void TestQueueOverflowStillAdvancesCursor()
{
  FakeCircularDmaRxBackend backend;
  backend.Start();
  backend.ProduceTo(sizeof(backend.storage_));
  ASSERT(backend.QueuedSize() == sizeof(backend.storage_));

  backend.ProduceTo(3U);
  ASSERT(backend.QueuedSize() == sizeof(backend.storage_));
  ASSERT(backend.LastPosition() == 3U);
}

void TestSamePositionOnlyRunsCacheHook()
{
  FakeCircularDmaRxBackend backend;
  backend.Start();
  backend.ProduceTo(0U);
  backend.ProduceTo(0U);
  ASSERT(backend.prepare_count_ == 2U);
  ASSERT(backend.QueuedSize() == 0U);
  ASSERT(backend.LastPosition() == 0U);
}

void TestModelStateHasNoBackendOrPortPointers()
{
  ASSERT(sizeof(LibXR::UartCircularDmaRxModel) ==
         sizeof(LibXR::RawData) + sizeof(size_t));
}

}  // namespace

void test_uart_circular_dma_rx_model()
{
  TestLinearProgressAndPendingRead();
  TestWrapEndAndRestart();
  TestQueueOverflowStillAdvancesCursor();
  TestSamePositionOnlyRunsCacheHook();
  TestModelStateHasNoBackendOrPortPointers();
}
