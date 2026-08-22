#include <algorithm>
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

  LibXR::UartDmaTxStartResult StartDmaTx(uint8_t* data, size_t size, int block,
                                         bool in_isr)
  {
    ASSERT(!disabled_);
    ASSERT(data != nullptr);
    ASSERT(size > 0U && size <= failed_payload_.size());
    ASSERT(start_count_ < started_payloads_.size());
    started_sizes_[start_count_] = size;
    started_blocks_[start_count_] = block;
    for (size_t i = 0U; i < size; ++i)
    {
      started_payloads_[start_count_][i] = data[i];
    }
    ++start_count_;
    if (inject_config_on_start_)
    {
      inject_config_on_start_ = false;
      ASSERT(model_ != nullptr);
      ASSERT(model_->SetConfig(TEST_CONFIG, in_isr) == LibXR::ErrorCode::OK);
    }
    if (start_failures_remaining_ != 0U)
    {
      --start_failures_remaining_;
      if (failed_payload_size_ == 0U)
      {
        failed_payload_size_ = size;
        for (size_t i = 0U; i < size; ++i)
        {
          failed_payload_[i] = data[i];
        }
      }
      if (start_count_ == hold_recovery_after_start_count_)
      {
        recovery_ready_ = false;
      }
      return LibXR::UartDmaTxStartResult::FAILED;
    }
    failed_payload_size_ = 0U;
    if (complete_on_next_start_)
    {
      complete_on_next_start_ = false;
      ASSERT(model_ != nullptr);
      model_->OnTransferDone(in_isr);
    }
    return LibXR::UartDmaTxStartResult::STARTED;
  }

  LibXR::UartDmaControlResult AdvanceConfig(LibXR::UART::Configuration, bool active_tx,
                                            bool)
  {
    ++config_advance_count_;
    last_config_active_tx_ = active_tx;
    disabled_ = true;
    return stop_ready_ ? LibXR::UartDmaControlResult::Completed(config_terminal_)
                       : LibXR::UartDmaControlResult::Pending();
  }

  LibXR::UartDmaControlResult AdvanceRecovery(bool active_tx, bool)
  {
    ++recovery_advance_count_;
    last_recovery_active_tx_ = active_tx;
    disabled_ = true;
    return recovery_ready_ ? LibXR::UartDmaControlResult::Completed(recovery_terminal_)
                           : LibXR::UartDmaControlResult::Pending();
  }

  LibXR::UartDmaControlProgress CompleteConfig(bool)
  {
    ++config_complete_count_;
    disabled_ = false;
    return LibXR::UartDmaControlProgress::COMPLETED;
  }

  LibXR::UartDmaControlProgress CompleteRecovery(bool)
  {
    ++recovery_complete_count_;
    if (!recovery_complete_ready_)
    {
      return LibXR::UartDmaControlProgress::PENDING;
    }
    disabled_ = false;
    return LibXR::UartDmaControlProgress::COMPLETED;
  }

  static constexpr LibXR::UART::Configuration TEST_CONFIG{
      230400U, LibXR::UART::Parity::NO_PARITY, 8U, 1U};

  UartDmaTxTestModel* model_ = nullptr;
  size_t start_count_ = 0U;
  size_t start_failures_remaining_ = 0U;
  size_t config_advance_count_ = 0U;
  size_t recovery_advance_count_ = 0U;
  size_t config_complete_count_ = 0U;
  size_t recovery_complete_count_ = 0U;
  size_t hold_recovery_after_start_count_ = 0U;
  std::array<uint8_t, 8U> failed_payload_{};
  std::array<std::array<uint8_t, 8U>, 16U> started_payloads_{};
  std::array<size_t, 16U> started_sizes_{};
  std::array<int, 16U> started_blocks_{};
  size_t failed_payload_size_ = 0U;
  LibXR::UartOldTxTerminal config_terminal_ = LibXR::UartOldTxTerminal::NONE;
  LibXR::UartOldTxTerminal recovery_terminal_ = LibXR::UartOldTxTerminal::NONE;
  bool inject_config_on_start_ = false;
  bool complete_on_next_start_ = false;
  bool stop_ready_ = false;
  bool recovery_ready_ = true;
  bool recovery_complete_ready_ = true;
  bool last_config_active_tx_ = false;
  bool last_recovery_active_tx_ = false;
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

  static void WriteFun(LibXR::WritePort& port, bool in_isr)
  {
    auto* rig = LibXR::ContainerOf(&port, &UartDmaTxTestRig::port_);
    ++rig->submit_count_;
    rig->model_.Submit(in_isr);
    if (rig->submit_count_ == 1U)
    {
      rig->start_count_after_first_submit_ = rig->backend_.start_count_;
    }
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

struct SubmitOnCompletionContext
{
  UartDmaTxTestRig* rig = nullptr;
  LibXR::ConstRawData next_data{};
  LibXR::WriteOperation* next_operation = nullptr;
  const CompletionObservation* next_observation = nullptr;
  size_t count = 0U;
  LibXR::ErrorCode result = LibXR::ErrorCode::FAILED;
};

void SubmitOnCompletion(bool in_isr, SubmitOnCompletionContext* context,
                        LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  ++context->count;
  ASSERT(context->rig->backend_.start_count_ == 1U);
  ASSERT(context->next_observation->count == 0U);
  context->result =
      context->rig->port_(context->next_data, *context->next_operation, in_isr);
  ASSERT(context->rig->backend_.start_count_ == 1U);
  ASSERT(context->next_observation->count == 0U);
}

struct ConfigOnCompletionContext
{
  UartDmaTxTestRig* rig = nullptr;
  size_t count = 0U;
  LibXR::ErrorCode result = LibXR::ErrorCode::FAILED;
};

void ConfigureOnCompletion(bool in_isr, ConfigOnCompletionContext* context,
                           LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  ++context->count;
  ASSERT(context->rig->backend_.start_count_ == 1U);
  ASSERT(!context->rig->backend_.disabled_);
  context->result =
      context->rig->model_.SetConfig(UartDmaTxTestBackend::TEST_CONFIG, in_isr);
  ASSERT(context->rig->backend_.start_count_ == 1U);
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

void test_released_doorbell_starts_without_a_retry_turn()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  OperationPollingStatus status;
  WriteOperation operation(status);
  const std::array<uint8_t, 2U> payload = {0x31U, 0x32U};

  ASSERT(rig.port_(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(rig.submit_count_ == 1U);
  ASSERT(rig.start_count_after_first_submit_ == 1U);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(status == OperationPollingStatus::DONE);
}

void test_failed_start_discards_slot_without_retry()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  rig.backend_.start_failures_remaining_ = 1U;
  CompletionObservation observation{&rig.backend_};
  auto callback = Callback<ErrorCode>::Create(RecordCompletion, &observation);
  WriteOperation operation(callback);
  const std::array<uint8_t, 3U> payload = {0x41U, 0x42U, 0x43U};

  ASSERT(rig.port_(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(rig.start_count_after_first_submit_ == 1U);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(rig.backend_.recovery_advance_count_ == 1U);
  ASSERT(rig.backend_.recovery_complete_count_ == 1U);
  ASSERT(!rig.backend_.last_recovery_active_tx_);
  ASSERT(!rig.backend_.disabled_);
  ASSERT(rig.backend_.failed_payload_size_ == payload.size());
  ASSERT(
      std::equal(payload.begin(), payload.end(), rig.backend_.failed_payload_.begin()));
  ASSERT(observation.count == 1U);
  ASSERT(observation.result == ErrorCode::OK);
  ASSERT(!observation.completed_while_disabled);
  ASSERT(rig.port_.Size() == 0U);

  rig.model_.Submit(false);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(observation.count == 1U);

  OperationPollingStatus next_status;
  WriteOperation next_operation(next_status);
  const std::array<uint8_t, 2U> next = {0x45U, 0x46U};
  ASSERT(rig.port_(ConstRawData{next.data(), next.size()}, next_operation) ==
         ErrorCode::OK);
  ASSERT(next_status == OperationPollingStatus::DONE);
  ASSERT(rig.backend_.start_count_ == 2U);
  ASSERT(rig.backend_.started_sizes_[1] == next.size());
  ASSERT(std::equal(next.begin(), next.end(), rig.backend_.started_payloads_[1].begin()));
  ASSERT(rig.backend_.started_blocks_[0] != rig.backend_.started_blocks_[1]);
}

void test_write_during_failed_start_recovery_resumes_new_slot()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  rig.backend_.start_failures_remaining_ = 1U;
  rig.backend_.hold_recovery_after_start_count_ = 1U;
  CompletionObservation observation{&rig.backend_};
  auto callback = Callback<ErrorCode>::Create(RecordCompletion, &observation);
  WriteOperation operation(callback);
  const std::array<uint8_t, 3U> payload = {0x81U, 0x82U, 0x83U};

  ASSERT(rig.port_(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(rig.backend_.disabled_);
  ASSERT(rig.backend_.recovery_advance_count_ == 1U);
  ASSERT(observation.count == 1U);
  ASSERT(observation.result == ErrorCode::OK);

  // A real queued WRITE is not a control carrier. It remains queued until the pending
  // recovery receives its hardware carrier.
  const std::array<uint8_t, 1U> queued = {0x84U};
  OperationPollingStatus queued_status;
  WriteOperation queued_operation(queued_status);
  ASSERT(rig.port_(ConstRawData{queued.data(), queued.size()}, queued_operation) ==
         ErrorCode::OK);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(rig.backend_.recovery_advance_count_ == 1U);
  ASSERT(queued_status == OperationPollingStatus::RUNNING);
  ASSERT(rig.port_.Size() == queued.size());

  rig.backend_.recovery_ready_ = true;
  rig.model_.OnStopDone(false);
  ASSERT(!rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 2U);
  ASSERT(rig.backend_.recovery_advance_count_ == 2U);
  ASSERT(rig.backend_.failed_payload_size_ == 0U);
  ASSERT(observation.count == 1U);
  ASSERT(observation.result == ErrorCode::OK);
  ASSERT(queued_status == OperationPollingStatus::DONE);
  ASSERT(rig.port_.Size() == 0U);
  ASSERT(rig.backend_.started_sizes_[1] == queued.size());
  ASSERT(std::equal(queued.begin(), queued.end(),
                    rig.backend_.started_payloads_[1].begin()));
}

void test_failed_ready_start_resumes_preexisting_suffix_after_sync_recovery()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  const std::array<uint8_t, 2U> first = {0x85U, 0x86U};
  const std::array<uint8_t, 3U> second = {0x87U, 0x88U, 0x89U};
  const std::array<uint8_t, 1U> third = {0x8AU};
  OperationPollingStatus first_status;
  OperationPollingStatus second_status;
  OperationPollingStatus third_status;
  WriteOperation first_operation(first_status);
  WriteOperation second_operation(second_status);
  WriteOperation third_operation(third_status);

  ASSERT(rig.port_(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(rig.port_(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(rig.port_(ConstRawData{third.data(), third.size()}, third_operation) ==
         ErrorCode::OK);
  ASSERT(first_status == OperationPollingStatus::DONE);
  ASSERT(second_status == OperationPollingStatus::DONE);
  ASSERT(third_status == OperationPollingStatus::RUNNING);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(rig.port_.Size() == third.size());

  rig.backend_.start_failures_remaining_ = 1U;
  rig.model_.OnTransferDone(false);

  ASSERT(rig.backend_.recovery_advance_count_ == 1U);
  ASSERT(rig.backend_.recovery_complete_count_ == 1U);
  ASSERT(!rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 3U);
  ASSERT(third_status == OperationPollingStatus::DONE);
  ASSERT(rig.port_.Size() == 0U);
  ASSERT(
      std::equal(first.begin(), first.end(), rig.backend_.started_payloads_[0].begin()));
  ASSERT(std::equal(second.begin(), second.end(),
                    rig.backend_.started_payloads_[1].begin()));
  ASSERT(
      std::equal(third.begin(), third.end(), rig.backend_.started_payloads_[2].begin()));

  rig.model_.OnTransferDone(false);
  ASSERT(rig.backend_.start_count_ == 3U);
}

void test_failed_ready_start_resumes_preexisting_suffix_after_pending_recovery()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  const std::array<uint8_t, 2U> first = {0x8BU, 0x8CU};
  const std::array<uint8_t, 3U> second = {0x8DU, 0x8EU, 0x8FU};
  const std::array<uint8_t, 1U> third = {0x90U};
  OperationPollingStatus first_status;
  OperationPollingStatus second_status;
  OperationPollingStatus third_status;
  WriteOperation first_operation(first_status);
  WriteOperation second_operation(second_status);
  WriteOperation third_operation(third_status);

  ASSERT(rig.port_(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(rig.port_(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(rig.port_(ConstRawData{third.data(), third.size()}, third_operation) ==
         ErrorCode::OK);
  ASSERT(first_status == OperationPollingStatus::DONE);
  ASSERT(second_status == OperationPollingStatus::DONE);
  ASSERT(third_status == OperationPollingStatus::RUNNING);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(rig.port_.Size() == third.size());

  rig.backend_.start_failures_remaining_ = 1U;
  rig.backend_.hold_recovery_after_start_count_ = 2U;
  rig.model_.OnTransferDone(false);

  ASSERT(rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 2U);
  ASSERT(rig.backend_.recovery_advance_count_ == 1U);
  ASSERT(rig.backend_.recovery_complete_count_ == 0U);
  ASSERT(third_status == OperationPollingStatus::RUNNING);
  ASSERT(rig.port_.Size() == third.size());

  rig.backend_.recovery_ready_ = true;
  rig.model_.OnStopDone(false);

  ASSERT(!rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 3U);
  ASSERT(rig.backend_.recovery_advance_count_ == 2U);
  ASSERT(rig.backend_.recovery_complete_count_ == 1U);
  ASSERT(third_status == OperationPollingStatus::DONE);
  ASSERT(rig.port_.Size() == 0U);
  ASSERT(
      std::equal(first.begin(), first.end(), rig.backend_.started_payloads_[0].begin()));
  ASSERT(std::equal(second.begin(), second.end(),
                    rig.backend_.started_payloads_[1].begin()));
  ASSERT(
      std::equal(third.begin(), third.end(), rig.backend_.started_payloads_[2].begin()));

  rig.model_.OnTransferDone(false);
  ASSERT(rig.backend_.start_count_ == 3U);
}

void test_pending_recovery_completion_resumes_ready_and_preexisting_suffix()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  const std::array<uint8_t, 2U> first = {0x91U, 0x92U};
  const std::array<uint8_t, 3U> second = {0x93U, 0x94U, 0x95U};
  const std::array<uint8_t, 1U> third = {0x96U};
  OperationPollingStatus first_status;
  OperationPollingStatus second_status;
  OperationPollingStatus third_status;
  WriteOperation first_operation(first_status);
  WriteOperation second_operation(second_status);
  WriteOperation third_operation(third_status);

  ASSERT(rig.port_(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(rig.port_(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(rig.port_(ConstRawData{third.data(), third.size()}, third_operation) ==
         ErrorCode::OK);
  ASSERT(first_status == OperationPollingStatus::DONE);
  ASSERT(second_status == OperationPollingStatus::DONE);
  ASSERT(third_status == OperationPollingStatus::RUNNING);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(rig.port_.Size() == third.size());

  rig.backend_.recovery_complete_ready_ = false;
  rig.model_.OnTransferError(false);

  ASSERT(rig.backend_.last_recovery_active_tx_);
  ASSERT(rig.backend_.recovery_advance_count_ == 1U);
  ASSERT(rig.backend_.recovery_complete_count_ == 1U);
  ASSERT(rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(third_status == OperationPollingStatus::RUNNING);
  ASSERT(rig.port_.Size() == third.size());

  rig.model_.OnStopDone(false);
  ASSERT(rig.backend_.recovery_complete_count_ == 2U);
  ASSERT(rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(third_status == OperationPollingStatus::RUNNING);
  ASSERT(rig.port_.Size() == third.size());

  rig.backend_.recovery_complete_ready_ = true;
  rig.model_.OnStopDone(false);

  ASSERT(rig.backend_.recovery_complete_count_ == 3U);
  ASSERT(!rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 2U);
  ASSERT(third_status == OperationPollingStatus::DONE);
  ASSERT(rig.port_.Size() == 0U);
  ASSERT(
      std::equal(first.begin(), first.end(), rig.backend_.started_payloads_[0].begin()));
  ASSERT(std::equal(second.begin(), second.end(),
                    rig.backend_.started_payloads_[1].begin()));

  rig.model_.OnTransferDone(false);
  ASSERT(rig.backend_.start_count_ == 3U);
  ASSERT(
      std::equal(third.begin(), third.end(), rig.backend_.started_payloads_[2].begin()));

  rig.model_.OnTransferDone(false);
  ASSERT(rig.backend_.start_count_ == 3U);
}

void test_one_scope_accepts_only_active_plus_ready()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  ASSERT(rig.model_.SetConfig(UartDmaTxTestBackend::TEST_CONFIG, false) == ErrorCode::OK);
  ASSERT(rig.backend_.disabled_);

  const std::array<uint8_t, 2U> first = {0x71U, 0x72U};
  const std::array<uint8_t, 3U> second = {0x73U, 0x74U, 0x75U};
  const std::array<uint8_t, 1U> third = {0x76U};
  OperationPollingStatus first_status;
  OperationPollingStatus second_status;
  OperationPollingStatus third_status;
  WriteOperation first_operation(first_status);
  WriteOperation second_operation(second_status);
  WriteOperation third_operation(third_status);

  ASSERT(rig.port_(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(rig.port_(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(rig.port_(ConstRawData{third.data(), third.size()}, third_operation) ==
         ErrorCode::OK);

  rig.backend_.stop_ready_ = true;
  rig.model_.OnStopDone(false);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(first_status == OperationPollingStatus::DONE);
  ASSERT(second_status == OperationPollingStatus::DONE);
  ASSERT(third_status == OperationPollingStatus::RUNNING);
  ASSERT(rig.port_.Size() == third.size());

  rig.model_.OnTransferDone(false);
  ASSERT(rig.backend_.start_count_ == 2U);
  ASSERT(third_status == OperationPollingStatus::DONE);
  ASSERT(rig.port_.Size() == 0U);

  rig.model_.OnTransferDone(false);
  ASSERT(rig.backend_.start_count_ == 3U);
  ASSERT(rig.backend_.started_sizes_[0] == first.size());
  ASSERT(rig.backend_.started_sizes_[1] == second.size());
  ASSERT(rig.backend_.started_sizes_[2] == third.size());
}

void test_config_published_during_start_does_not_replay_active()
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
  ASSERT(rig.backend_.start_count_ == 1U);
}

void test_active_transfer_prefetches_one_persistent_ready_record()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  const std::array<uint8_t, 2U> first = {0x51U, 0x52U};
  WriteOperation first_operation;
  ASSERT(rig.port_(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(rig.backend_.start_count_ == 1U);

  OperationPollingStatus second_status;
  WriteOperation second_operation(second_status);
  const std::array<uint8_t, 3U> second = {0x61U, 0x62U, 0x63U};
  ASSERT(rig.port_(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(second_status == OperationPollingStatus::DONE);
  ASSERT(rig.backend_.start_count_ == 1U);

  rig.model_.OnTransferDone(false);
  ASSERT(rig.backend_.start_count_ == 2U);
  ASSERT(rig.backend_.started_sizes_[0] == first.size());
  ASSERT(rig.backend_.started_sizes_[1] == second.size());
  ASSERT(
      std::equal(first.begin(), first.end(), rig.backend_.started_payloads_[0].begin()));
  ASSERT(std::equal(second.begin(), second.end(),
                    rig.backend_.started_payloads_[1].begin()));
  ASSERT(rig.backend_.started_blocks_[0] != rig.backend_.started_blocks_[1]);
}

void test_config_complete_terminal_retires_active_and_preserves_ready()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  rig.backend_.stop_ready_ = true;
  rig.backend_.config_terminal_ = UartOldTxTerminal::COMPLETE;

  CompletionObservation first_observation{&rig.backend_};
  auto first_callback = Callback<ErrorCode>::Create(RecordCompletion, &first_observation);
  WriteOperation first_operation(first_callback);
  const std::array<uint8_t, 2U> first = {0xC1U, 0xC2U};
  ASSERT(rig.port_(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);

  CompletionObservation second_observation{&rig.backend_};
  auto second_callback =
      Callback<ErrorCode>::Create(RecordCompletion, &second_observation);
  WriteOperation second_operation(second_callback);
  const std::array<uint8_t, 3U> second = {0xC3U, 0xC4U, 0xC5U};
  ASSERT(rig.port_(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(first_observation.count == 1U);
  ASSERT(second_observation.count == 1U);

  ASSERT(rig.model_.SetConfig(UartDmaTxTestBackend::TEST_CONFIG, false) == ErrorCode::OK);
  ASSERT(rig.backend_.last_config_active_tx_);
  ASSERT(rig.backend_.config_advance_count_ == 1U);
  ASSERT(rig.backend_.config_complete_count_ == 1U);
  ASSERT(!rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 2U);
  ASSERT(rig.backend_.started_sizes_[0] == first.size());
  ASSERT(rig.backend_.started_sizes_[1] == second.size());
  ASSERT(
      std::equal(first.begin(), first.end(), rig.backend_.started_payloads_[0].begin()));
  ASSERT(std::equal(second.begin(), second.end(),
                    rig.backend_.started_payloads_[1].begin()));
  ASSERT(rig.backend_.started_blocks_[0] != rig.backend_.started_blocks_[1]);
  ASSERT(first_observation.count == 1U);
  ASSERT(second_observation.count == 1U);
}

void test_recovery_error_terminal_discards_active_and_preserves_ready()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  rig.backend_.recovery_terminal_ = UartOldTxTerminal::ERROR;

  CompletionObservation first_observation{&rig.backend_};
  auto first_callback = Callback<ErrorCode>::Create(RecordCompletion, &first_observation);
  WriteOperation first_operation(first_callback);
  const std::array<uint8_t, 2U> first = {0xD1U, 0xD2U};
  ASSERT(rig.port_(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);

  CompletionObservation second_observation{&rig.backend_};
  auto second_callback =
      Callback<ErrorCode>::Create(RecordCompletion, &second_observation);
  WriteOperation second_operation(second_callback);
  const std::array<uint8_t, 3U> second = {0xD3U, 0xD4U, 0xD5U};
  ASSERT(rig.port_(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(rig.backend_.start_count_ == 1U);

  rig.model_.OnTransferError(false);
  ASSERT(rig.backend_.last_recovery_active_tx_);
  ASSERT(rig.backend_.recovery_advance_count_ == 1U);
  ASSERT(rig.backend_.recovery_complete_count_ == 1U);
  ASSERT(!rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 2U);
  ASSERT(rig.backend_.started_sizes_[0] == first.size());
  ASSERT(rig.backend_.started_sizes_[1] == second.size());
  ASSERT(
      std::equal(first.begin(), first.end(), rig.backend_.started_payloads_[0].begin()));
  ASSERT(std::equal(second.begin(), second.end(),
                    rig.backend_.started_payloads_[1].begin()));
  ASSERT(rig.backend_.started_blocks_[0] != rig.backend_.started_blocks_[1]);
  ASSERT(first_observation.count == 1U);
  ASSERT(second_observation.count == 1U);
}

void test_write_queue_completion_callback_submits_next_write_without_reentry()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  CompletionObservation second_observation{&rig.backend_};
  auto second_callback =
      Callback<ErrorCode>::Create(RecordCompletion, &second_observation);
  WriteOperation second_operation(second_callback);
  const std::array<uint8_t, 3U> second = {0xE3U, 0xE4U, 0xE5U};

  SubmitOnCompletionContext context{&rig, ConstRawData{second.data(), second.size()},
                                    &second_operation, &second_observation};
  auto first_callback = Callback<ErrorCode>::Create(SubmitOnCompletion, &context);
  WriteOperation first_operation(first_callback);
  const std::array<uint8_t, 2U> first = {0xE1U, 0xE2U};

  ASSERT(rig.port_(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(context.count == 1U);
  ASSERT(context.result == ErrorCode::OK);
  ASSERT(second_observation.count == 1U);
  ASSERT(second_observation.result == ErrorCode::OK);
  ASSERT(rig.submit_count_ == 2U);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(rig.port_.Size() == 0U);
  ASSERT(rig.backend_.started_sizes_[0] == first.size());
  ASSERT(
      std::equal(first.begin(), first.end(), rig.backend_.started_payloads_[0].begin()));

  rig.model_.OnTransferDone(false);
  ASSERT(rig.backend_.start_count_ == 2U);
  ASSERT(rig.backend_.started_sizes_[1] == second.size());
  ASSERT(std::equal(second.begin(), second.end(),
                    rig.backend_.started_payloads_[1].begin()));
  ASSERT(rig.backend_.started_blocks_[0] != rig.backend_.started_blocks_[1]);
  ASSERT(context.count == 1U);
  ASSERT(second_observation.count == 1U);

  rig.model_.OnTransferDone(false);
  ASSERT(rig.backend_.start_count_ == 2U);
}

void test_write_queue_completion_callback_stops_active_without_replay()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  ConfigOnCompletionContext context{&rig};
  auto callback = Callback<ErrorCode>::Create(ConfigureOnCompletion, &context);
  WriteOperation operation(callback);
  const std::array<uint8_t, 2U> payload = {0xF1U, 0xF2U};

  ASSERT(rig.port_(ConstRawData{payload.data(), payload.size()}, operation) ==
         ErrorCode::OK);
  ASSERT(context.count == 1U);
  ASSERT(context.result == ErrorCode::OK);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(rig.backend_.config_advance_count_ == 1U);
  ASSERT(rig.backend_.config_complete_count_ == 0U);
  ASSERT(rig.backend_.last_config_active_tx_);
  ASSERT(rig.backend_.disabled_);
  ASSERT(rig.port_.Size() == 0U);

  rig.backend_.stop_ready_ = true;
  rig.model_.OnStopDone(false);
  ASSERT(rig.backend_.config_advance_count_ == 2U);
  ASSERT(rig.backend_.config_complete_count_ == 1U);
  ASSERT(!rig.backend_.disabled_);
  ASSERT(rig.backend_.start_count_ == 1U);
  ASSERT(rig.backend_.started_sizes_[0] == payload.size());
  ASSERT(std::equal(payload.begin(), payload.end(),
                    rig.backend_.started_payloads_[0].begin()));
  ASSERT(context.count == 1U);
}

void test_synchronous_complete_observes_published_active()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  rig.backend_.complete_on_next_start_ = true;
  const std::array<uint8_t, 2U> first = {0x91U, 0x92U};
  WriteOperation first_operation;
  ASSERT(rig.port_(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(rig.backend_.start_count_ == 1U);

  const std::array<uint8_t, 2U> second = {0x93U, 0x94U};
  WriteOperation second_operation;
  ASSERT(rig.port_(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(rig.backend_.start_count_ == 2U);
}

void test_runtime_error_discards_active_and_preserves_ready()
{
  using namespace LibXR;

  UartDmaTxTestRig rig;
  const std::array<uint8_t, 2U> first = {0xA1U, 0xA2U};
  WriteOperation first_operation;
  ASSERT(rig.port_(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);

  const std::array<uint8_t, 3U> second = {0xB1U, 0xB2U, 0xB3U};
  WriteOperation second_operation;
  ASSERT(rig.port_(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(rig.backend_.start_count_ == 1U);

  rig.model_.OnTransferError(false);
  ASSERT(rig.backend_.recovery_advance_count_ == 1U);
  ASSERT(rig.backend_.start_count_ == 2U);
  ASSERT(rig.backend_.started_sizes_[0] == first.size());
  ASSERT(rig.backend_.started_sizes_[1] == second.size());
  ASSERT(
      std::equal(first.begin(), first.end(), rig.backend_.started_payloads_[0].begin()));
  ASSERT(std::equal(second.begin(), second.end(),
                    rig.backend_.started_payloads_[1].begin()));
}

void test_disabled_port_accepts_empty_dma_storage()
{
  UartDmaTxTestBackend backend;
  LibXR::UartDirectPolicy policy;
  LibXR::WritePort port(1U, 0U);
  UartDmaTxTestModel model(backend, policy, port, LibXR::RawData{nullptr, 0U});
  (void)model;
  ASSERT(port.Capacity() == 0U);
}

}  // namespace

void test_uart_dma_tx_model()
{
  test_config_already_disabled_defers_tx_completion();
  test_released_doorbell_starts_without_a_retry_turn();
  test_failed_start_discards_slot_without_retry();
  test_write_during_failed_start_recovery_resumes_new_slot();
  test_failed_ready_start_resumes_preexisting_suffix_after_sync_recovery();
  test_failed_ready_start_resumes_preexisting_suffix_after_pending_recovery();
  test_pending_recovery_completion_resumes_ready_and_preexisting_suffix();
  test_one_scope_accepts_only_active_plus_ready();
  test_config_published_during_start_does_not_replay_active();
  test_active_transfer_prefetches_one_persistent_ready_record();
  test_config_complete_terminal_retires_active_and_preserves_ready();
  test_recovery_error_terminal_discards_active_and_preserves_ready();
  test_write_queue_completion_callback_submits_next_write_without_reentry();
  test_write_queue_completion_callback_stops_active_without_replay();
  test_synchronous_complete_observes_published_active();
  test_runtime_error_discards_active_and_preserves_ready();
  test_disabled_port_accepts_empty_dma_storage();
}
