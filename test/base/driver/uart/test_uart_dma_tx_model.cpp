#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/uart/uart_dma_model.hpp"
#include "driver/uart/uart_execution_policy.hpp"
#include "test.hpp"

namespace
{

class UartDmaTxTestBackend;
using UartDmaTxTestModel =
    LibXR::UartDmaModel<UartDmaTxTestBackend, LibXR::UartDirectPolicy>;

class UartDmaTxTestBackend
{
 public:
  LibXR::ErrorCode ValidateConfig(LibXR::UART::Configuration) const
  {
    return LibXR::ErrorCode::OK;
  }

  LibXR::UartDmaTxStartResult StartDmaTx(uint8_t*, size_t, int, bool in_isr)
  {
    ASSERT(!disabled_);
    ++start_count_;
    if (inject_config_on_start_)
    {
      inject_config_on_start_ = false;
      ASSERT(model_ != nullptr);
      ASSERT(model_->SetConfig(TEST_CONFIG, in_isr) == LibXR::ErrorCode::OK);
    }
    return start_result_;
  }

  LibXR::UartDmaControlResult AdvanceConfig(LibXR::UART::Configuration, bool, bool)
  {
    disabled_ = true;
    return stop_ready_ ? LibXR::UartDmaControlResult::Completed()
                       : LibXR::UartDmaControlResult::Pending();
  }

  LibXR::UartDmaControlResult AdvanceRecovery(bool, bool)
  {
    disabled_ = true;
    return LibXR::UartDmaControlResult::Completed();
  }

  LibXR::UartDmaControlProgress CompleteConfig(bool)
  {
    disabled_ = false;
    return LibXR::UartDmaControlProgress::COMPLETED;
  }

  LibXR::UartDmaControlProgress CompleteRecovery(bool)
  {
    disabled_ = false;
    return LibXR::UartDmaControlProgress::COMPLETED;
  }

  static constexpr LibXR::UART::Configuration TEST_CONFIG{
      230400U, LibXR::UART::Parity::NO_PARITY, 8U, 1U};

  UartDmaTxTestModel* model_ = nullptr;
  size_t start_count_ = 0U;
  LibXR::UartDmaTxStartResult start_result_ = LibXR::UartDmaTxStartResult::STARTED;
  bool inject_config_on_start_ = false;
  bool stop_ready_ = false;
  bool disabled_ = false;
};

class UartDmaTxTestRig
{
 public:
  UartDmaTxTestRig()
      : port_(4U, 8U),
        model_(backend_, policy_, port_,
               LibXR::RawData{dma_storage_.data(), dma_storage_.size()})
  {
    backend_.model_ = &model_;
    port_ = WriteFun;
  }

  static LibXR::ErrorCode WriteFun(LibXR::WritePort& port, bool in_isr)
  {
    auto* rig = LibXR::ContainerOf(&port, &UartDmaTxTestRig::port_);
    ++rig->submit_count_;
    const LibXR::ErrorCode result = rig->model_.Submit(in_isr);
    if (rig->submit_count_ == 1U)
    {
      rig->start_count_after_first_submit_ = rig->backend_.start_count_;
    }
    return result;
  }

  UartDmaTxTestBackend backend_{};
  LibXR::UartDirectPolicy policy_{};
  LibXR::WritePort port_;
  std::array<uint8_t, 16U> dma_storage_{};
  UartDmaTxTestModel model_;
  size_t submit_count_ = 0U;
  size_t start_count_after_first_submit_ = 0U;
};

struct CompletionObservation
{
  const UartDmaTxTestBackend* backend = nullptr;
  size_t count = 0U;
  LibXR::ErrorCode result = LibXR::ErrorCode::FAILED;
  bool completed_while_disabled = false;
};

void RecordCompletion(bool, CompletionObservation* observation, LibXR::ErrorCode result)
{
  ++observation->count;
  observation->result = result;
  observation->completed_while_disabled |= observation->backend->disabled_;
}

void test_config_already_disabled_defers_tx_completion()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  ASSERT(rig.model_.SetConfig(UartDmaTxTestBackend::TEST_CONFIG, false) == ErrorCode::OK);
  ASSERT(rig.backend_.disabled_);

  OperationPollingStatus status;
  WriteOperation operation(status);
  const std::array<uint8_t, 2U> payload = {0x11U, 0x12U};
  ASSERT(rig.port_(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(status == OperationPollingStatus::RUNNING);
  ASSERT(rig.backend_.start_count_ == 0U);

  rig.backend_.stop_ready_ = true;
  rig.model_.OnStopDone(false);
  ASSERT(!rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(status == OperationPollingStatus::DONE);
}

void test_start_waits_for_publication_release()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  OperationPollingStatus status;
  WriteOperation operation(status);
  const std::array<uint8_t, 2U> payload = {0x31U, 0x32U};

  ASSERT(rig.port_(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(rig.submit_count_ == 2U);
  ASSERT(rig.start_count_after_first_submit_ == 0U);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(status == OperationPollingStatus::DONE);
}

void test_failed_start_completes_once_without_retrying_payload()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  rig.backend_.start_result_ = UartDmaTxStartResult::FAILED;
  CompletionObservation observation{&rig.backend_};
  auto callback = Callback<ErrorCode>::Create(RecordCompletion, &observation);
  WriteOperation operation(callback);
  const std::array<uint8_t, 3U> payload = {0x41U, 0x42U, 0x43U};

  ASSERT(rig.port_(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(rig.start_count_after_first_submit_ == 0U);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(observation.count == 1U);
  ASSERT(observation.result == ErrorCode::FAILED);
  ASSERT(!observation.completed_while_disabled);
  ASSERT(rig.port_.QueueInfo()->Size() == 0U);
  ASSERT(rig.port_.Size() == 0U);

  ASSERT(rig.model_.Submit(false) == ErrorCode::PENDING);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(observation.count == 1U);
}

void test_config_published_during_start_runs_after_completion()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  rig.backend_.inject_config_on_start_ = true;
  CompletionObservation observation{&rig.backend_};
  auto callback = Callback<ErrorCode>::Create(RecordCompletion, &observation);
  WriteOperation operation(callback);
  const std::array<uint8_t, 2U> payload = {0x21U, 0x22U};

  ASSERT(rig.port_(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(observation.count == 1U);
  ASSERT(observation.result == ErrorCode::OK);
  ASSERT(!observation.completed_while_disabled);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(rig.backend_.disabled_);

  rig.backend_.stop_ready_ = true;
  rig.model_.OnStopDone(false);
  ASSERT(!rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 2U);
}

}  // namespace

void test_uart_dma_tx_model()
{
  test_config_already_disabled_defers_tx_completion();
  test_start_waits_for_publication_release();
  test_failed_start_completes_once_without_retrying_payload();
  test_config_published_during_start_runs_after_completion();
}
