#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/uart/uart_circular_dma_rx_model.hpp"
#include "test.hpp"

namespace
{

class CircularRxBackend
{
 public:
  [[nodiscard]] size_t GetCircularDmaRxRemaining() const { return remaining; }

  void PrepareCircularDmaRxForCpu(uint8_t*, size_t) {}

  size_t remaining = 0U;
};

}  // namespace

void test_uart_circular_dma_rx_model()
{
  using LibXR::ErrorCode;
  using LibXR::OperationPollingStatus;
  using LibXR::RawData;
  using LibXR::ReadOperation;
  using LibXR::ReadPort;
  using LibXR::UartCircularDmaRxModel;

  std::array<uint8_t, 8U> storage{};
  for (size_t i = 0U; i < storage.size(); ++i)
  {
    storage[i] = static_cast<uint8_t>(i);
  }

  UartCircularDmaRxModel model(RawData{storage.data(), storage.size()});
  CircularRxBackend backend;
  ReadPort port(2U);

  backend.remaining = 5U;
  {
    auto queue = port.GetReadQueue();
    model.OnDataAvailable(backend, queue);
    queue.Publish();
  }
  ASSERT(model.LastPosition() == 3U);
  ASSERT(port.Size() == 0U);

  std::array<uint8_t, 2U> received{};
  OperationPollingStatus status;
  ReadOperation operation(status);
  ASSERT(port(RawData{received.data(), received.size()}, operation) == ErrorCode::OK);
  ASSERT(status.Load() == OperationPollingStatus::RUNNING);

  backend.remaining = 3U;
  auto queue = port.GetReadQueue();
  model.OnDataAvailable(backend, queue);
  ASSERT(model.LastPosition() == 5U);
  ASSERT(port.Size() == 2U);
  ASSERT(status.Load() == OperationPollingStatus::RUNNING);

  queue.Publish();
  ASSERT(status.Load() == OperationPollingStatus::DONE);
  ASSERT(received[0U] == 3U);
  ASSERT(received[1U] == 4U);
  ASSERT(port.Size() == 0U);
}
