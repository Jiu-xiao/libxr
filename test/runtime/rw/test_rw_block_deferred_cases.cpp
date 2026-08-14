/**
 * @file test_rw_block_deferred_cases.cpp
 * @brief Runtime coverage for one deferred direct BLOCK write.
 */
#include <poll.h>
#include <signal.h>

#include <array>
#include <cerrno>

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
    if (syscall(SYS_tgkill, getpid(), thread_id, SIGUSR2) != 0)
    {
      return false;
    }
    if (!WaitForNotification())
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

constexpr uint32_t LITMUS_PHASE_MASK = 0x3U;
constexpr uint32_t LITMUS_RESERVED = 0x1U;
constexpr uint32_t LITMUS_WAITING = 0x2U;
constexpr uint32_t LITMUS_PUBLISHING = 0x3U;
constexpr uint32_t LITMUS_CARRIER = 0x4U;

uint32_t LitmusWithPhase(uint32_t state, uint32_t phase)
{
  return (state & ~LITMUS_PHASE_MASK) | phase;
}

struct CapacityCarrierLitmusContext
{
  std::atomic<uint32_t> state{LITMUS_PUBLISHING};
  std::atomic<bool> capacity{false};
  std::atomic<bool> published{false};
  Semaphore capacity_sampled;
  Semaphore allow_waiting_cas;
  Semaphore done;
};

void RunCapacityCarrierLitmus(CapacityCarrierLitmusContext* context)
{
  uint32_t observed = context->state.load(std::memory_order_acquire);
  bool first_capacity_sample = true;
  while (true)
  {
    ASSERT((observed & LITMUS_PHASE_MASK) == LITMUS_PUBLISHING);
    if (context->capacity.load(std::memory_order_acquire))
    {
      context->published.store(true, std::memory_order_release);
      context->done.Post();
      return;
    }

    if (first_capacity_sample)
    {
      first_capacity_sample = false;
      context->capacity_sampled.Post();
      REQUIRE(context->allow_waiting_cas.Wait(UINT32_MAX) == ErrorCode::OK);
    }

    if ((observed & LITMUS_CARRIER) != 0U)
    {
      const uint32_t retry = observed & ~LITMUS_CARRIER;
      if (context->state.compare_exchange_weak(observed, retry, std::memory_order_acq_rel,
                                               std::memory_order_acquire))
      {
        observed = context->state.load(std::memory_order_acquire);
      }
      continue;
    }

    const uint32_t waiting = LitmusWithPhase(observed, LITMUS_WAITING);
    if (context->state.compare_exchange_weak(observed, waiting, std::memory_order_acq_rel,
                                             std::memory_order_acquire))
    {
      context->done.Post();
      return;
    }
  }
}

struct RegistrationCarrierLitmusContext
{
  std::atomic<uint32_t> state{LITMUS_RESERVED};
  std::atomic<bool> info_ready{false};
  std::atomic<bool> published{false};
  Semaphore reservation_visible;
  Semaphore progress_recorded;
  Semaphore done;
};

void RunRegistrationCarrierLitmus(RegistrationCarrierLitmusContext* context)
{
  context->reservation_visible.Post();
  REQUIRE(context->progress_recorded.Wait(UINT32_MAX) == ErrorCode::OK);

  context->info_ready.store(true, std::memory_order_release);
  uint32_t observed = context->state.load(std::memory_order_acquire);
  ASSERT((observed & LITMUS_PHASE_MASK) == LITMUS_RESERVED);
  const uint32_t waiting = LitmusWithPhase(observed, LITMUS_WAITING);
  ASSERT(context->state.compare_exchange_strong(
      observed, waiting, std::memory_order_acq_rel, std::memory_order_acquire));
  ASSERT((context->state.load(std::memory_order_acquire) & LITMUS_CARRIER) != 0U);

  context->state.fetch_or(LITMUS_CARRIER, std::memory_order_acq_rel);
  observed = context->state.load(std::memory_order_acquire);
  ASSERT((observed & LITMUS_PHASE_MASK) == LITMUS_WAITING);
  const uint32_t publishing = LitmusWithPhase(observed, LITMUS_PUBLISHING);
  ASSERT(context->state.compare_exchange_strong(
      observed, publishing, std::memory_order_acq_rel, std::memory_order_acquire));
  ASSERT(context->info_ready.load(std::memory_order_acquire));
  context->published.store(true, std::memory_order_release);
  context->done.Post();
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
  WriteInfoBlock first{};
  {
    auto dequeue = context.port->BeginDequeue(false);
    REQUIRE(dequeue.PopInfo(first) == ErrorCode::OK);
    REQUIRE(first.data.size_ == context.first_data.size_);
    REQUIRE(dequeue.PopData(data.data(), first.data.size_) == ErrorCode::OK);
  }
  REQUIRE(std::memcmp(data.data(), context.first_data.addr_, first.data.size_) == 0);
  context.port->Finish(false, ErrorCode::OK, first);

  WriteInfoBlock deferred{};
  while (context.port->QueueInfo()->Peek(deferred) != ErrorCode::OK)
  {
    REQUIRE(std::chrono::steady_clock::now() < deadline);
    Thread::Yield();
  }
  {
    auto dequeue = context.port->BeginDequeue(false);
    REQUIRE(dequeue.PopInfo(deferred) == ErrorCode::OK);
    REQUIRE(deferred.data.size_ == context.deferred_data.size_);
    REQUIRE(dequeue.PopData(data.data(), deferred.data.size_) == ErrorCode::OK);
  }
  REQUIRE(std::memcmp(data.data(), context.deferred_data.addr_, deferred.data.size_) ==
          0);
  context.port->Finish(false, ErrorCode::OK, deferred);
  context.done->Post();
}

struct ControlledWritePort : WritePort
{
  enum class Mode : uint8_t
  {
    PENDING,
    FINISH,
    RETURN,
    STAGED_PENDING,
    STAGED_FINISH,
  };

  ControlledWritePort(size_t queue_size, size_t buffer_size)
      : WritePort(queue_size, buffer_size)
  {
    WritePort::operator=(HandleWrite);
  }

  static ErrorCode HandleWrite(WritePort& base, bool in_isr)
  {
    auto& port = static_cast<ControlledWritePort&>(base);
    const uint32_t call = port.call_count.fetch_add(1, std::memory_order_acq_rel) + 1U;
    if (call == 1U || port.mode == Mode::PENDING)
    {
      return ErrorCode::PENDING;
    }

    if (port.mode == Mode::STAGED_PENDING)
    {
      port.entered.PostFromCallback(in_isr);
      REQUIRE(port.release.Wait(UINT32_MAX) == ErrorCode::OK);
      return ErrorCode::PENDING;
    }

    WriteInfoBlock info{};
    {
      auto dequeue = port.BeginDequeue(in_isr);
      ASSERT(dequeue.PopInfo(info) == ErrorCode::OK);
      ASSERT(info.data.size_ <= sizeof(port.payload));
      port.payload_size = info.data.size_;
      ASSERT(dequeue.PopData(port.payload, port.payload_size) == ErrorCode::OK);
    }

    if (port.mode == Mode::FINISH)
    {
      port.Finish(in_isr, port.result, info);
      return ErrorCode::PENDING;
    }

    if (port.mode == Mode::STAGED_FINISH)
    {
      port.Finish(in_isr, port.result, info);
      port.entered.PostFromCallback(in_isr);
      REQUIRE(port.release.Wait(UINT32_MAX) == ErrorCode::OK);
      return ErrorCode::PENDING;
    }
    return port.result;
  }

  std::atomic<uint32_t> call_count{0};
  Mode mode = Mode::PENDING;
  ErrorCode result = ErrorCode::OK;
  Semaphore entered;
  Semaphore release;
  uint8_t payload[32]{};
  size_t payload_size = 0;
};

struct TerminalReturnWritePort : WritePort
{
  TerminalReturnWritePort() : WritePort(2, 8) { WritePort::operator=(HandleWrite); }

  static ErrorCode HandleWrite(WritePort& base, bool)
  {
    auto& port = static_cast<TerminalReturnWritePort&>(base);
    WriteInfoBlock info{};
    {
      auto dequeue = port.BeginDequeue(false);
      ASSERT(dequeue.PopInfo(info) == ErrorCode::OK);
      ASSERT(dequeue.DiscardData(info.data.size_) == ErrorCode::OK);
    }
    port.call_count++;
    return ErrorCode::OK;
  }

  uint32_t call_count = 0U;
};

struct TerminalReturnReentry
{
  TerminalReturnWritePort* port;
  ErrorCode completion_result = ErrorCode::FAILED;
  ErrorCode nested_result = ErrorCode::FAILED;
};

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
    if (context->depth > context->max_depth)
    {
      context->max_depth = context->depth;
    }

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

  ImmediateFinishWritePort port{4, 32};
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

void OnTerminalReturnComplete(bool, TerminalReturnReentry* context, ErrorCode result)
{
  context->completion_result = result;
  static const uint8_t NESTED[] = {0x5A};
  WriteOperation nested_operation;
  context->nested_result =
      (*context->port)(ConstRawData{NESTED, sizeof(NESTED)}, nested_operation);
}

struct DequeueRecordContext
{
  WritePort* port;
  WriteInfoBlock* info;
  uint8_t* data;
  size_t size;
  Semaphore* done;
};

void RunDequeueRecord(DequeueRecordContext context)
{
  {
    auto dequeue = context.port->BeginDequeue(false);
    REQUIRE(dequeue.PopInfo(*context.info) == ErrorCode::OK);
    REQUIRE(context.info->data.size_ == context.size);
    REQUIRE(dequeue.PopData(context.data, context.size) == ErrorCode::OK);
  }
  context.done->Post();
}

void StartDequeueRecord(Thread& thread, WritePort& port, WriteInfoBlock& info,
                        uint8_t* data, size_t size, Semaphore& done, const char* name)
{
  thread.Create(DequeueRecordContext{&port, &info, data, size, &done}, RunDequeueRecord,
                name, 1024, Thread::Priority::MEDIUM);
}

WriteInfoBlock PopRecord(WritePort& port, uint8_t* data)
{
  WriteInfoBlock info{};
  {
    auto dequeue = port.BeginDequeue(false);
    ASSERT(dequeue.PopInfo(info) == ErrorCode::OK);
    ASSERT(dequeue.PopData(data, info.data.size_) == ErrorCode::OK);
  }
  return info;
}

void test_capacity_progress_carrier_survives_return_to_waiting_cas()
{
  CapacityCarrierLitmusContext context;
  Thread publisher;
  publisher.Create<CapacityCarrierLitmusContext*>(&context, RunCapacityCarrierLitmus,
                                                  "wr_kick_litmus", 1024,
                                                  Thread::Priority::MEDIUM);

  ExpectWaitOk(context.capacity_sampled, THREAD_STATE_TIMEOUT_MS);
  context.capacity.store(true, std::memory_order_release);
  context.state.fetch_or(LITMUS_CARRIER, std::memory_order_acq_rel);
  context.allow_waiting_cas.Post();

  ExpectWaitOk(context.done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(publisher);
  ASSERT(context.published.load(std::memory_order_acquire));
  ASSERT((context.state.load(std::memory_order_acquire) & LITMUS_PHASE_MASK) ==
         LITMUS_PUBLISHING);
}

void test_progress_during_deferred_slot_registration_is_closed_after_publication()
{
  RegistrationCarrierLitmusContext context;
  Thread writer;
  writer.Create<RegistrationCarrierLitmusContext*>(&context, RunRegistrationCarrierLitmus,
                                                   "wr_register_litmus", 1024,
                                                   Thread::Priority::MEDIUM);

  ExpectWaitOk(context.reservation_visible, THREAD_STATE_TIMEOUT_MS);
  context.state.fetch_or(LITMUS_CARRIER, std::memory_order_acq_rel);
  context.progress_recorded.Post();

  ExpectWaitOk(context.done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.info_ready.load(std::memory_order_acquire));
  ASSERT(context.published.load(std::memory_order_acquire));
}

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
    ASSERT(port.QueueInfo()->Size() == 0U);
    ASSERT(port.Size() == 0U);
  }
}

void test_deferred_write_remains_pending_until_enough_space_is_released()
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
  ASSERT(port.QueueInfo()->Size() == 1U);
  ASSERT(port.Size() == sizeof(A));

  WriteInfoBlock first_info{};
  uint8_t first_prefix[1]{};
  {
    auto dequeue = port.BeginDequeue(false);
    ASSERT(dequeue.PopInfo(first_info) == ErrorCode::OK);
    ASSERT(dequeue.PopData(first_prefix, sizeof(first_prefix)) == ErrorCode::OK);
    ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);
  }
  ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);
  ASSERT(port.QueueInfo()->Size() == 0U);
  ASSERT(port.Size() == sizeof(A) - sizeof(first_prefix));

  uint8_t first_tail[sizeof(A) - sizeof(first_prefix)]{};
  {
    auto dequeue = port.BeginDequeue(false);
    ASSERT(dequeue.PopData(first_tail, sizeof(first_tail)) == ErrorCode::OK);
    ASSERT(port.QueueInfo()->Size() == 0U);
  }
  port.Finish(false, ErrorCode::OK, first_info);
  REQUIRE(port.QueueInfo()->Size() == 1U);

  uint8_t second_data[sizeof(B)]{};
  WriteInfoBlock second_info = PopRecord(port, second_data);
  ASSERT(std::memcmp(second_data, B, sizeof(B)) == 0);
  port.Finish(false, ErrorCode::OK, second_info);

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

  uint8_t first_data[sizeof(A)]{};
  WriteInfoBlock first_info = PopRecord(port, first_data);
  port.Finish(false, ErrorCode::OK, first_info);
  ASSERT(port.QueueInfo()->Size() == 0U);
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

void test_publish_claim_before_timeout_preserves_buffer_until_handoff()
{
  ControlledWritePort port(3, 4);
  static const uint8_t A[] = {0x61, 0x62, 0x63, 0x64};
  uint8_t deferred[] = {0x71, 0x72, 0x73};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext context{&port, ConstRawData{deferred, sizeof(deferred)}, 1000,
                                   ErrorCode::FAILED, &writer_done};
  context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, context, "wr_publish_timeout");
  REQUIRE(WaitForLinuxFutexWaitMode(context.thread_id, LinuxFutexWaitMode::TIMED));

  port.mode = ControlledWritePort::Mode::STAGED_PENDING;
  uint8_t first_data[sizeof(A)]{};
  WriteInfoBlock first_info{};
  Semaphore publisher_done;
  Thread publisher;
  StartDequeueRecord(publisher, port, first_info, first_data, sizeof(first_data),
                     publisher_done, "wr_publish_stage");
  ExpectWaitOk(port.entered, THREAD_STATE_TIMEOUT_MS);

  REQUIRE(WaitForLinuxFutexWaitMode(context.thread_id, LinuxFutexWaitMode::UNTIMED));
  ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);
  static const uint8_t EXPECTED[] = {0x71, 0x72, 0x73};
  ASSERT(std::memcmp(deferred, EXPECTED, sizeof(EXPECTED)) == 0);
  port.release.Post();

  ExpectWaitOk(publisher_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(publisher);
  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::TIMEOUT);
  ASSERT(writer_semaphore.Value() == 0U);

  deferred[0] = 0xEE;
  uint8_t queued[sizeof(deferred)]{};
  WriteInfoBlock deferred_info = PopRecord(port, queued);
  ASSERT(std::memcmp(queued, EXPECTED, sizeof(EXPECTED)) == 0);
  port.Finish(false, ErrorCode::OK, deferred_info);
  port.Finish(false, ErrorCode::OK, first_info);
  ASSERT(writer_semaphore.Value() == 0U);
}

void test_timeout_after_metadata_only_dequeue_cancels_waiting_request_without_copy()
{
  WritePort port(2, 4);
  port = PendingWriteFun;
  static const uint8_t A[] = {0x75, 0x76, 0x77, 0x78};
  uint8_t deferred[] = {0x85, 0x86};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext writer_context{&port, ConstRawData{deferred, sizeof(deferred)},
                                          1000, ErrorCode::FAILED, &writer_done};
  writer_context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, writer_context, "wr_cancel_publish");
  REQUIRE(WaitForLinuxFutexWaitMode(writer_context.thread_id, LinuxFutexWaitMode::TIMED));

  WriteInfoBlock first_info{};
  {
    auto dequeue = port.BeginDequeue(false);
    ASSERT(dequeue.PopInfo(first_info) == ErrorCode::OK);
    ASSERT(first_info.data.size_ == sizeof(A));
    ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);
  }

  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(writer_context.result == ErrorCode::TIMEOUT);
  ASSERT(writer_semaphore.Value() == 0U);
  deferred[0] = 0xEE;
  deferred[1] = 0xEF;
  ASSERT(writer_semaphore.Value() == 0U);
  ASSERT(port.QueueInfo()->Size() == 0U);
  ASSERT(port.Size() == sizeof(A));

  uint8_t first_data[sizeof(A)]{};
  {
    auto dequeue = port.BeginDequeue(false);
    ASSERT(dequeue.PopData(first_data, sizeof(first_data)) == ErrorCode::OK);
  }
  ASSERT(std::memcmp(first_data, A, sizeof(A)) == 0);
  port.Finish(false, ErrorCode::OK, first_info);
  ASSERT(writer_semaphore.Value() == 0U);
  ASSERT(port.QueueInfo()->Size() == 0U);
  ASSERT(port.Size() == 0U);
}

void test_old_detached_record_coexists_with_new_deferred_request()
{
  WritePort port(3, 8);
  port = PendingWriteFun;
  static const uint8_t A[] = {0x81, 0x82};
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
  ASSERT(port.QueueInfo()->Size() == 1U);

  uint8_t first_data[sizeof(A)]{};
  WriteInfoBlock first_info = PopRecord(port, first_data);
  port.Finish(false, ErrorCode::OK, first_info);
  REQUIRE(port.QueueInfo()->Size() == 1U);

  uint8_t second_data[sizeof(B)]{};
  WriteInfoBlock second_info = PopRecord(port, second_data);
  ASSERT(std::memcmp(second_data, B, sizeof(B)) == 0);
  port.Finish(false, ErrorCode::OK, second_info);
  ExpectWaitOk(second_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(second.result == ErrorCode::OK);
  ASSERT(first_semaphore.Value() == 0U);
  ASSERT(second_semaphore.Value() == 0U);
}

void test_deferred_publish_handles_synchronous_finish_and_return()
{
  for (const auto mode :
       {ControlledWritePort::Mode::FINISH, ControlledWritePort::Mode::RETURN})
  {
    for (const auto result : {ErrorCode::OK, ErrorCode::INIT_ERR})
    {
      ControlledWritePort port(3, 4);
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
      StartBlockingWriteCaller(writer, context, "wr_defer_sync");
      REQUIRE(WaitForLinuxFutexWait(context.thread_id));

      port.mode = mode;
      port.result = result;
      uint8_t first_data[sizeof(A)]{};
      WriteInfoBlock first_info = PopRecord(port, first_data);
      port.Finish(false, ErrorCode::OK, first_info);

      ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
      JoinThreadIfNeeded(writer);
      ASSERT(context.result == result);
      ASSERT(port.payload_size == sizeof(B));
      ASSERT(std::memcmp(port.payload, B, sizeof(B)) == 0);
      ASSERT(writer_semaphore.Value() == 0U);
      ASSERT(port.QueueInfo()->Size() == 0U);
      ASSERT(port.Size() == 0U);
    }
  }
}

void test_synchronous_finish_does_not_release_waiter_before_write_fun_returns()
{
  ControlledWritePort port(3, 4);
  static const uint8_t A[] = {0xB1, 0xB2, 0xB3, 0xB4};
  static const uint8_t B[] = {0xC1, 0xC2};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext context{&port, ConstRawData{B, sizeof(B)}, UINT32_MAX,
                                   ErrorCode::FAILED, &writer_done};
  context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, context, "wr_finish_staged");
  REQUIRE(WaitForLinuxFutexWait(context.thread_id));

  port.mode = ControlledWritePort::Mode::STAGED_FINISH;
  port.result = ErrorCode::INIT_ERR;
  uint8_t first_data[sizeof(A)]{};
  WriteInfoBlock first_info{};
  Semaphore publisher_done;
  Thread publisher;
  StartDequeueRecord(publisher, port, first_info, first_data, sizeof(first_data),
                     publisher_done, "wr_finish_owner");
  ExpectWaitOk(port.entered, THREAD_STATE_TIMEOUT_MS);
  ASSERT(std::memcmp(first_data, A, sizeof(A)) == 0);

  ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);
  ASSERT(publisher_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);
  port.release.Post();

  ExpectWaitOk(publisher_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(publisher);
  port.Finish(false, ErrorCode::OK, first_info);
  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::INIT_ERR);
  ASSERT(writer_semaphore.Value() == 0U);
  ASSERT(port.payload_size == sizeof(B));
  ASSERT(std::memcmp(port.payload, B, sizeof(B)) == 0);
  ASSERT(port.QueueInfo()->Size() == 0U);
  ASSERT(port.Size() == 0U);
}

void test_completion_claimed_before_wait_error_preserves_backend_result()
{
  ControlledWritePort port(3, 4);
  static const uint8_t A[] = {0xB4, 0xB5, 0xB6, 0xB7};
  static const uint8_t B[] = {0xC4, 0xC5};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore writer_done;
  Semaphore writer_semaphore;
  BlockingWriteCallContext context{&port, ConstRawData{B, sizeof(B)}, 100U,
                                   ErrorCode::FAILED, &writer_done};
  context.semaphore = &writer_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, context, "wr_claimed_timeout");
  REQUIRE(WaitForLinuxFutexWaitMode(context.thread_id, LinuxFutexWaitMode::TIMED));

  port.mode = ControlledWritePort::Mode::STAGED_FINISH;
  port.result = ErrorCode::INIT_ERR;
  uint8_t first_data[sizeof(A)]{};
  WriteInfoBlock first_info{};
  Semaphore publisher_done;
  Thread publisher;
  StartDequeueRecord(publisher, port, first_info, first_data, sizeof(first_data),
                     publisher_done, "wr_claimed_owner");
  ExpectWaitOk(port.entered, THREAD_STATE_TIMEOUT_MS);
  ASSERT(std::memcmp(first_data, A, sizeof(A)) == 0);

  REQUIRE(WaitForLinuxFutexWaitMode(context.thread_id, LinuxFutexWaitMode::UNTIMED));
  ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);
  port.release.Post();

  ExpectWaitOk(publisher_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(publisher);
  port.Finish(false, ErrorCode::OK, first_info);
  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::INIT_ERR);
  ASSERT(writer_semaphore.Value() == 0U);
  ASSERT(port.QueueInfo()->Size() == 0U);
  ASSERT(port.Size() == 0U);
}

void test_terminal_return_releases_owner_before_callback_reentry()
{
  TerminalReturnWritePort port;
  TerminalReturnReentry context{&port};
  auto callback = Callback<ErrorCode>::Create(OnTerminalReturnComplete, &context);
  WriteOperation operation(callback);
  static const uint8_t OUTER[] = {0x41, 0x42};

  ASSERT(port(ConstRawData{OUTER, sizeof(OUTER)}, operation) == ErrorCode::OK);
  ASSERT(context.completion_result == ErrorCode::OK);
  ASSERT(context.nested_result == ErrorCode::OK);
  ASSERT(port.call_count == 2U);
  ASSERT(port.QueueInfo()->Size() == 0U);
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
  ASSERT(context.port.QueueInfo()->Size() == 0U);
  ASSERT(context.port.Size() == 0U);
}

void test_terminal_handoff_holds_admission_until_waiter_consumes_result()
{
  ControlledWritePort port(3, 4);
  static const uint8_t A[] = {0xD1, 0xD2, 0xD3, 0xD4};
  static const uint8_t B[] = {0xE1, 0xE2};
  static const uint8_t C[] = {0xF1};
  WriteOperation first_operation;
  ASSERT(port(ConstRawData{A, sizeof(A)}, first_operation) == ErrorCode::OK);

  Semaphore writer_done;
  Semaphore reused_semaphore;
  BlockingWriteCallContext timed_out{&port, ConstRawData{B, sizeof(B)}, 1000,
                                     ErrorCode::FAILED, &writer_done};
  timed_out.semaphore = &reused_semaphore;
  Thread writer;
  StartBlockingWriteCaller(writer, timed_out, "wr_handoff_detach");
  REQUIRE(WaitForLinuxFutexWaitMode(timed_out.thread_id, LinuxFutexWaitMode::TIMED));

  port.mode = ControlledWritePort::Mode::STAGED_PENDING;
  uint8_t first_data[sizeof(A)]{};
  WriteInfoBlock first_info{};
  Semaphore publisher_done;
  Thread publisher;
  StartDequeueRecord(publisher, port, first_info, first_data, sizeof(first_data),
                     publisher_done, "wr_handoff_publish");
  ExpectWaitOk(port.entered, THREAD_STATE_TIMEOUT_MS);
  ASSERT(std::memcmp(first_data, A, sizeof(A)) == 0);
  REQUIRE(WaitForLinuxFutexWaitMode(timed_out.thread_id, LinuxFutexWaitMode::UNTIMED));

  ScopedThreadSignalPark parked_writer;
  REQUIRE(parked_writer.Park(timed_out.thread_id.load(std::memory_order_acquire)));
  port.release.Post();
  ExpectWaitOk(publisher_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(publisher);
  port.Finish(false, ErrorCode::OK, first_info);

  Semaphore blocked_semaphore;
  WriteOperation blocked_operation(blocked_semaphore, 0);
  ASSERT(port(ConstRawData{C, sizeof(C)}, blocked_operation) == ErrorCode::BUSY);
  ASSERT(blocked_semaphore.Value() == 0U);
  ASSERT(writer_done.Wait(SHORT_WAIT_MS) == ErrorCode::TIMEOUT);

  parked_writer.Release();
  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(timed_out.result == ErrorCode::TIMEOUT);
  ASSERT(reused_semaphore.Value() == 0U);

  Semaphore recovered_done;
  BlockingWriteCallContext recovered{&port, ConstRawData{C, sizeof(C)}, UINT32_MAX,
                                     ErrorCode::FAILED, &recovered_done};
  recovered.semaphore = &reused_semaphore;
  Thread recovered_writer;
  StartBlockingWriteCaller(recovered_writer, recovered, "wr_handoff_reuse");
  REQUIRE(WaitForLinuxFutexWait(recovered.thread_id));

  port.mode = ControlledWritePort::Mode::PENDING;
  uint8_t timed_out_data[sizeof(B)]{};
  WriteInfoBlock timed_out_info = PopRecord(port, timed_out_data);
  ASSERT(std::memcmp(timed_out_data, B, sizeof(B)) == 0);
  port.Finish(false, ErrorCode::OK, timed_out_info);

  uint8_t recovered_data[sizeof(C)]{};
  WriteInfoBlock recovered_info = PopRecord(port, recovered_data);
  ASSERT(std::memcmp(recovered_data, C, sizeof(C)) == 0);
  port.Finish(false, ErrorCode::INIT_ERR, recovered_info);

  ExpectWaitOk(recovered_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(recovered_writer);
  ASSERT(recovered.result == ErrorCode::INIT_ERR);
  ASSERT(reused_semaphore.Value() == 0U);
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

  uint8_t first_data[sizeof(A)]{};
  WriteInfoBlock first_info = PopRecord(port, first_data);
  port.Finish(false, ErrorCode::OK, first_info);
  uint8_t second_data[sizeof(B)]{};
  WriteInfoBlock second_info = PopRecord(port, second_data);
  port.Finish(false, ErrorCode::OK, second_info);
  ExpectWaitOk(writer_done, THREAD_STATE_TIMEOUT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(context.result == ErrorCode::OK);
  ASSERT(writer_semaphore.Value() == 0U);
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
  ASSERT(port.QueueInfo()->Size() == 1U);
  ASSERT(port.Size() == sizeof(A));

  uint8_t first_data[sizeof(A)]{};
  WriteInfoBlock first_info = PopRecord(port, first_data);
  port.Finish(false, ErrorCode::OK, first_info);
  ASSERT(stream_semaphore.Value() == 0U);
  ASSERT(port.QueueInfo()->Size() == 0U);
  ASSERT(port.Size() == 0U);

  static const uint8_t FRESH[] = {0xA1, 0xA2};
  WriteOperation fresh_operation;
  WritePort::Stream fresh(&port, fresh_operation);
  ASSERT(fresh.Write(ConstRawData{FRESH, sizeof(FRESH)}) == ErrorCode::OK);
  ASSERT(fresh.Commit() == ErrorCode::OK);
  uint8_t fresh_data[sizeof(FRESH)]{};
  WriteInfoBlock fresh_info = PopRecord(port, fresh_data);
  ASSERT(std::memcmp(fresh_data, FRESH, sizeof(FRESH)) == 0);
  port.Finish(false, ErrorCode::OK, fresh_info);
  ASSERT(stream_semaphore.Value() == 0U);
  ASSERT(port.QueueInfo()->Size() == 0U);
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
  test_capacity_progress_carrier_survives_return_to_waiting_cas();
  test_progress_during_deferred_slot_registration_is_closed_after_publication();
  test_progress_racing_deferred_admission_is_not_lost();
  test_deferred_write_remains_pending_until_enough_space_is_released();
  test_timeout_before_publish_claim_cancels_without_copy();
  test_publish_claim_before_timeout_preserves_buffer_until_handoff();
  test_timeout_after_metadata_only_dequeue_cancels_waiting_request_without_copy();
  test_old_detached_record_coexists_with_new_deferred_request();
  test_deferred_publish_handles_synchronous_finish_and_return();
  test_synchronous_finish_does_not_release_waiter_before_write_fun_returns();
  test_completion_claimed_before_wait_error_preserves_backend_result();
  test_terminal_return_releases_owner_before_callback_reentry();
  test_guarded_completion_replay_submits_after_owner_release();
  test_terminal_handoff_holds_admission_until_waiter_consumes_result();
  test_deferred_slot_rejects_other_producers_and_validates_input();
  test_stream_space_failure_never_defers_caller_data();
  test_pipe_consumer_progress_promotes_deferred_block();
  test_pipe_clear_promotes_deferred_block();
}
