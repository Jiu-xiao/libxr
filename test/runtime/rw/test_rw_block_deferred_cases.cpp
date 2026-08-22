/**
 * @file test_rw_block_deferred_cases.cpp
 * @brief Runtime coverage for one deferred direct BLOCK write.
 */
#include <poll.h>
#include <signal.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>

#include "rw_runtime_test_common.hpp"

namespace
{

using namespace LibXR;

volatile sig_atomic_t signal_park_notify_fd = -1;
volatile sig_atomic_t signal_park_release_fd = -1;

void ParkTargetThread(int)
{
  const int saved_errno = errno;
  char token = 1;
  while (write(static_cast<int>(signal_park_notify_fd), &token, sizeof(token)) < 0 &&
         errno == EINTR)
  {
  }
  while (read(static_cast<int>(signal_park_release_fd), &token, sizeof(token)) < 0 &&
         errno == EINTR)
  {
  }
  while (write(static_cast<int>(signal_park_notify_fd), &token, sizeof(token)) < 0 &&
         errno == EINTR)
  {
  }
  errno = saved_errno;
}

class ScopedThreadSignalPark
{
 public:
  ScopedThreadSignalPark()
  {
    REQUIRE(pipe(notify_pipe_) == 0);
    REQUIRE(pipe(release_pipe_) == 0);
    signal_park_notify_fd = notify_pipe_[1];
    signal_park_release_fd = release_pipe_[0];

    struct sigaction action = {};
    action.sa_handler = ParkTargetThread;
    sigemptyset(&action.sa_mask);
    REQUIRE(sigaction(SIGUSR2, &action, &previous_action_) == 0);
  }

  ~ScopedThreadSignalPark()
  {
    Release();
    (void)sigaction(SIGUSR2, &previous_action_, nullptr);
    signal_park_notify_fd = -1;
    signal_park_release_fd = -1;
    (void)close(notify_pipe_[0]);
    (void)close(notify_pipe_[1]);
    (void)close(release_pipe_[0]);
    (void)close(release_pipe_[1]);
  }

  ScopedThreadSignalPark(const ScopedThreadSignalPark&) = delete;
  ScopedThreadSignalPark& operator=(const ScopedThreadSignalPark&) = delete;

  bool Park(pid_t thread_id)
  {
    if (syscall(SYS_tgkill, getpid(), thread_id, SIGUSR2) != 0 || !WaitForNotification())
    {
      return false;
    }
    parked_count_++;
    return true;
  }

  void Release()
  {
    while (parked_count_ > 0U)
    {
      char token = 1;
      REQUIRE(write(release_pipe_[1], &token, sizeof(token)) == sizeof(token));
      REQUIRE(WaitForNotification());
      parked_count_--;
    }
  }

 private:
  bool WaitForNotification()
  {
    struct pollfd wait_fd = {notify_pipe_[0], POLLIN, 0};
    int poll_result = 0;
    do
    {
      poll_result = poll(&wait_fd, 1, static_cast<int>(THREAD_STATE_TIMEOUT_MS));
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result != 1 || (wait_fd.revents & POLLIN) == 0)
    {
      return false;
    }

    char token = 0;
    return read(notify_pipe_[0], &token, sizeof(token)) == sizeof(token);
  }

  int notify_pipe_[2] = {-1, -1};
  int release_pipe_[2] = {-1, -1};
  struct sigaction previous_action_ = {};
  size_t parked_count_ = 0U;
};

size_t ConsumeQueue(WritePort::WriteQueue& queue, uint8_t* destination, size_t limit)
{
  const size_t offered = std::min(limit, queue.front_size + queue.next_size);
  if (offered == 0U)
  {
    return 0U;
  }

  size_t offset = 0U;
  const size_t accepted = queue.PopWithWriter(
      offered,
      [destination, &offset](const uint8_t* first, size_t first_size,
                             const uint8_t* second, size_t second_size)
      {
        std::memcpy(destination + offset, first, first_size);
        offset += first_size;
        if (second_size != 0U)
        {
          std::memcpy(destination + offset, second, second_size);
          offset += second_size;
        }
        return first_size + second_size;
      });
  REQUIRE(accepted == offered);
  REQUIRE(offset == accepted);
  return accepted;
}

size_t ConsumePrefix(WritePort& port, uint8_t* destination, size_t limit,
                     bool in_isr = false)
{
  auto queue = port.GetWriteQueue(in_isr);
  return ConsumeQueue(queue, destination, limit);
}

size_t ConsumeFront(WritePort& port, uint8_t* destination, size_t capacity,
                    bool in_isr = false)
{
  auto queue = port.GetWriteQueue(in_isr);
  if (queue.front_size == 0U)
  {
    return 0U;
  }
  REQUIRE(queue.front_size <= capacity);
  return ConsumeQueue(queue, destination, queue.front_size);
}

struct OrderedCompletionMarker
{
  std::array<uint8_t, 2U>* order;
  size_t* count;
  uint8_t marker;
};

void RecordOrderedCompletion(bool, OrderedCompletionMarker* context, ErrorCode result)
{
  ASSERT(result == ErrorCode::OK);
  ASSERT(*context->count < context->order->size());
  (*context->order)[(*context->count)++] = context->marker;
}

struct DeferredAdmissionRaceContext
{
  WritePort* port;
  ConstRawData data;
  ErrorCode result = ErrorCode::FAILED;
  Semaphore* operation_semaphore;
  Semaphore* writer_done;
  std::atomic<bool> entering{false};
};

void RunDeferredAdmissionRaceWriter(DeferredAdmissionRaceContext* context)
{
  WriteOperation operation(*context->operation_semaphore, UINT32_MAX);
  context->entering.store(true, std::memory_order_release);
  context->result = (*context->port)(context->data, operation);
  context->writer_done->Post();
}

struct AdmissionProgressContext
{
  WritePort* port;
  DeferredAdmissionRaceContext* writer;
  ConstRawData first_data;
  ConstRawData deferred_data;
  Semaphore* done;
};

void RunAdmissionProgress(AdmissionProgressContext context)
{
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(THREAD_STATE_TIMEOUT_MS);
  while (!context.writer->entering.load(std::memory_order_acquire))
  {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    Thread::Yield();
  }

  std::array<uint8_t, 8> data{};
  while (ConsumeFront(*context.port, data.data(), data.size()) == 0U)
  {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    Thread::Yield();
  }
  REQUIRE(std::memcmp(data.data(), context.first_data.addr_, context.first_data.size_) ==
          0);

  data.fill(0U);
  while (ConsumeFront(*context.port, data.data(), data.size()) == 0U)
  {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    Thread::Yield();
  }
  REQUIRE(std::memcmp(data.data(), context.deferred_data.addr_,
                      context.deferred_data.size_) == 0);
  context.done->Post();
}

struct GuardedRewritePump
{
  GuardedRewritePump()
      : callback(Callback<ErrorCode>::CreateGuarded(OnEvent, this)), operation(callback)
  {
  }

  static void OnEvent(bool in_isr, GuardedRewritePump* context, ErrorCode result)
  {
    ASSERT(!context->submit_call_active);
    context->depth++;
    context->max_depth = std::max(context->max_depth, context->depth);

    const bool initial_kick = context->callback_count == 0U;
    context->callback_count++;
    if (!initial_kick)
    {
      ASSERT(result == ErrorCode::OK);
      context->completion_count++;
    }

    if (context->next_submit < context->payloads.size())
    {
      const size_t index = context->next_submit++;
      const auto& payload = context->payloads[index];
      context->submit_call_active = true;
      context->submit_results[index] = context->port(
          ConstRawData{payload.data(), payload.size()}, context->operation, in_isr);
      context->submit_call_active = false;

      ASSERT(context->submit_results[index] == ErrorCode::OK);
      ASSERT(context->port.payload_size == payload.size());
      ASSERT(std::memcmp(context->port.payload, payload.data(), payload.size()) == 0);
    }

    context->depth--;
  }

  SynchronousWritePort port{4, 32};
  Callback<ErrorCode> callback;
  WriteOperation operation;
  const std::array<std::array<uint8_t, 2>, 3> payloads = {
      std::array<uint8_t, 2>{0x61, 0x62},
      std::array<uint8_t, 2>{0x71, 0x72},
      std::array<uint8_t, 2>{0x81, 0x82},
  };
  std::array<ErrorCode, 3> submit_results = {
      ErrorCode::FAILED,
      ErrorCode::FAILED,
      ErrorCode::FAILED,
  };
  size_t next_submit = 0U;
  uint32_t completion_count = 0U;
  uint32_t callback_count = 0U;
  uint32_t depth = 0U;
  uint32_t max_depth = 0U;
  bool submit_call_active = false;
};

class BarrierProbePort : public WritePort
{
 public:
  BarrierProbePort() : WritePort(2U, 8U) { WritePort::operator=(HandleWrite); }

  static void HandleWrite(WritePort& base, bool in_isr)
  {
    auto& port = static_cast<BarrierProbePort&>(base);
    port.doorbell_count_++;
    auto queue = port.GetWriteQueue(in_isr);
    port.last_front_size_ = queue.front_size;
    port.last_next_size_ = queue.next_size;
  }

  uint32_t doorbell_count_ = 0U;
  size_t last_front_size_ = 0U;
  size_t last_next_size_ = 0U;
};

void test_progress_racing_deferred_admission_is_not_lost()
{
  WritePort port(2, 4);
  port = PendingWriteFun;
  static const uint8_t A[] = {0x01, 0x02, 0x03, 0x04};
  static const uint8_t B[] = {0x11, 0x12};
  constexpr uint32_t ITERATIONS = 128U;

  for (uint32_t iteration = 0; iteration < ITERATIONS; iteration++)
  {
    WriteOperation first_operation;
    ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

    Semaphore writer_done;
    Semaphore progress_done;
    Semaphore operation_semaphore;
    DeferredAdmissionRaceContext writer_context{&port, ConstRawData{B, sizeof(B)},
                                                ErrorCode::FAILED, &operation_semaphore,
                                                &writer_done};
    AdmissionProgressContext progress_context{&port, &writer_context,
                                              ConstRawData{A, sizeof(A)},
                                              ConstRawData{B, sizeof(B)}, &progress_done};
    Thread progress;
    Thread writer;
    progress.Create(progress_context, RunAdmissionProgress, "wr_admit_progress", 1024,
                    Thread::Priority::MEDIUM);
    writer.Create<DeferredAdmissionRaceContext*>(
        &writer_context, RunDeferredAdmissionRaceWriter, "wr_admit_race", 1024,
        Thread::Priority::MEDIUM);

    ExpectWaitOk(progress_done, THREAD_STATE_TIMEOUT_MS);
    ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
    JoinThreadIfNeeded(progress);
    JoinThreadIfNeeded(writer);
    ASSERT(writer_context.result == ErrorCode::OK);
    ASSERT(operation_semaphore.Value() == 0U);
    ASSERT(port.Size() == 0U);
  }
}

void test_deferred_write_waits_for_full_capacity_and_completion()
{
  WritePort port(3, 8);
  port = PendingWriteFun;
  static const uint8_t A[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
  static const uint8_t B[] = {0x21, 0x22, 0x23, 0x24};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext context{&port, ConstRawData{B, sizeof(B)}, UINT32_MAX,
                                   ErrorCode::FAILED, &writer_done};
  context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, context, "wr_defer_space");
  REQUIRE(WaitForLinuxFutexWait(context.thread_id));
  ASSERT(port.Size() == sizeof(A));

  uint8_t first[sizeof(A)]{};
  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.PopBatch(first, 2U) == 2U);
    ASSERT(port.Size() == sizeof(A) - 2U);
    ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);
  }
  ASSERT(port.Size() == sizeof(A) - 2U + sizeof(B));

  ASSERT(ConsumePrefix(port, first + 2U, sizeof(A) - 2U) == sizeof(A) - 2U);
  ASSERT(std::memcmp(first, A, sizeof(A)) == 0);
  ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);
  ASSERT(port.Size() == sizeof(B));

  uint8_t second[sizeof(B)]{};
  ASSERT(ConsumeFront(port, second, sizeof(second)) == sizeof(second));
  ASSERT(std::memcmp(second, B, sizeof(B)) == 0);

  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::OK);
  ASSERT(writer_semaphore.Value() == 0U);
  ASSERT(port.Size() == 0U);
}

void test_timeout_before_publish_claim_cancels_without_copy()
{
  WritePort port(2, 4);
  port = PendingWriteFun;
  static const uint8_t A[] = {0x31, 0x32, 0x33, 0x34};
  uint8_t deferred[] = {0x41, 0x42};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore semaphore;
  WriteOperation deferred_operation(semaphore, 0);
  ASSERT(port(ConstRawData{deferred, sizeof(deferred)}, deferred_operation) ==
         ErrorCode::TIMEOUT);
  ASSERT(semaphore.Value() == 0U);
  deferred[0] = 0xEE;
  deferred[1] = 0xEF;

  uint8_t first[sizeof(A)]{};
  ASSERT(ConsumeFront(port, first, sizeof(first)) == sizeof(first));
  ASSERT(std::memcmp(first, A, sizeof(A)) == 0);
  ASSERT(port.Size() == 0U);

  static const uint8_t C[] = {0x51};
  WriteOperation reused(semaphore, 1000);
  Semaphore finish_done;
  Thread finisher;
  StartWriteFinisher(finisher, port, finish_done, ErrorCode::OK, "wr_defer_reuse");
  ASSERT(port(ConstRawData{C, sizeof(C)}, reused) == ErrorCode::OK);
  ExpectWaitOk(finish_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(finisher);
  ASSERT(semaphore.Value() == 0U);
}

void test_timeout_after_publication_uses_the_copied_payload()
{
  WritePort port(3, 4);
  port = PendingWriteFun;
  static const uint8_t A[] = {0x61, 0x62, 0x63, 0x64};
  uint8_t deferred[] = {0x71, 0x72, 0x73};
  static const uint8_t EXPECTED[] = {0x71, 0x72, 0x73};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext context{&port, ConstRawData{deferred, sizeof(deferred)}, 100U,
                                   ErrorCode::FAILED, &writer_done};
  context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, context, "wr_publish_timeout");
  REQUIRE(WaitForLinuxFutexWaitMode(context.thread_id, LinuxFutexWaitMode::TIMED));

  uint8_t first[sizeof(A)]{};
  ASSERT(ConsumeFront(port, first, sizeof(first)) == sizeof(first));
  ASSERT(std::memcmp(first, A, sizeof(A)) == 0);
  ASSERT(port.Size() == sizeof(deferred));

  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::TIMEOUT);
  ASSERT(writer_semaphore.Value() == 0U);

  deferred[0] = 0xEE;
  deferred[1] = 0xEF;
  uint8_t queued[sizeof(deferred)]{};
  ASSERT(ConsumeFront(port, queued, sizeof(queued)) == sizeof(queued));
  ASSERT(std::memcmp(queued, EXPECTED, sizeof(EXPECTED)) == 0);
  ASSERT(writer_semaphore.Value() == 0U);
}

void test_old_detached_record_coexists_with_new_deferred_request()
{
  WritePort port(3, 4);
  port = PendingWriteFun;
  static const uint8_t A[] = {0x81, 0x82, 0x83, 0x84};
  static const uint8_t B[] = {0x91, 0x92, 0x93};
  Semaphore first_semaphore;
  WriteOperation first_operation(first_semaphore, 0);
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::TIMEOUT);

  Semaphore second_done;
  Semaphore second_semaphore;
  BlockingWriteCallContext second{&port, ConstRawData{B, sizeof(B)}, UINT32_MAX,
                                  ErrorCode::FAILED, &second_done};
  second.semaphore = &second_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, second, "wr_after_detach");
  REQUIRE(WaitForLinuxFutexWait(second.thread_id));
  ASSERT(port.Size() == sizeof(A));

  uint8_t first[sizeof(A)]{};
  ASSERT(ConsumeFront(port, first, sizeof(first)) == sizeof(first));
  ASSERT(std::memcmp(first, A, sizeof(A)) == 0);
  ASSERT(port.Size() == sizeof(B));

  uint8_t deferred[sizeof(B)]{};
  ASSERT(ConsumeFront(port, deferred, sizeof(deferred)) == sizeof(deferred));
  ASSERT(std::memcmp(deferred, B, sizeof(B)) == 0);
  ExpectWaitOk(second_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(second.result == ErrorCode::OK);
  ASSERT(first_semaphore.Value() == 0U);
  ASSERT(second_semaphore.Value() == 0U);
}

void test_non_block_writes_publish_behind_detached_front()
{
  WritePort port(6, 16);
  port = PendingWriteFun;
  static const uint8_t A[] = {0x11, 0x12};
  static const uint8_t B[] = {0x21, 0x22};
  static const uint8_t C[] = {0x31, 0x32};
  static const uint8_t D[] = {0x41, 0x42};
  static const uint8_t E[] = {0x51, 0x52};

  Semaphore first_semaphore;
  WriteOperation first_operation(first_semaphore, 0U);
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::TIMEOUT);

  WriteOperation none_operation;
  ASSERT(port(ConstRawData{B, sizeof(B)}, none_operation) == ErrorCode::OK);

  OperationPollingStatus polling_status;
  WriteOperation polling_operation(polling_status);
  ASSERT(port(ConstRawData{C, sizeof(C)}, polling_operation) == ErrorCode::OK);

  std::array<uint8_t, 2U> completion_order{};
  size_t completion_count = 0U;
  OrderedCompletionMarker direct_marker{&completion_order, &completion_count, 1U};
  auto direct_callback =
      Callback<ErrorCode>::Create(RecordOrderedCompletion, &direct_marker);
  WriteOperation callback_operation(direct_callback);
  ASSERT(port(ConstRawData{D, sizeof(D)}, callback_operation) == ErrorCode::OK);

  OrderedCompletionMarker stream_marker{&completion_order, &completion_count, 2U};
  auto stream_callback =
      Callback<ErrorCode>::Create(RecordOrderedCompletion, &stream_marker);
  WriteOperation stream_operation(stream_callback);
  WritePort::Stream stream(&port, stream_operation);
  ASSERT(stream.Write(ConstRawData{E, sizeof(E)}) == ErrorCode::OK);
  ASSERT(stream.Commit() == ErrorCode::OK);

  ASSERT(first_semaphore.Value() == 0U);
  ASSERT(polling_status == OperationPollingStatus::RUNNING);
  ASSERT(completion_count == 0U);

  static const std::array<uint8_t, 10U> EXPECTED = {A[0], A[1], B[0], B[1], C[0],
                                                    C[1], D[0], D[1], E[0], E[1]};
  std::array<uint8_t, EXPECTED.size()> accepted{};
  size_t offset = 0U;
  while (offset != EXPECTED.size())
  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.front_size != 0U);
    const size_t accepted_now = queue.PopWithWriter(
        EXPECTED.size() - offset,
        [&accepted, &offset](const uint8_t* first, size_t first_size,
                             const uint8_t* second, size_t second_size)
        {
          std::memcpy(accepted.data() + offset, first, first_size);
          offset += first_size;
          if (second_size != 0U)
          {
            std::memcpy(accepted.data() + offset, second, second_size);
            offset += second_size;
          }
          return first_size + second_size;
        });
    ASSERT(accepted_now != 0U);
  }

  ASSERT(accepted == EXPECTED);
  ASSERT(polling_status == OperationPollingStatus::DONE);
  ASSERT(completion_count == completion_order.size());
  ASSERT(completion_order == (std::array<uint8_t, 2U>{1U, 2U}));
  ASSERT(first_semaphore.Value() == 0U);
  ASSERT(port.Size() == 0U);
}

void test_failed_promoted_front_returns_exact_block_result()
{
  WritePort port(3, 4);
  port = PendingWriteFun;
  static const uint8_t A[] = {0xA1, 0xA2, 0xA3, 0xA4};
  static const uint8_t B[] = {0xB1, 0xB2};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext context{&port, ConstRawData{B, sizeof(B)}, UINT32_MAX,
                                   ErrorCode::FAILED, &writer_done};
  context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, context, "wr_defer_fail");
  REQUIRE(WaitForLinuxFutexWait(context.thread_id));

  uint8_t first[sizeof(A)]{};
  ASSERT(ConsumeFront(port, first, sizeof(first)) == sizeof(first));
  {
    auto queue = port.GetWriteQueue();
    ASSERT(queue.FailFront(ErrorCode::INIT_ERR));
  }

  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::INIT_ERR);
  ASSERT(writer_semaphore.Value() == 0U);
  ASSERT(port.Size() == 0U);
}

void test_guarded_completion_replay_submits_after_owner_release()
{
  GuardedRewritePump context;

  context.callback.Run(false, ErrorCode::OK);

  ASSERT(context.next_submit == context.payloads.size());
  ASSERT(context.completion_count == context.payloads.size());
  ASSERT(context.callback_count == context.payloads.size() + 1U);
  ASSERT(context.max_depth == 1U);
  ASSERT(context.depth == 0U);
  ASSERT(!context.submit_call_active);
  for (const ErrorCode result : context.submit_results)
  {
    ASSERT(result == ErrorCode::OK);
  }
  ASSERT(context.port.Size() == 0U);
}

void test_claimed_block_holds_admission_until_waiter_returns()
{
  WritePort port(2, 8);
  port = PendingWriteFun;
  static const uint8_t A[] = {0xD1, 0xD2};
  static const uint8_t B[] = {0xE1};

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext context{&port, ConstRawData{A, sizeof(A)}, UINT32_MAX,
                                   ErrorCode::FAILED, &writer_done};
  context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, context, "wr_claimed_handoff");
  REQUIRE(WaitForLinuxFutexWait(context.thread_id));

  ScopedThreadSignalPark parked_writer;
  REQUIRE(parked_writer.Park(context.thread_id.load(std::memory_order_acquire)));
  uint8_t accepted[sizeof(A)]{};
  ASSERT(ConsumeFront(port, accepted, sizeof(accepted)) == sizeof(accepted));
  ASSERT(std::memcmp(accepted, A, sizeof(A)) == 0);

  WriteOperation non_blocking;
  ASSERT(port(ConstRawData{B, sizeof(B)}, non_blocking) == ErrorCode::BUSY);
  Semaphore blocked_semaphore;
  WriteOperation blocked(blocked_semaphore, 0U);
  ASSERT(port(ConstRawData{B, sizeof(B)}, blocked) == ErrorCode::BUSY);
  ASSERT(blocked_semaphore.Value() == 0U);
  ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);

  parked_writer.Release();
  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::OK);
  ASSERT(writer_semaphore.Value() == 0U);

  ASSERT(port(ConstRawData{B, sizeof(B)}, non_blocking) == ErrorCode::OK);
  uint8_t recovered[sizeof(B)]{};
  ASSERT(ConsumeFront(port, recovered, sizeof(recovered)) == sizeof(recovered));
  ASSERT(recovered[0] == B[0]);
}

void test_deferred_slot_rejects_other_producers_and_validates_input()
{
  WritePort port(3, 4);
  port = PendingWriteFun;
  static const uint8_t A[] = {0xC1, 0xC2, 0xC3, 0xC4};
  static const uint8_t B[] = {0xD1, 0xD2};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore invalid_semaphore;
  WriteOperation invalid_operation(invalid_semaphore, 0);
  ASSERT(port(ConstRawData{nullptr, 1}, invalid_operation) == ErrorCode::PTR_NULL);
  uint8_t oversized[5]{};
  ASSERT(port(ConstRawData{oversized, sizeof(oversized)}, invalid_operation) ==
         ErrorCode::SIZE_ERR);
  ASSERT(invalid_semaphore.Value() == 0U);

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext context{&port, ConstRawData{B, sizeof(B)}, UINT32_MAX,
                                   ErrorCode::FAILED, &writer_done};
  context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, context, "wr_defer_exclusive");
  REQUIRE(WaitForLinuxFutexWait(context.thread_id));

  WriteOperation none_operation;
  ASSERT(port(ConstRawData{B, sizeof(B)}, none_operation) == ErrorCode::BUSY);
  OperationPollingStatus polling_status;
  WriteOperation polling_operation(polling_status);
  ASSERT(port(ConstRawData{B, sizeof(B)}, polling_operation) == ErrorCode::BUSY);
  ASSERT(polling_status.Load() == OperationPollingStatus::READY);
  WriteHarness callback_operation(TestMode::CALLBACK);
  ASSERT(port(ConstRawData{B, sizeof(B)}, callback_operation.op) == ErrorCode::BUSY);
  callback_operation.ExpectPendingSubmitted();
  Semaphore other_semaphore;
  WriteOperation other_block(other_semaphore, 0);
  ASSERT(port(ConstRawData{B, sizeof(B)}, other_block) == ErrorCode::BUSY);
  ASSERT(other_semaphore.Value() == 0U);

  uint8_t first[sizeof(A)]{};
  ASSERT(ConsumeFront(port, first, sizeof(first)) == sizeof(first));
  uint8_t deferred[sizeof(B)]{};
  ASSERT(ConsumeFront(port, deferred, sizeof(deferred)) == sizeof(deferred));
  ASSERT(std::memcmp(deferred, B, sizeof(B)) == 0);
  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::OK);
  ASSERT(writer_semaphore.Value() == 0U);
}

void test_empty_stream_release_rings_the_backend_doorbell()
{
  BarrierProbePort port;
  WriteOperation operation;
  WritePort::Stream stream(&port, operation);

  ASSERT(port.doorbell_count_ == 0U);
  ASSERT(stream.Commit() == ErrorCode::OK);
  ASSERT(port.doorbell_count_ == 1U);
  ASSERT(port.last_front_size_ == 0U);
  ASSERT(port.last_next_size_ == 0U);
}

void test_stream_space_failure_never_defers_caller_data()
{
  WritePort port(2, 4);
  port = PendingWriteFun;
  static const uint8_t A[] = {0xE1, 0xE2, 0xE3, 0xE4};
  uint8_t stream_data[] = {0xF1, 0xF2};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore stream_semaphore;
  WriteOperation stream_operation(stream_semaphore, 1000);
  WritePort::Stream stream(&port, stream_operation);
  ASSERT(stream.Write(ConstRawData{stream_data, sizeof(stream_data)}) == ErrorCode::FULL);
  stream_data[0] = 0x00;
  ASSERT(stream.Commit() == ErrorCode::OK);
  ASSERT(stream_semaphore.Value() == 0U);
  ASSERT(port.Size() == sizeof(A));

  uint8_t first[sizeof(A)]{};
  ASSERT(ConsumeFront(port, first, sizeof(first)) == sizeof(first));
  ASSERT(std::memcmp(first, A, sizeof(A)) == 0);
  ASSERT(stream_semaphore.Value() == 0U);
  ASSERT(port.Size() == 0U);

  static const uint8_t FRESH[] = {0xA1, 0xA2};
  WriteOperation fresh_operation;
  WritePort::Stream fresh(&port, fresh_operation);
  ASSERT(fresh.Write(ConstRawData{FRESH, sizeof(FRESH)}) == ErrorCode::OK);
  ASSERT(fresh.Commit() == ErrorCode::OK);
  uint8_t fresh_data[sizeof(FRESH)]{};
  ASSERT(ConsumeFront(port, fresh_data, sizeof(fresh_data)) == sizeof(fresh_data));
  ASSERT(std::memcmp(fresh_data, FRESH, sizeof(FRESH)) == 0);
  ASSERT(stream_semaphore.Value() == 0U);
  ASSERT(port.Size() == 0U);
}

void test_pipe_consumer_progress_promotes_deferred_block()
{
  Pipe pipe(8);
  ReadPort& read = pipe.GetReadPort();
  WritePort& write = pipe.GetWritePort();
  static const uint8_t A[] = {1, 2, 3, 4, 5, 6};
  static const uint8_t B[] = {7, 8, 9, 10};
  WriteOperation first_operation;
  ASSERT(write(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext context{&write, ConstRawData{B, sizeof(B)}, UINT32_MAX,
                                   ErrorCode::FAILED, &writer_done};
  context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, context, "pipe_defer_partial");
  REQUIRE(WaitForLinuxFutexWait(context.thread_id));

  uint8_t prefix = 0;
  ReadOperation read_operation;
  ASSERT(read(RawData{&prefix, 1}, read_operation) == ErrorCode::OK);
  ASSERT(prefix == A[0]);
  ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);
  ASSERT(read(RawData{&prefix, 1}, read_operation) == ErrorCode::OK);
  ASSERT(prefix == A[1]);

  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::OK);
  ASSERT(writer_semaphore.Value() == 0U);

  uint8_t remaining[8]{};
  ASSERT(read(RawData{remaining, sizeof(remaining)}, read_operation) == ErrorCode::OK);
  static const uint8_t EXPECTED[] = {3, 4, 5, 6, 7, 8, 9, 10};
  ASSERT(std::memcmp(remaining, EXPECTED, sizeof(EXPECTED)) == 0);
}

void test_pipe_clear_promotes_deferred_block()
{
  Pipe pipe(4);
  ReadPort& read = pipe.GetReadPort();
  WritePort& write = pipe.GetWritePort();
  static const uint8_t A[] = {0x11, 0x12, 0x13, 0x14};
  static const uint8_t B[] = {0x21, 0x22, 0x23};
  WriteOperation first_operation;
  ASSERT(write(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext context{&write, ConstRawData{B, sizeof(B)}, UINT32_MAX,
                                   ErrorCode::FAILED, &writer_done};
  context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, context, "pipe_defer_clear");
  REQUIRE(WaitForLinuxFutexWait(context.thread_id));
  ASSERT(read.ClearQueuedData(false) == ErrorCode::OK);

  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::OK);
  ASSERT(writer_semaphore.Value() == 0U);
  uint8_t queued[sizeof(B)]{};
  ReadOperation read_operation;
  ASSERT(read(RawData{queued, sizeof(queued)}, read_operation) == ErrorCode::OK);
  ASSERT(std::memcmp(queued, B, sizeof(B)) == 0);
}

}  // namespace

void RunRuntimeRwBlockDeferredTests()
{
  test_progress_racing_deferred_admission_is_not_lost();
  test_deferred_write_waits_for_full_capacity_and_completion();
  test_timeout_before_publish_claim_cancels_without_copy();
  test_timeout_after_publication_uses_the_copied_payload();
  test_old_detached_record_coexists_with_new_deferred_request();
  test_non_block_writes_publish_behind_detached_front();
  test_failed_promoted_front_returns_exact_block_result();
  test_guarded_completion_replay_submits_after_owner_release();
  test_claimed_block_holds_admission_until_waiter_returns();
  test_deferred_slot_rejects_other_producers_and_validates_input();
  test_empty_stream_release_rings_the_backend_doorbell();
  test_stream_space_failure_never_defers_caller_data();
  test_pipe_consumer_progress_promotes_deferred_block();
  test_pipe_clear_promotes_deferred_block();
}
