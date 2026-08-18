/**
 * @file test_rw_publication.cpp
 * @brief WritePort publication and completion-ordering scenarios.
 */
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "rw_test_common.hpp"
#include "serialized_service.hpp"

namespace
{

class PublicationPort : public LibXR::WritePort
{
 public:
  PublicationPort() : WritePort(4U, 16U) { WritePort::operator=(HandleWrite); }

  void AcceptWrites(bool in_isr = false)
  {
    (void)service_.Invoke(
        1U,
        [this, in_isr](uint32_t) noexcept
        {
          if (!auto_accept_)
          {
            return;
          }

          while (true)
          {
            size_t expected = 0U;
            size_t accepted = 0U;
            {
              auto queue = GetWriteQueue(in_isr);
              if (queue.front_size == 0U)
              {
                return;
              }

              expected = queue.front_size + (accept_next_ ? queue.next_size : 0U);
              accepted = queue.PopWithWriter(
                  expected,
                  [this](const uint8_t* first, size_t first_size, const uint8_t* second,
                         size_t second_size)
                  {
                    writer_calls_++;
                    accepted_.insert(accepted_.end(), first, first + first_size);
                    if (second_size != 0U)
                    {
                      accepted_.insert(accepted_.end(), second, second + second_size);
                    }
                    return first_size + second_size;
                  });
              REQUIRE(accepted == expected);
            }

            if (accepted == 0U)
            {
              return;
            }

            if (reuse_stream_ != nullptr)
            {
              REQUIRE(reuse_status_ != nullptr);
              REQUIRE(reuse_status_->Load() == LibXR::OperationPollingStatus::DONE);
              LibXR::WritePort::Stream* stream = reuse_stream_;
              reuse_stream_ = nullptr;
              static const uint8_t REUSED[] = {0x71U, 0x72U};
              reuse_write_result_ =
                  stream->Write(LibXR::ConstRawData{REUSED, sizeof(REUSED)});
              reuse_commit_result_ = stream->Commit();
            }
          }
        });
  }

  bool auto_accept_ = true;
  bool accept_next_ = false;
  size_t writer_calls_ = 0U;
  std::vector<uint8_t> accepted_;
  LibXR::WritePort::Stream* reuse_stream_ = nullptr;
  LibXR::OperationPollingStatus* reuse_status_ = nullptr;
  LibXR::ErrorCode reuse_write_result_ = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode reuse_commit_result_ = LibXR::ErrorCode::FAILED;

 private:
  static void HandleWrite(LibXR::WritePort& base, bool in_isr)
  {
    static_cast<PublicationPort&>(base).AcceptWrites(in_isr);
  }

  LibXR::SerializedService service_;
};

struct ReentryContext
{
  PublicationPort* port = nullptr;
  LibXR::ErrorCode nested_result = LibXR::ErrorCode::FAILED;
};

struct StreamReentryContext
{
  LibXR::WritePort::Stream* stream = nullptr;
  uint32_t callback_count = 0U;
  LibXR::ErrorCode write_result = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode commit_result = LibXR::ErrorCode::FAILED;
};

struct StreamPublicationPause
{
  std::atomic<bool> entered{false};
  std::atomic<bool> release{false};
};

std::atomic<StreamPublicationPause*> stream_publication_pause{nullptr};

struct OrderedCompletionContext
{
  std::vector<uint8_t>* order = nullptr;
  uint8_t id = 0U;
  LibXR::OperationPollingStatus* preceding_status = nullptr;
};

void RecordOrderedCompletion(bool, OrderedCompletionContext* context,
                             LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  if (context->preceding_status != nullptr)
  {
    ASSERT(context->preceding_status->Load() == LibXR::OperationPollingStatus::DONE);
  }
  context->order->push_back(context->id);
}

void CompleteAndReenter(bool, ReentryContext* context, LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  static const uint8_t NESTED[] = {0x61U};
  LibXR::WriteOperation nested_operation;
  context->nested_result =
      (*context->port)(LibXR::ConstRawData{NESTED, sizeof(NESTED)}, nested_operation);
}

void CompleteAndReenterStream(bool, StreamReentryContext* context,
                              LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  ASSERT(context->stream != nullptr);
  static const uint8_t NESTED[] = {0x91U};
  context->callback_count++;
  context->write_result = context->stream->Write(LibXR::ConstRawData{NESTED, sizeof(NESTED)});
  context->commit_result = context->stream->Commit();
}

void PauseStreamPublicationForTest()
{
  StreamPublicationPause* pause = stream_publication_pause.load(std::memory_order_acquire);
  ASSERT(pause != nullptr);
  pause->entered.store(true, std::memory_order_release);
  while (!pause->release.load(std::memory_order_acquire))
  {
    std::this_thread::yield();
  }
}

struct ResultContext
{
  uint32_t count = 0U;
  LibXR::ErrorCode result = LibXR::ErrorCode::OK;
};

struct FrontBeforeBlockContext
{
  LibXR::Semaphore* block_semaphore = nullptr;
  std::array<uint8_t, 2U>* order = nullptr;
  size_t* order_count = nullptr;
  uint32_t callback_count = 0U;
};

void RecordResult(bool, ResultContext* context, LibXR::ErrorCode result)
{
  context->count++;
  context->result = result;
}

void RecordFrontBeforeBlock(bool, FrontBeforeBlockContext* context,
                            LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  ASSERT(context->block_semaphore->Value() == 0U);
  ASSERT(*context->order_count < context->order->size());
  (*context->order)[(*context->order_count)++] = 1U;
  context->callback_count++;
}

void test_inline_accept_allows_callback_reentry()
{
  using namespace LibXR;

  PublicationPort port;
  ReentryContext context{&port};
  auto callback = Callback<ErrorCode>::Create(CompleteAndReenter, &context);
  WriteOperation operation(callback);
  static const uint8_t OUTER[] = {0x51U, 0x52U};
  static const uint8_t EXPECTED[] = {0x51U, 0x52U, 0x61U};

  ASSERT(port(ConstRawData{OUTER, sizeof(OUTER)}, operation) == ErrorCode::OK);
  ASSERT(context.nested_result == ErrorCode::OK);
  ASSERT(port.accepted_.size() == sizeof(EXPECTED));
  ASSERT(std::memcmp(port.accepted_.data(), EXPECTED, sizeof(EXPECTED)) == 0);
  ASSERT(port.Size() == 0U);
}

void test_delayed_accept_completes_exact_polling_operation()
{
  using namespace LibXR;

  PublicationPort port;
  port.auto_accept_ = false;
  OperationPollingStatus status;
  WriteOperation operation(status);
  static const uint8_t PAYLOAD[] = {0x21U, 0x22U};

  ASSERT(port(ConstRawData{PAYLOAD, sizeof(PAYLOAD)}, operation) == ErrorCode::OK);
  ASSERT(status == OperationPollingStatus::RUNNING);
  ASSERT(port.accepted_.empty());
  ASSERT(port.Size() == sizeof(PAYLOAD));

  port.auto_accept_ = true;
  port.AcceptWrites();

  ASSERT(status == OperationPollingStatus::DONE);
  ASSERT(port.accepted_.size() == sizeof(PAYLOAD));
  ASSERT(std::memcmp(port.accepted_.data(), PAYLOAD, sizeof(PAYLOAD)) == 0);
  ASSERT(port.Size() == 0U);
}

void test_old_completion_during_stream_admission_rejects_nested_write()
{
  using namespace LibXR;

  PublicationPort port;
  port.auto_accept_ = false;
  ReentryContext context{&port};
  auto callback = Callback<ErrorCode>::Create(CompleteAndReenter, &context);
  WriteOperation first_operation(callback);
  static const uint8_t FIRST[] = {0x31U};
  ASSERT(port(ConstRawData{FIRST, sizeof(FIRST)}, first_operation) == ErrorCode::OK);

  WriteOperation stream_operation;
  WritePort::Stream stream(&port, stream_operation);
  static const uint8_t SECOND[] = {0x41U, 0x42U};
  ASSERT(stream.Write(ConstRawData{SECOND, sizeof(SECOND)}) == ErrorCode::OK);

  port.auto_accept_ = true;
  port.AcceptWrites();
  ASSERT(context.nested_result == ErrorCode::BUSY);
  ASSERT(port.accepted_.size() == sizeof(FIRST));
  ASSERT(port.accepted_[0] == FIRST[0]);

  ASSERT(stream.Commit() == ErrorCode::OK);
  ASSERT(port.accepted_.size() == sizeof(FIRST) + sizeof(SECOND));
  ASSERT(std::memcmp(port.accepted_.data() + sizeof(FIRST), SECOND, sizeof(SECOND)) == 0);
  ASSERT(port.Size() == 0U);
}

void test_old_completion_inside_stream_publication_rejects_stream_reentry()
{
  using namespace LibXR;

  PublicationPort port;
  port.auto_accept_ = false;
  StreamReentryContext context;
  auto callback = Callback<ErrorCode>::Create(CompleteAndReenterStream, &context);
  WriteOperation first_operation(callback);
  static const uint8_t FIRST[] = {0x31U};
  static const uint8_t SECOND[] = {0x41U, 0x42U};
  static const uint8_t EXPECTED[] = {0x31U, 0x41U, 0x42U};

  ASSERT(port(ConstRawData{FIRST, sizeof(FIRST)}, first_operation) == ErrorCode::OK);

  WriteOperation stream_operation;
  WritePort::Stream stream(&port, stream_operation);
  context.stream = &stream;
  ASSERT(stream.Write(ConstRawData{SECOND, sizeof(SECOND)}) == ErrorCode::OK);

  StreamPublicationPause pause;
  stream_publication_pause.store(&pause, std::memory_order_release);
  WritePortTestAccess::SetStreamPublicationHook(PauseStreamPublicationForTest);

  ErrorCode commit_result = ErrorCode::FAILED;
  std::thread committer([&]() { commit_result = stream.Commit(); });

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!pause.entered.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline)
  {
    std::this_thread::yield();
  }

  if (!pause.entered.load(std::memory_order_acquire))
  {
    pause.release.store(true, std::memory_order_release);
    committer.join();
    WritePortTestAccess::SetStreamPublicationHook(nullptr);
    stream_publication_pause.store(nullptr, std::memory_order_release);
    ASSERT(false);
  }

  std::array<uint8_t, sizeof(FIRST)> accepted_first{};
  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == sizeof(FIRST));
    ASSERT(queue.PopBatch(accepted_first.data(), accepted_first.size()) ==
           accepted_first.size());
  }
  port.accepted_.insert(port.accepted_.end(), accepted_first.begin(), accepted_first.end());
  ASSERT(context.callback_count == 1U);
  ASSERT(context.write_result == ErrorCode::BUSY);
  ASSERT(context.commit_result == ErrorCode::BUSY);

  pause.release.store(true, std::memory_order_release);
  committer.join();
  WritePortTestAccess::SetStreamPublicationHook(nullptr);
  stream_publication_pause.store(nullptr, std::memory_order_release);

  ASSERT(commit_result == ErrorCode::OK);
  port.auto_accept_ = true;
  port.AcceptWrites();
  ASSERT(port.accepted_.size() == sizeof(EXPECTED));
  ASSERT(std::memcmp(port.accepted_.data(), EXPECTED, sizeof(EXPECTED)) == 0);
  ASSERT(port.Size() == 0U);
}

void test_block_completion_can_precede_first_wait()
{
  using namespace LibXR;

  PublicationPort port;
  Semaphore semaphore;
  WriteOperation operation(semaphore, 100U);
  static const uint8_t PAYLOAD[] = {0x71U, 0x72U};

  ASSERT(port(ConstRawData{PAYLOAD, sizeof(PAYLOAD)}, operation) == ErrorCode::OK);
  ASSERT(semaphore.Value() == 0U);
  ASSERT(port.accepted_.size() == sizeof(PAYLOAD));
  ASSERT(std::memcmp(port.accepted_.data(), PAYLOAD, sizeof(PAYLOAD)) == 0);
  ASSERT(port.Size() == 0U);
}

void test_polling_stream_can_be_reused_after_inline_dequeue()
{
  using namespace LibXR;

  PublicationPort port;
  OperationPollingStatus status;
  WriteOperation operation(status);
  WritePort::Stream stream(&port, operation);
  port.reuse_stream_ = &stream;
  port.reuse_status_ = &status;
  static const uint8_t OUTER[] = {0x61U};
  static const uint8_t EXPECTED[] = {0x61U, 0x71U, 0x72U};

  ASSERT(stream.Write(ConstRawData{OUTER, sizeof(OUTER)}) == ErrorCode::OK);
  ASSERT(stream.Commit() == ErrorCode::OK);

  ASSERT(port.reuse_write_result_ == ErrorCode::OK);
  ASSERT(port.reuse_commit_result_ == ErrorCode::OK);
  ASSERT(status.Load() == OperationPollingStatus::DONE);
  ASSERT(port.accepted_.size() == sizeof(EXPECTED));
  ASSERT(std::memcmp(port.accepted_.data(), EXPECTED, sizeof(EXPECTED)) == 0);
  ASSERT(port.Size() == 0U);
}

void test_write_queue_crosses_request_boundaries_and_preserves_completion_order()
{
  using namespace LibXR;

  PublicationPort port;
  port.auto_accept_ = false;

  std::vector<uint8_t> completion_order;
  OrderedCompletionContext first_context{&completion_order, 1U};
  auto first_callback =
      Callback<ErrorCode>::Create(RecordOrderedCompletion, &first_context);
  WriteOperation first_operation(first_callback);

  OperationPollingStatus second_status;
  WriteOperation second_operation(second_status);

  OrderedCompletionContext third_context{&completion_order, 3U, &second_status};
  auto third_callback =
      Callback<ErrorCode>::Create(RecordOrderedCompletion, &third_context);
  WriteOperation third_operation(third_callback);

  const std::array<uint8_t, 2U> first = {0x11U, 0x12U};
  const std::array<uint8_t, 3U> second = {0x21U, 0x22U, 0x23U};
  const std::array<uint8_t, 2U> third = {0x31U, 0x32U};
  const std::array<uint8_t, 7U> expected = {0x11U, 0x12U, 0x21U, 0x22U,
                                            0x23U, 0x31U, 0x32U};

  ASSERT(port(ConstRawData{first.data(), first.size()}, first_operation) ==
         ErrorCode::OK);
  ASSERT(port(ConstRawData{second.data(), second.size()}, second_operation) ==
         ErrorCode::OK);
  ASSERT(port(ConstRawData{third.data(), third.size()}, third_operation) ==
         ErrorCode::OK);

  size_t reader_calls = 0U;
  auto reader = [&port, &reader_calls](const uint8_t* first_span, size_t first_size,
                                       const uint8_t* second_span,
                                       size_t second_size) -> size_t
  {
    ++reader_calls;
    port.accepted_.insert(port.accepted_.end(), first_span, first_span + first_size);
    if (second_size != 0U)
    {
      port.accepted_.insert(port.accepted_.end(), second_span, second_span + second_size);
    }
    return first_size + second_size;
  };

  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == first.size());
    ASSERT(queue.next_size == second.size());
    ASSERT(queue.PopWithWriter(first.size() + 1U, reader) == first.size() + 1U);
    ASSERT(!queue.FailFront(ErrorCode::INIT_ERR));
    ASSERT(completion_order.empty());
    ASSERT(second_status == OperationPollingStatus::RUNNING);
  }
  ASSERT(reader_calls == 1U);
  ASSERT(completion_order == std::vector<uint8_t>{1U});
  ASSERT(second_status == OperationPollingStatus::RUNNING);

  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == second.size() - 1U);
    ASSERT(queue.next_size == third.size());
    ASSERT(queue.PopWithWriter(expected.size(), reader) ==
           expected.size() - first.size() - 1U);
    ASSERT(completion_order == std::vector<uint8_t>{1U});
    ASSERT(second_status == OperationPollingStatus::RUNNING);
  }

  ASSERT(reader_calls == 2U);
  ASSERT(completion_order == (std::vector<uint8_t>{1U, 3U}));
  ASSERT(second_status == OperationPollingStatus::DONE);
  ASSERT(port.accepted_.size() == expected.size());
  ASSERT(std::memcmp(port.accepted_.data(), expected.data(), expected.size()) == 0);
  ASSERT(port.Size() == 0U);
}

void test_write_queue_caps_each_scope_at_front_plus_next()
{
  using namespace LibXR;

  PublicationPort port;
  port.auto_accept_ = false;
  OperationPollingStatus statuses[3];
  WriteOperation operations[3] = {WriteOperation(statuses[0]),
                                  WriteOperation(statuses[1]),
                                  WriteOperation(statuses[2])};
  static const uint8_t FIRST[] = {0x11U, 0x12U};
  static const uint8_t SECOND[] = {0x21U, 0x22U, 0x23U};
  static const uint8_t THIRD[] = {0x31U};

  ASSERT(port(ConstRawData{FIRST, sizeof(FIRST)}, operations[0]) == ErrorCode::OK);
  ASSERT(port(ConstRawData{SECOND, sizeof(SECOND)}, operations[1]) == ErrorCode::OK);
  ASSERT(port(ConstRawData{THIRD, sizeof(THIRD)}, operations[2]) == ErrorCode::OK);

  uint8_t accepted[sizeof(FIRST) + sizeof(SECOND)]{};
  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == sizeof(FIRST));
    ASSERT(queue.next_size == sizeof(SECOND));
    ASSERT(queue.PopBatch(accepted, port.Size()) == sizeof(accepted));
    ASSERT(statuses[0] == OperationPollingStatus::RUNNING);
    ASSERT(statuses[1] == OperationPollingStatus::RUNNING);
    ASSERT(statuses[2] == OperationPollingStatus::RUNNING);
  }

  ASSERT(statuses[0] == OperationPollingStatus::DONE);
  ASSERT(statuses[1] == OperationPollingStatus::DONE);
  ASSERT(statuses[2] == OperationPollingStatus::RUNNING);
  ASSERT(port.Size() == sizeof(THIRD));
  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == sizeof(THIRD));
    ASSERT(queue.next_size == 0U);
    ASSERT(queue.PopBatch(nullptr, sizeof(THIRD)) == sizeof(THIRD));
  }
  ASSERT(statuses[2] == OperationPollingStatus::DONE);
  ASSERT(port.Size() == 0U);
}

void test_fresh_queue_can_fail_a_previous_partial_front()
{
  using namespace LibXR;

  PublicationPort port;
  port.auto_accept_ = false;
  ResultContext context;
  auto callback = Callback<ErrorCode>::Create(RecordResult, &context);
  WriteOperation first_operation(callback);
  OperationPollingStatus second_status;
  WriteOperation second_operation(second_status);
  static const uint8_t FIRST[] = {0x41U, 0x42U, 0x43U};
  static const uint8_t SECOND[] = {0x51U};

  ASSERT(port(ConstRawData{FIRST, sizeof(FIRST)}, first_operation) == ErrorCode::OK);
  ASSERT(port(ConstRawData{SECOND, sizeof(SECOND)}, second_operation) == ErrorCode::OK);

  uint8_t prefix = 0U;
  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.PopBatch(&prefix, 1U) == 1U);
    ASSERT(!queue.FailFront(ErrorCode::INIT_ERR));
    ASSERT(context.count == 0U);
  }
  ASSERT(prefix == FIRST[0]);
  ASSERT(context.count == 0U);

  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == sizeof(FIRST) - 1U);
    ASSERT(queue.next_size == sizeof(SECOND));
    ASSERT(queue.FailFront(ErrorCode::INIT_ERR));
    ASSERT(context.count == 0U);
  }
  ASSERT(context.count == 1U);
  ASSERT(context.result == ErrorCode::INIT_ERR);
  ASSERT(second_status == OperationPollingStatus::RUNNING);

  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == sizeof(SECOND));
    ASSERT(queue.next_size == 0U);
    ASSERT(queue.PopBatch(nullptr, sizeof(SECOND)) == sizeof(SECOND));
  }
  ASSERT(second_status == OperationPollingStatus::DONE);
  ASSERT(port.Size() == 0U);
}

void test_zero_writer_acceptance_preserves_queue_and_operations()
{
  using namespace LibXR;

  PublicationPort port;
  port.auto_accept_ = false;
  OperationPollingStatus statuses[2];
  WriteOperation operations[2] = {WriteOperation(statuses[0]),
                                  WriteOperation(statuses[1])};
  static const uint8_t FIRST[] = {0x11U, 0x12U};
  static const uint8_t SECOND[] = {0x21U, 0x22U, 0x23U};
  constexpr size_t TOTAL_SIZE = sizeof(FIRST) + sizeof(SECOND);

  ASSERT(port(ConstRawData{FIRST, sizeof(FIRST)}, operations[0]) == ErrorCode::OK);
  ASSERT(port(ConstRawData{SECOND, sizeof(SECOND)}, operations[1]) == ErrorCode::OK);

  size_t writer_calls = 0U;
  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == sizeof(FIRST));
    ASSERT(queue.next_size == sizeof(SECOND));
    ASSERT(queue.PopWithWriter(
               TOTAL_SIZE,
               [&writer_calls](const uint8_t*, size_t, const uint8_t*, size_t)
               {
                 writer_calls++;
                 return 0U;
               }) == 0U);
    ASSERT(port.Size() == TOTAL_SIZE);
    ASSERT(statuses[0] == OperationPollingStatus::RUNNING);
    ASSERT(statuses[1] == OperationPollingStatus::RUNNING);
  }

  ASSERT(writer_calls == 1U);
  ASSERT(port.Size() == TOTAL_SIZE);
  ASSERT(statuses[0] == OperationPollingStatus::RUNNING);
  ASSERT(statuses[1] == OperationPollingStatus::RUNNING);
  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == sizeof(FIRST));
    ASSERT(queue.next_size == sizeof(SECOND));
    ASSERT(queue.PopBatch(nullptr, TOTAL_SIZE) == TOTAL_SIZE);
  }
  ASSERT(statuses[0] == OperationPollingStatus::DONE);
  ASSERT(statuses[1] == OperationPollingStatus::DONE);
  ASSERT(port.Size() == 0U);
}

void test_zero_writer_acceptance_can_fail_only_front_in_same_scope()
{
  using namespace LibXR;

  PublicationPort port;
  port.auto_accept_ = false;
  ResultContext first_context;
  auto first_callback = Callback<ErrorCode>::Create(RecordResult, &first_context);
  WriteOperation first_operation(first_callback);
  OperationPollingStatus second_status;
  WriteOperation second_operation(second_status);
  static const uint8_t FIRST[] = {0x31U, 0x32U};
  static const uint8_t SECOND[] = {0x41U, 0x42U, 0x43U};

  ASSERT(port(ConstRawData{FIRST, sizeof(FIRST)}, first_operation) == ErrorCode::OK);
  ASSERT(port(ConstRawData{SECOND, sizeof(SECOND)}, second_operation) == ErrorCode::OK);

  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == sizeof(FIRST));
    ASSERT(queue.next_size == sizeof(SECOND));
    ASSERT(queue.PopWithWriter(sizeof(FIRST) + sizeof(SECOND),
                               [](const uint8_t*, size_t, const uint8_t*, size_t)
                               { return 0U; }) == 0U);
    ASSERT(queue.FailFront(ErrorCode::INIT_ERR));
    ASSERT(first_context.count == 0U);
    ASSERT(second_status == OperationPollingStatus::RUNNING);
  }

  ASSERT(first_context.count == 1U);
  ASSERT(first_context.result == ErrorCode::INIT_ERR);
  ASSERT(second_status == OperationPollingStatus::RUNNING);
  ASSERT(port.Size() == sizeof(SECOND));
  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size == sizeof(SECOND));
    ASSERT(queue.next_size == 0U);
    ASSERT(queue.PopBatch(nullptr, sizeof(SECOND)) == sizeof(SECOND));
  }
  ASSERT(first_context.count == 1U);
  ASSERT(second_status == OperationPollingStatus::DONE);
  ASSERT(port.Size() == 0U);
}

void test_one_queue_settles_non_block_front_before_block_waiter()
{
  using namespace LibXR;

  PublicationPort port;
  port.auto_accept_ = false;
  std::array<uint8_t, 2U> completion_order{};
  size_t completion_count = 0U;
  Semaphore block_semaphore;
  FrontBeforeBlockContext first_context{&block_semaphore, &completion_order,
                                        &completion_count};
  auto first_callback =
      Callback<ErrorCode>::Create(RecordFrontBeforeBlock, &first_context);
  WriteOperation first_operation(first_callback);
  WriteOperation second_operation(block_semaphore, 100U);
  static const uint8_t FIRST[] = {0x51U, 0x52U};
  static const uint8_t SECOND[] = {0x61U, 0x62U, 0x63U};
  static const uint8_t EXPECTED[] = {0x51U, 0x52U, 0x61U, 0x62U, 0x63U};

  ASSERT(port(ConstRawData{FIRST, sizeof(FIRST)}, first_operation) == ErrorCode::OK);
  ASSERT(first_context.callback_count == 0U);
  ASSERT(port.writer_calls_ == 0U);

  port.accept_next_ = true;
  port.auto_accept_ = true;
  const ErrorCode block_result =
      port(ConstRawData{SECOND, sizeof(SECOND)}, second_operation);
  ASSERT(completion_count < completion_order.size());
  completion_order[completion_count++] = 2U;

  ASSERT(block_result == ErrorCode::OK);
  ASSERT(completion_order == (std::array<uint8_t, 2U>{1U, 2U}));
  ASSERT(completion_count == completion_order.size());
  ASSERT(first_context.callback_count == 1U);
  ASSERT(port.writer_calls_ == 1U);
  ASSERT(port.accepted_.size() == sizeof(EXPECTED));
  ASSERT(std::memcmp(port.accepted_.data(), EXPECTED, sizeof(EXPECTED)) == 0);
  ASSERT(block_semaphore.Value() == 0U);
  ASSERT(port.Size() == 0U);
}

void test_stable_idle_action_runs_under_producer_admission()
{
  using namespace LibXR;

  PublicationPort port;
  port.auto_accept_ = false;
  static const uint8_t QUEUED[] = {0x41U};
  WriteOperation queued_operation;
  ASSERT(port(ConstRawData{QUEUED, sizeof(QUEUED)}, queued_operation) == ErrorCode::OK);

  size_t action_count = 0U;
  ASSERT(!port.TryRunWhenWriteQueueIdle([&action_count]() { ++action_count; }));
  ASSERT(action_count == 0U);

  port.auto_accept_ = true;
  port.AcceptWrites();
  ASSERT(port.Size() == 0U);

  static const uint8_t REENTRANT[] = {0x51U};
  WriteOperation reentrant_operation;
  ErrorCode reentrant_result = ErrorCode::FAILED;
  ASSERT(port.TryRunWhenWriteQueueIdle(
      [&]()
      {
        ++action_count;
        reentrant_result =
            port(ConstRawData{REENTRANT, sizeof(REENTRANT)}, reentrant_operation);
      }));
  ASSERT(action_count == 1U);
  ASSERT(reentrant_result == ErrorCode::BUSY);
  ASSERT(port.Size() == 0U);
}

}  // namespace

void RunBaseRwPublicationTests()
{
  test_inline_accept_allows_callback_reentry();
  test_delayed_accept_completes_exact_polling_operation();
  test_old_completion_during_stream_admission_rejects_nested_write();
  test_old_completion_inside_stream_publication_rejects_stream_reentry();
  test_block_completion_can_precede_first_wait();
  test_polling_stream_can_be_reused_after_inline_dequeue();
  test_write_queue_crosses_request_boundaries_and_preserves_completion_order();
  test_write_queue_caps_each_scope_at_front_plus_next();
  test_fresh_queue_can_fail_a_previous_partial_front();
  test_zero_writer_acceptance_preserves_queue_and_operations();
  test_zero_writer_acceptance_can_fail_only_front_in_same_scope();
  test_one_queue_settles_non_block_front_before_block_waiter();
  test_stable_idle_action_runs_under_producer_admission();
}
