#include <cstddef>
#include <cstdint>
#include <cstring>

#include "test.hpp"
#include "uart_dma_tx_model.hpp"

namespace
{

class FakeUartDmaBackend
{
 public:
  struct StartRecord
  {
    uint8_t data[8]{};
    size_t size = 0U;
    int block = 0;
  };

  FakeUartDmaBackend()
      : port_(4U, 8U), model_(*this, port_, LibXR::RawData(storage_, sizeof(storage_)))
  {
    port_ = WriteFun;
  }

  static LibXR::ErrorCode WriteFun(LibXR::WritePort& port, bool in_isr)
  {
    auto* backend = LibXR::ContainerOf(&port, &FakeUartDmaBackend::port_);
    return backend->model_.Submit(in_isr);
  }

  LibXR::ErrorCode Write(const uint8_t* data, size_t size, LibXR::WriteOperation& op)
  {
    return port_(LibXR::ConstRawData(data, size), op, false);
  }

  void Complete(LibXR::ErrorCode result = LibXR::ErrorCode::OK)
  {
    model_.OnTransferDone(false, result);
  }

  bool RestartActive() { return model_.RestartActive(); }

  [[nodiscard]] bool IsBusy() const { return model_.IsBusy(); }

  [[nodiscard]] const StartRecord& StartAt(size_t index) const
  {
    ASSERT(index < start_count_);
    return starts_[index];
  }

  [[nodiscard]] size_t StartCount() const { return start_count_; }

  bool allow_start_ = true;

 private:
  friend class LibXR::UartDmaTxModel<FakeUartDmaBackend>;

  bool StartDmaTx(uint8_t* data, size_t size, int block)
  {
    ASSERT(start_count_ < (sizeof(starts_) / sizeof(starts_[0])));
    if (!allow_start_)
    {
      return false;
    }

    StartRecord& record = starts_[start_count_++];
    ASSERT(size <= sizeof(record.data));
    std::memcpy(record.data, data, size);
    record.size = size;
    record.block = block;
    return true;
  }

  alignas(size_t) uint8_t storage_[16]{};
  LibXR::WritePort port_;
  LibXR::UartDmaTxModel<FakeUartDmaBackend> model_;
  StartRecord starts_[8]{};
  size_t start_count_ = 0U;
};

using PollStatus = LibXR::WriteOperation::OperationPollingStatus;

void AssertStart(const FakeUartDmaBackend& backend, size_t index, const uint8_t* data,
                 size_t size, int block)
{
  const auto& record = backend.StartAt(index);
  ASSERT(record.size == size);
  ASSERT(record.block == block);
  ASSERT(std::memcmp(record.data, data, size) == 0);
}

void TestPipelineAndRestart()
{
  FakeUartDmaBackend backend;
  const uint8_t first[] = {1U, 2U, 3U};
  const uint8_t second[] = {4U, 5U};
  const uint8_t third[] = {6U};

  PollStatus first_status = PollStatus::READY;
  PollStatus second_status = PollStatus::READY;
  PollStatus third_status = PollStatus::READY;
  LibXR::WriteOperation first_op(first_status);
  LibXR::WriteOperation second_op(second_status);
  LibXR::WriteOperation third_op(third_status);

  ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
  ASSERT(first_status == PollStatus::DONE);
  ASSERT(backend.IsBusy());
  ASSERT(backend.StartCount() == 1U);
  AssertStart(backend, 0U, first, sizeof(first), 0);

  ASSERT(backend.RestartActive());
  ASSERT(backend.StartCount() == 2U);
  AssertStart(backend, 1U, first, sizeof(first), 0);

  ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
  ASSERT(second_status == PollStatus::RUNNING);
  ASSERT(backend.Write(third, sizeof(third), third_op) == LibXR::ErrorCode::OK);
  ASSERT(third_status == PollStatus::RUNNING);

  backend.Complete();
  ASSERT(second_status == PollStatus::DONE);
  ASSERT(third_status == PollStatus::RUNNING);
  ASSERT(backend.StartCount() == 3U);
  AssertStart(backend, 2U, second, sizeof(second), 1);

  backend.Complete();
  ASSERT(third_status == PollStatus::DONE);
  ASSERT(backend.StartCount() == 4U);
  AssertStart(backend, 3U, third, sizeof(third), 0);

  backend.Complete();
  ASSERT(!backend.IsBusy());
  ASSERT(backend.StartCount() == 4U);

  backend.Complete();
  backend.Complete(LibXR::ErrorCode::FAILED);
  ASSERT(backend.StartCount() == 4U);
}

void TestStartAndTransferFailures()
{
  {
    FakeUartDmaBackend backend;
    const uint8_t data[] = {0x11U};
    PollStatus status = PollStatus::READY;
    LibXR::WriteOperation op(status);

    backend.allow_start_ = false;
    ASSERT(backend.Write(data, sizeof(data), op) == LibXR::ErrorCode::FAILED);
    ASSERT(status == PollStatus::ERROR);
    ASSERT(!backend.IsBusy());
  }

  {
    FakeUartDmaBackend backend;
    const uint8_t first[] = {0x21U};
    const uint8_t second[] = {0x22U};
    PollStatus first_status = PollStatus::READY;
    PollStatus second_status = PollStatus::READY;
    LibXR::WriteOperation first_op(first_status);
    LibXR::WriteOperation second_op(second_status);

    ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
    ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
    backend.allow_start_ = false;
    backend.Complete();
    ASSERT(first_status == PollStatus::DONE);
    ASSERT(second_status == PollStatus::ERROR);
    ASSERT(!backend.IsBusy());
  }

  {
    FakeUartDmaBackend backend;
    const uint8_t first[] = {0x31U};
    const uint8_t second[] = {0x32U};
    PollStatus first_status = PollStatus::READY;
    PollStatus second_status = PollStatus::READY;
    LibXR::WriteOperation first_op(first_status);
    LibXR::WriteOperation second_op(second_status);

    ASSERT(backend.Write(first, sizeof(first), first_op) == LibXR::ErrorCode::OK);
    ASSERT(backend.Write(second, sizeof(second), second_op) == LibXR::ErrorCode::OK);
    backend.Complete(LibXR::ErrorCode::FAILED);
    ASSERT(first_status == PollStatus::DONE);
    ASSERT(second_status == PollStatus::ERROR);
    ASSERT(!backend.IsBusy());
    ASSERT(backend.StartCount() == 1U);
  }
}

}  // namespace

void test_uart_dma_tx_model()
{
  TestPipelineAndRestart();
  TestStartAndTransferFailures();
}
