#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/uart/uart_linked_list_dma_rx_model.hpp"
#include "driver/uart/uart_rx_config_gate.hpp"
#include "test.hpp"

namespace
{

struct PreparedSpan
{
  uint8_t* address = nullptr;
  size_t size = 0U;
};

class LinkedListRxBackend
{
 public:
  void StartLinkedListDmaRx(uint8_t* buffer, size_t size, size_t descriptor_count)
  {
    start_buffer = buffer;
    start_size = size;
    start_descriptor_count = descriptor_count;
    ++start_calls;
  }

  uint8_t* GetLinkedListDmaRxProducer()
  {
    ++producer_reads;
    return producer;
  }

  void PrepareLinkedListDmaRxForCpu(uint8_t* address, size_t size)
  {
    ASSERT(prepared_count < prepared.size());
    prepared[prepared_count++] = {address, size};
  }

  void ResetObservations()
  {
    producer_reads = 0U;
    prepared_count = 0U;
    prepared = {};
  }

  uint8_t* producer = nullptr;
  uint8_t* start_buffer = nullptr;
  size_t start_size = 0U;
  size_t start_descriptor_count = 0U;
  size_t start_calls = 0U;
  size_t producer_reads = 0U;
  std::array<PreparedSpan, 2U> prepared{};
  size_t prepared_count = 0U;
};

void ExpectQueue(LibXR::ReadPort& port, const uint8_t* expected, size_t size)
{
  std::array<uint8_t, 32U> received{};
  ASSERT(size <= received.size());
  ASSERT(port.queue_data_->PopBatch(received.data(), size) == LibXR::ErrorCode::OK);
  for (size_t i = 0U; i < size; ++i)
  {
    ASSERT(received[i] == expected[i]);
  }
  ASSERT(port.queue_data_->Size() == 0U);
}

}  // namespace

void test_uart_linked_list_dma_rx_model()
{
  using LibXR::RawData;
  using LibXR::ReadPort;
  using LibXR::UartLinkedListDmaRxModel;

  std::array<uint8_t, 16U> storage{};
  for (size_t i = 0U; i < storage.size(); ++i)
  {
    storage[i] = static_cast<uint8_t>(i);
  }

  {
    UartLinkedListDmaRxModel<4U> model(RawData{storage.data(), storage.size()});
    LinkedListRxBackend backend;
    ReadPort port(32U);

    backend.producer = &storage[6U];
    ASSERT(model.OnDataAvailable(backend, port));
    ASSERT(model.LastPosition() == 6U);
    uint8_t expected[6U] = {0U, 1U, 2U, 3U, 4U, 5U};
    ExpectQueue(port, expected, sizeof(expected));

    backend.ResetObservations();
    model.Start(backend);
    ASSERT(model.LastPosition() == 0U);
    ASSERT(backend.start_calls == 1U);
    ASSERT(backend.start_buffer == storage.data());
    ASSERT(backend.start_size == storage.size());
    ASSERT(backend.start_descriptor_count == 4U);
  }

  {
    UartLinkedListDmaRxModel<4U> model(RawData{storage.data(), storage.size()});
    LinkedListRxBackend backend;
    ReadPort port(32U);

    backend.producer = &storage[4U];
    ASSERT(model.OnDataAvailable(backend, port));
    ASSERT(backend.producer_reads == 1U);
    ASSERT(backend.prepared_count == 1U);
    ASSERT(backend.prepared[0U].address == storage.data());
    ASSERT(backend.prepared[0U].size == 4U);

    backend.ResetObservations();
    backend.producer = &storage[8U];
    ASSERT(model.OnDataAvailable(backend, port));
    ASSERT(backend.producer_reads == 1U);
    ASSERT(backend.prepared_count == 1U);
    ASSERT(backend.prepared[0U].address == &storage[4U]);
    ASSERT(backend.prepared[0U].size == 4U);
    ExpectQueue(port, storage.data(), 8U);

    backend.ResetObservations();
    backend.producer = &storage[14U];
    ASSERT(model.OnDataAvailable(backend, port));
    ExpectQueue(port, &storage[8U], 6U);

    backend.ResetObservations();
    backend.producer = &storage[3U];
    ASSERT(model.OnDataAvailable(backend, port));
    ASSERT(backend.producer_reads == 1U);
    ASSERT(backend.prepared_count == 2U);
    ASSERT(backend.prepared[0U].address == &storage[14U]);
    ASSERT(backend.prepared[0U].size == 2U);
    ASSERT(backend.prepared[1U].address == storage.data());
    ASSERT(backend.prepared[1U].size == 3U);
    const uint8_t wrapped[5U] = {14U, 15U, 0U, 1U, 2U};
    ExpectQueue(port, wrapped, sizeof(wrapped));
  }

  {
    UartLinkedListDmaRxModel<4U> model(RawData{storage.data(), storage.size()});
    LinkedListRxBackend backend;
    ReadPort port(32U);

    backend.producer = &storage[12U];
    ASSERT(model.OnDataAvailable(backend, port));
    ExpectQueue(port, storage.data(), 12U);

    backend.ResetObservations();
    backend.producer = storage.data() + storage.size();
    ASSERT(model.OnDataAvailable(backend, port));
    ASSERT(backend.producer_reads == 1U);
    ASSERT(backend.prepared_count == 1U);
    ASSERT(backend.prepared[0U].address == &storage[12U]);
    ASSERT(backend.prepared[0U].size == 4U);
    ASSERT(model.LastPosition() == 0U);
    ExpectQueue(port, &storage[12U], 4U);

    backend.ResetObservations();
    backend.producer = storage.data();
    ASSERT(!model.OnDataAvailable(backend, port));
    ASSERT(backend.producer_reads == 1U);
    ASSERT(backend.prepared_count == 0U);
  }

  {
    UartLinkedListDmaRxModel<4U> model(RawData{storage.data(), storage.size()});
    LinkedListRxBackend backend;
    ReadPort port(4U);

    backend.producer = &storage[6U];
    ASSERT(model.OnDataAvailable(backend, port));
    ASSERT(model.LastPosition() == 6U);
    ASSERT(port.queue_data_->Size() == 0U);

    backend.producer = &storage[8U];
    ASSERT(model.OnDataAvailable(backend, port));
    const uint8_t retained[2U] = {6U, 7U};
    ExpectQueue(port, retained, sizeof(retained));
  }

  {
    UartLinkedListDmaRxModel<4U> model(RawData{storage.data(), storage.size()});
    LinkedListRxBackend backend;
    ReadPort port(32U);
    LibXR::UartRxConfigGate gate;

    ASSERT(gate.TryReserveConfig());
    gate.PublishConfig();
    ASSERT(gate.TryEnterConfig());
    ASSERT(!gate.TryEnterRx());
    ASSERT(backend.producer_reads == 0U);
    ASSERT(model.LastPosition() == 0U);
    gate.LeaveConfig();

    ASSERT(gate.TryEnterRx());
    backend.producer = &storage[4U];
    ASSERT(model.OnDataAvailable(backend, port));
    ASSERT(backend.producer_reads == 1U);
    ASSERT(!gate.LeaveRx());
  }

  {
    LibXR::UartRxConfigGate gate;

    ASSERT(gate.TryEnterRx());
    ASSERT(gate.TryReserveConfig());
    gate.PublishConfig();
    ASSERT(!gate.TryEnterConfig());
    ASSERT(gate.LeaveRx());
    ASSERT(gate.TryEnterConfig());
    gate.LeaveConfig();
  }

  {
    UartLinkedListDmaRxModel<> disabled(RawData{nullptr, 0U});
    LinkedListRxBackend backend;
    ReadPort port(1U);
    ASSERT(!disabled.OnDataAvailable(backend, port));
    ASSERT(backend.producer_reads == 0U);
  }
}
