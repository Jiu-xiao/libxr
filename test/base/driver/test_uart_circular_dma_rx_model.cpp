#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "model/uart_circular_dma_rx_model.hpp"
#include "test.hpp"

namespace
{

class FakeCircularRxBackend
{
 public:
  void StartCircularDmaRx(uint8_t* buffer, size_t size)
  {
    started_buffer = buffer;
    started_size = size;
    start_count.fetch_add(1U, std::memory_order_acq_rel);
  }

  [[nodiscard]] size_t GetCircularDmaRxRemaining() const { return remaining; }

  void PrepareCircularDmaRxForCpu(uint8_t* buffer, size_t size)
  {
    ASSERT(buffer == started_buffer);
    ASSERT(size == started_size);
    prepare_count.fetch_add(1U, std::memory_order_acq_rel);
  }

  uint8_t* started_buffer = nullptr;
  size_t started_size = 0U;
  size_t remaining = 0U;
  std::atomic<uint32_t> start_count{0U};
  std::atomic<uint32_t> prepare_count{0U};
};

void ExpectQueued(LibXR::ReadPort& port, const uint8_t* expected, size_t size)
{
  uint8_t actual[16]{};
  ASSERT(size <= sizeof(actual));
  ASSERT(port.queue_data_->PopBatch(actual, size) == LibXR::ErrorCode::OK);
  ASSERT(std::memcmp(actual, expected, size) == 0);
  ASSERT(port.queue_data_->Size() == 0U);
}

void TestStartResetsPositionAndBindsStorage()
{
  uint8_t storage[8]{};
  LibXR::UartCircularDmaRxModel model(LibXR::RawData(storage, sizeof(storage)));
  FakeCircularRxBackend backend;

  model.ResetPosition();
  model.Start(backend);
  ASSERT(backend.start_count.load(std::memory_order_acquire) == 1U);
  ASSERT(backend.started_buffer == storage);
  ASSERT(backend.started_size == sizeof(storage));
  ASSERT(model.Buffer() == storage);
  ASSERT(model.BufferSize() == sizeof(storage));
  ASSERT(model.LastPosition() == 0U);
}

void TestLinearAndWrappedIntervals()
{
  uint8_t storage[] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
  LibXR::UartCircularDmaRxModel model(LibXR::RawData(storage, sizeof(storage)));
  FakeCircularRxBackend backend;
  LibXR::ReadPort port(16U);
  model.Start(backend);

  backend.remaining = 5U;  // position 3
  model.OnDataAvailable(backend, port, true);
  const uint8_t first[] = {0U, 1U, 2U};
  ExpectQueued(port, first, sizeof(first));
  ASSERT(model.LastPosition() == 3U);

  backend.remaining = 2U;  // position 6
  model.OnDataAvailable(backend, port, false);
  const uint8_t second[] = {3U, 4U, 5U};
  ExpectQueued(port, second, sizeof(second));
  ASSERT(model.LastPosition() == 6U);

  backend.remaining = 6U;  // wrapped to position 2
  model.OnDataAvailable(backend, port, true);
  const uint8_t wrapped[] = {6U, 7U, 0U, 1U};
  ExpectQueued(port, wrapped, sizeof(wrapped));
  ASSERT(model.LastPosition() == 2U);
  ASSERT(backend.prepare_count.load(std::memory_order_acquire) == 3U);
}

void TestQueueFullDropsIntervalAndAdvancesPosition()
{
  uint8_t storage[] = {0x10U, 0x11U, 0x12U, 0x13U};
  LibXR::UartCircularDmaRxModel model(LibXR::RawData(storage, sizeof(storage)));
  FakeCircularRxBackend backend;
  LibXR::ReadPort port(2U);
  model.Start(backend);

  backend.remaining = 1U;  // three bytes cannot fit as one PushBatch
  model.OnDataAvailable(backend, port, true);
  ASSERT(port.queue_data_->Size() == 0U);
  ASSERT(model.LastPosition() == 3U);

  // The dropped interval is not retried when the DMA position is unchanged.
  model.OnDataAvailable(backend, port, false);
  ASSERT(port.queue_data_->Size() == 0U);
  ASSERT(model.LastPosition() == 3U);
}

void TestEqualModuloPositionRepresentsNoObservableProgress()
{
  uint8_t storage[] = {0x20U, 0x21U, 0x22U, 0x23U};
  LibXR::UartCircularDmaRxModel model(LibXR::RawData(storage, sizeof(storage)));
  FakeCircularRxBackend backend;
  LibXR::ReadPort port(8U);
  model.Start(backend);

  backend.remaining = sizeof(storage);  // modulo position remains zero
  model.OnDataAvailable(backend, port, true);
  ASSERT(port.queue_data_->Size() == 0U);
  ASSERT(model.LastPosition() == 0U);
  ASSERT(backend.prepare_count.load(std::memory_order_acquire) == 1U);
}

}  // namespace

void test_uart_circular_dma_rx_model()
{
  TestStartResetsPositionAndBindsStorage();
  TestLinearAndWrappedIntervals();
  TestQueueFullDropsIntervalAndAdvancesPosition();
  TestEqualModuloPositionRepresentsNoObservableProgress();
}
