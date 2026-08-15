#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "driver/common/fifo_tx_model.hpp"
#include "test.hpp"

namespace
{

static_assert(!std::is_copy_constructible_v<LibXR::FifoTxModel>);
static_assert(!std::is_copy_assignable_v<LibXR::FifoTxModel>);
static_assert(!std::is_move_constructible_v<LibXR::FifoTxModel>);
static_assert(!std::is_move_assignable_v<LibXR::FifoTxModel>);

class FifoTxTestPort : public LibXR::WritePort
{
 public:
  enum class WriterMode : uint8_t
  {
    EXACT,
    PARTIAL,
  };

  FifoTxTestPort() : WritePort(4U, 16U), model_(*this) { WritePort::operator=(WriteFun); }

  static LibXR::ErrorCode WriteFun(LibXR::WritePort& base, bool in_isr)
  {
    auto& port = static_cast<FifoTxTestPort&>(base);
    if (port.model_.HasPendingCompletion())
    {
      ++port.retry_count_;
    }
    if (port.auto_service_)
    {
      port.Drain(in_isr);
    }
    return LibXR::ErrorCode::PENDING;
  }

  LibXR::FifoTxModel::CompletionPublication Publication() const
  {
    return completion_allowed_ ? LibXR::FifoTxModel::CompletionPublication::ALLOW
                               : LibXR::FifoTxModel::CompletionPublication::DEFER;
  }

  void Drain(bool in_isr = false)
  {
    while (true)
    {
      if (model_.HasPendingCompletion())
      {
        if (!model_.PublishPendingCompletion(in_isr, Publication()))
        {
          return;
        }
        continue;
      }

      if (!model_.HasActiveRecord())
      {
        if (!model_.TryClaim(in_isr))
        {
          ++idle_action_count_;
          return;
        }
      }

      if (!Fill(in_isr))
      {
        return;
      }
    }
  }

  bool Fill(bool in_isr)
  {
    if (writer_mode_ == WriterMode::EXACT)
    {
      return model_.FillExact(in_isr, writable_size_, Publication(),
                              [this](const uint8_t* data, size_t size)
                              {
                                ++writer_calls_;
                                Append(data, size);
                              });
    }

    std::array<uint8_t, 8U> scratch{};
    return model_.FillWithScratch(
        in_isr, writable_size_, LibXR::RawData{scratch.data(), scratch.size()},
        Publication(),
        [this](const uint8_t* data, size_t size) -> size_t
        {
          ++writer_calls_;
          const size_t accepted = std::min(size, accepted_size_);
          Append(data, accepted);
          return accepted;
        });
  }

  void Append(const uint8_t* data, size_t size)
  {
    ASSERT(hardware_size_ + size <= hardware_.size());
    for (size_t i = 0U; i < size; ++i)
    {
      hardware_[hardware_size_ + i] = data[i];
    }
    hardware_size_ += size;
  }

  LibXR::FifoTxModel model_;
  WriterMode writer_mode_ = WriterMode::EXACT;
  size_t writable_size_ = 16U;
  size_t accepted_size_ = 16U;
  std::array<uint8_t, 48U> hardware_{};
  size_t hardware_size_ = 0U;
  size_t writer_calls_ = 0U;
  size_t retry_count_ = 0U;
  size_t idle_action_count_ = 0U;
  bool auto_service_ = true;
  bool completion_allowed_ = true;
};

struct CompletionRecord
{
  size_t count = 0U;
  LibXR::ErrorCode result = LibXR::ErrorCode::FAILED;
};

void RecordCompletion(bool, CompletionRecord* record, LibXR::ErrorCode result)
{
  ++record->count;
  record->result = result;
}

struct ReentrantCompletion
{
  FifoTxTestPort* port = nullptr;
  size_t count = 0U;
  size_t idle_count_at_callback = 0U;
  LibXR::ErrorCode nested_result = LibXR::ErrorCode::FAILED;
};

void RecordCompletionAndReenter(bool, ReentrantCompletion* context,
                                LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  ++context->count;
  context->idle_count_at_callback = context->port->idle_action_count_;

  context->port->auto_service_ = false;
  static const uint8_t NESTED[] = {0x71U, 0x72U};
  LibXR::WriteOperation nested_operation;
  context->nested_result =
      (*context->port)(LibXR::ConstRawData{NESTED, sizeof(NESTED)}, nested_operation);
}

void test_exact_record_completes_after_publication_release()
{
  using namespace LibXR;

  FifoTxTestPort port;
  OperationPollingStatus status;
  WriteOperation operation(status);
  const std::array<uint8_t, 4U> payload = {1U, 2U, 3U, 4U};

  ASSERT(port(ConstRawData{payload.data(), payload.size()}, operation) == ErrorCode::OK);
  ASSERT(status == OperationPollingStatus::DONE);
  ASSERT(!port.model_.HasActiveRecord());
  ASSERT(!port.model_.HasPendingCompletion());
  ASSERT(port.QueueInfo()->Size() == 0U);
  ASSERT(port.Size() == 0U);
  ASSERT(port.hardware_size_ == payload.size());
  ASSERT(port.retry_count_ == 1U);
}

void test_partial_and_zero_submit_become_async()
{
  using namespace LibXR;

  {
    FifoTxTestPort port;
    port.writable_size_ = 2U;
    OperationPollingStatus status;
    WriteOperation operation(status);
    const std::array<uint8_t, 5U> payload = {5U, 6U, 7U, 8U, 9U};

    ASSERT(port(ConstRawData{payload.data(), payload.size()}, operation) ==
           ErrorCode::OK);
    ASSERT(status == OperationPollingStatus::RUNNING);
    ASSERT(port.model_.HasActiveRecord());
    ASSERT(port.hardware_size_ == 2U);

    port.writable_size_ = 16U;
    port.Drain();
    ASSERT(status == OperationPollingStatus::DONE);
    ASSERT(port.hardware_size_ == payload.size());
  }

  {
    FifoTxTestPort port;
    port.writable_size_ = 0U;
    OperationPollingStatus status;
    WriteOperation operation(status);
    const std::array<uint8_t, 2U> payload = {0x21U, 0x22U};

    ASSERT(port(ConstRawData{payload.data(), payload.size()}, operation) ==
           ErrorCode::OK);
    ASSERT(status == OperationPollingStatus::RUNNING);
    ASSERT(port.writer_calls_ == 0U);
    ASSERT(port.model_.HasActiveRecord());

    port.writable_size_ = 16U;
    port.Drain();
    ASSERT(status == OperationPollingStatus::DONE);
    ASSERT(port.hardware_size_ == payload.size());
  }
}

void test_terminal_publication_can_be_deferred()
{
  using namespace LibXR;

  FifoTxTestPort port;
  port.completion_allowed_ = false;
  CompletionRecord completion{};
  auto callback = Callback<ErrorCode>::Create(RecordCompletion, &completion);
  WriteOperation operation(callback);
  const std::array<uint8_t, 3U> payload = {0x31U, 0x32U, 0x33U};

  ASSERT(port(ConstRawData{payload.data(), payload.size()}, operation) == ErrorCode::OK);
  ASSERT(completion.count == 0U);
  ASSERT(port.model_.HasPendingCompletion());

  port.completion_allowed_ = true;
  port.Drain();
  ASSERT(completion.count == 1U);
  ASSERT(completion.result == ErrorCode::OK);
  ASSERT(!port.model_.HasPendingCompletion());
}

void test_older_queued_record_completes_asynchronously()
{
  using namespace LibXR;

  FifoTxTestPort port;
  port.auto_service_ = false;
  CompletionRecord first_completion{};
  CompletionRecord second_completion{};
  auto first_callback = Callback<ErrorCode>::Create(RecordCompletion, &first_completion);
  auto second_callback =
      Callback<ErrorCode>::Create(RecordCompletion, &second_completion);
  WriteOperation first_operation(first_callback);
  WriteOperation second_operation(second_callback);
  const std::array<uint8_t, 2U> first = {0x11U, 0x12U};
  const std::array<uint8_t, 2U> second = {0x21U, 0x22U};

  ASSERT(port(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(port.QueueInfo()->Size() == 1U);

  port.auto_service_ = true;
  ASSERT(port(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(first_completion.count == 1U);
  ASSERT(second_completion.count == 1U);
  ASSERT(port.retry_count_ == 1U);
  ASSERT(port.QueueInfo()->Size() == 0U);
  ASSERT(port.hardware_size_ == first.size() + second.size());
}

void test_older_active_record_completes_asynchronously()
{
  using namespace LibXR;

  FifoTxTestPort port;
  port.writable_size_ = 1U;
  CompletionRecord first_completion{};
  CompletionRecord second_completion{};
  auto first_callback = Callback<ErrorCode>::Create(RecordCompletion, &first_completion);
  auto second_callback =
      Callback<ErrorCode>::Create(RecordCompletion, &second_completion);
  WriteOperation first_operation(first_callback);
  WriteOperation second_operation(second_callback);
  const std::array<uint8_t, 3U> first = {0x41U, 0x42U, 0x43U};
  const std::array<uint8_t, 2U> second = {0x51U, 0x52U};

  ASSERT(port(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(port.model_.HasActiveRecord());
  ASSERT(first_completion.count == 0U);

  port.writable_size_ = 16U;
  ASSERT(port(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(first_completion.count == 1U);
  ASSERT(second_completion.count == 1U);
  ASSERT(port.retry_count_ == 1U);
  ASSERT(port.hardware_size_ == first.size() + second.size());
}

void test_partial_writer_dequeues_only_accepted_bytes()
{
  using namespace LibXR;

  FifoTxTestPort port;
  port.writer_mode_ = FifoTxTestPort::WriterMode::PARTIAL;
  port.accepted_size_ = 0U;
  OperationPollingStatus status;
  WriteOperation operation(status);
  const std::array<uint8_t, 5U> payload = {5U, 6U, 7U, 8U, 9U};

  ASSERT(port(ConstRawData{payload.data(), payload.size()}, operation) == ErrorCode::OK);
  ASSERT(status == OperationPollingStatus::RUNNING);
  ASSERT(port.model_.HasActiveRecord());
  ASSERT(port.Size() == payload.size());

  port.accepted_size_ = 2U;
  port.Drain();
  ASSERT(port.hardware_size_ == 2U);
  ASSERT(status == OperationPollingStatus::RUNNING);
  ASSERT(port.Size() == 3U);

  port.accepted_size_ = 8U;
  port.Drain();
  ASSERT(status == OperationPollingStatus::DONE);
  ASSERT(port.Size() == 0U);
  ASSERT(port.hardware_size_ == payload.size());
  for (size_t i = 0U; i < payload.size(); ++i)
  {
    ASSERT(port.hardware_[i] == payload[i]);
  }
}

void test_terminal_slot_is_cleared_before_callback_reentry()
{
  using namespace LibXR;

  FifoTxTestPort port;
  ReentrantCompletion completion{&port};
  auto callback = Callback<ErrorCode>::Create(RecordCompletionAndReenter, &completion);
  WriteOperation operation(callback);
  const std::array<uint8_t, 3U> payload = {0x61U, 0x62U, 0x63U};

  ASSERT(port(ConstRawData{payload.data(), payload.size()}, operation) == ErrorCode::OK);
  ASSERT(completion.count == 1U);
  ASSERT(completion.nested_result == ErrorCode::OK);
  ASSERT(completion.idle_count_at_callback == 0U);
  ASSERT(port.retry_count_ == 1U);
  ASSERT(port.idle_action_count_ == 1U);
  ASSERT(!port.model_.HasActiveRecord());
  ASSERT(!port.model_.HasPendingCompletion());
  ASSERT(port.QueueInfo()->Size() == 0U);
}

}  // namespace

void test_fifo_tx_model()
{
  using namespace LibXR;

  WritePort empty_port(2U, 8U);
  FifoTxModel empty_model(empty_port);
  ASSERT(!empty_model.TryClaim(false));

  test_exact_record_completes_after_publication_release();
  test_partial_and_zero_submit_become_async();
  test_terminal_publication_can_be_deferred();
  test_older_queued_record_completes_asynchronously();
  test_older_active_record_completes_asynchronously();
  test_partial_writer_dequeues_only_accepted_bytes();
  test_terminal_slot_is_cleared_before_callback_reentry();
}
