#include <atomic>
#include <cstring>
#include <vector>

#if defined(LIBXR_SYSTEM_POSIX_HOST)
#include <pthread.h>
#endif

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "libxr_rw.hpp"
#include "test.hpp"

namespace LibXRTest
{

inline constexpr uint32_t ASYNC_TIMEOUT_MS = 200;
inline constexpr uint32_t SHORT_WAIT_MS = 100;

enum class TestMode : uint8_t
{
  NONE,
  POLLING,
  CALLBACK,
  BLOCK
};

inline constexpr TestMode ALL_MODES[] = {TestMode::NONE, TestMode::POLLING,
                                         TestMode::CALLBACK, TestMode::BLOCK};

struct CompletionProbe
{
  std::atomic<uint32_t> count{0};
  std::atomic<int> last{static_cast<int>(LibXR::ErrorCode::OK)};
  LibXR::Semaphore sem;

  CompletionProbe() : sem(0) {}

  void Reset()
  {
    count.store(0, std::memory_order_release);
    last.store(static_cast<int>(LibXR::ErrorCode::OK), std::memory_order_release);
  }
};

template <typename Op>
struct ModeHarness
{
  using CallbackType = typename Op::Callback;
  using PollingStatus = typename Op::OperationPollingStatus;

  explicit ModeHarness(TestMode mode, uint32_t timeout = ASYNC_TIMEOUT_MS)
      : mode(mode), callback(CallbackType::Create(OnCallback, this)), sem(0), op()
  {
    Bind(timeout);
    Reset();
  }

  void Reset()
  {
    polling_status = PollingStatus::READY;
    probe.Reset();
  }

  void ExpectFinal(LibXR::ErrorCode expected)
  {
    switch (mode)
    {
      case TestMode::NONE:
        return;
      case TestMode::POLLING:
        ASSERT(polling_status == ((expected == LibXR::ErrorCode::OK)
                                      ? PollingStatus::DONE
                                      : PollingStatus::ERROR));
        return;
      case TestMode::CALLBACK:
        ASSERT(probe.sem.Wait(ASYNC_TIMEOUT_MS) == LibXR::ErrorCode::OK);
        ASSERT(probe.count.load(std::memory_order_acquire) == 1);
        ASSERT(static_cast<LibXR::ErrorCode>(
                   probe.last.load(std::memory_order_acquire)) == expected);
        return;
      case TestMode::BLOCK:
        return;
    }
  }

  static void OnCallback(bool in_isr, ModeHarness* self, LibXR::ErrorCode status)
  {
    self->probe.last.store(static_cast<int>(status), std::memory_order_release);
    self->probe.count.fetch_add(1, std::memory_order_acq_rel);
    self->probe.sem.PostFromCallback(in_isr);
  }

  void Bind(uint32_t timeout)
  {
    switch (mode)
    {
      case TestMode::NONE:
        op = Op();
        return;
      case TestMode::POLLING:
        op = Op(polling_status);
        return;
      case TestMode::CALLBACK:
        op = Op(callback);
        return;
      case TestMode::BLOCK:
        op = Op(sem, timeout);
        return;
    }
  }

  TestMode mode;
  PollingStatus polling_status = PollingStatus::READY;
  CompletionProbe probe;
  CallbackType callback;
  LibXR::Semaphore sem;
  Op op;
};

using ReadHarness = ModeHarness<LibXR::ReadOperation>;
using WriteHarness = ModeHarness<LibXR::WriteOperation>;

inline void JoinThreadIfNeeded(LibXR::Thread& thread)
{
#if defined(LIBXR_SYSTEM_POSIX_HOST)
  pthread_join(thread, nullptr);
#else
  UNUSED(thread);
#endif
}

inline void ExpectWaitOk(LibXR::Semaphore& sem, uint32_t timeout = ASYNC_TIMEOUT_MS)
{
  ASSERT(sem.Wait(timeout) == LibXR::ErrorCode::OK);
}

}  // namespace LibXRTest

namespace
{
using LibXRTest::ALL_MODES;
using LibXRTest::ASYNC_TIMEOUT_MS;
using LibXRTest::ExpectWaitOk;
using LibXRTest::JoinThreadIfNeeded;
using LibXRTest::ReadHarness;
using LibXRTest::SHORT_WAIT_MS;
using LibXRTest::TestMode;
using LibXRTest::WriteHarness;

LibXR::ErrorCode PendingWriteFun(LibXR::WritePort&, bool)
{
  return LibXR::ErrorCode::PENDING;
}

LibXR::ErrorCode PendingReadFun(LibXR::ReadPort&, bool)
{
  return LibXR::ErrorCode::PENDING;
}

struct ReadQueueCompletionContext
{
  LibXR::ReadPort* port;
  LibXR::Semaphore* done;
  const uint8_t* data;
  size_t size;
};

struct TrackingReadPort : LibXR::ReadPort
{
  using LibXR::ReadPort::ReadPort;
  using LibXR::ReadPort::operator=;

  void OnRxDequeue(bool) override { dequeue_count++; }

  uint32_t dequeue_count = 0;
};

void CompletePendingReadFromQueue(ReadQueueCompletionContext ctx)
{
  while (ctx.port->busy_.load(std::memory_order_acquire) !=
         LibXR::ReadPort::BusyState::PENDING)
  {
    LibXR::Thread::Yield();
  }

  auto ans = ctx.port->queue_data_->PushBatch(ctx.data, ctx.size);
  UNUSED(ans);
  ASSERT(ans == LibXR::ErrorCode::OK);
  ctx.port->ProcessPendingReads(false);
  ctx.done->Post();
}

struct WriteFinishContext
{
  LibXR::WritePort* port;
  LibXR::Semaphore* done;
  LibXR::ErrorCode result;
};

void FinishPendingWrite(WriteFinishContext ctx)
{
  LibXR::WriteInfoBlock completed{};

  while (ctx.port->queue_info_->Pop(completed) != LibXR::ErrorCode::OK)
  {
    LibXR::Thread::Yield();
  }

  ctx.port->Finish(false, ctx.result, completed);
  ctx.done->Post();
}

struct BlockingReadCallContext
{
  LibXR::ReadPort* port;
  LibXR::RawData data;
  uint32_t timeout_ms;
  LibXR::ErrorCode result;
  LibXR::Semaphore* done;
};

void BlockingReadCall(BlockingReadCallContext* ctx)
{
  LibXR::Semaphore sem(0);
  LibXR::ReadOperation op(sem, ctx->timeout_ms);
  ctx->result = (*ctx->port)(ctx->data, op);
  ctx->done->Post();
}

struct BlockingWriteCallContext
{
  LibXR::WritePort* port;
  LibXR::ConstRawData data;
  uint32_t timeout_ms;
  LibXR::ErrorCode result;
  LibXR::Semaphore* done;
};

void BlockingWriteCall(BlockingWriteCallContext* ctx)
{
  LibXR::Semaphore sem(0);
  LibXR::WriteOperation op(sem, ctx->timeout_ms);
  ctx->result = (*ctx->port)(ctx->data, op);
  ctx->done->Post();
}

void StartReadQueueCompleter(LibXR::Thread& thread, LibXR::ReadPort& port,
                             LibXR::Semaphore& done, const uint8_t* data, size_t size,
                             const char* name)
{
  thread.Create(ReadQueueCompletionContext{&port, &done, data, size},
                CompletePendingReadFromQueue, name, 1024,
                LibXR::Thread::Priority::MEDIUM);
}

void StartWriteFinisher(LibXR::Thread& thread, LibXR::WritePort& port,
                        LibXR::Semaphore& done, LibXR::ErrorCode result, const char* name)
{
  thread.Create(WriteFinishContext{&port, &done, result}, FinishPendingWrite, name, 1024,
                LibXR::Thread::Priority::MEDIUM);
}

void StartBlockingReadCaller(LibXR::Thread& thread, BlockingReadCallContext& ctx,
                             const char* name)
{
  thread.Create<BlockingReadCallContext*>(&ctx, BlockingReadCall, name, 1024,
                                          LibXR::Thread::Priority::MEDIUM);
}

void StartBlockingWriteCaller(LibXR::Thread& thread, BlockingWriteCallContext& ctx,
                              const char* name)
{
  thread.Create<BlockingWriteCallContext*>(&ctx, BlockingWriteCall, name, 1024,
                                           LibXR::Thread::Priority::MEDIUM);
}

enum class StreamSubmitMode : uint8_t
{
  COMMIT,
  DESTRUCT
};

void VerifyStreamBlockPendingCompletion(LibXR::ErrorCode finish_result,
                                        LibXR::ErrorCode expected_result,
                                        StreamSubmitMode submit_mode)
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x41, 0x42, 0x43, 0x44};
  Semaphore sem;
  WriteOperation op(sem, SHORT_WAIT_MS);
  Semaphore done;
  Thread finisher;
  StartWriteFinisher(
      finisher, w, done, finish_result,
      (submit_mode == StreamSubmitMode::COMMIT) ? "wr_stream_commit" : "wr_stream_dtor");

  {
    WritePort::Stream ws(&w, op);
    ws << ConstRawData{TX, sizeof(TX)};
    if (submit_mode == StreamSubmitMode::COMMIT)
    {
      auto ec = ws.Commit();
      ASSERT(ec == expected_result);
    }
  }

  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
  ASSERT(sem.Value() == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

void VerifyStreamBlockTimeout()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x51, 0x52, 0x53};
  Semaphore sem;
  WriteOperation op(sem, 0);
  WritePort::Stream ws(&w, op);
  ws << ConstRawData{TX, sizeof(TX)};

  auto ec = ws.Commit();
  ASSERT(ec == ErrorCode::TIMEOUT);
  ASSERT(sem.Value() == 0);

  WriteInfoBlock completed{};
  ASSERT(w.queue_info_->Pop(completed) == ErrorCode::OK);
  w.Finish(false, ErrorCode::OK, completed);

  ASSERT(sem.Value() == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

void test_rw_stream_block_pending_result_propagates()
{
  VerifyStreamBlockPendingCompletion(LibXR::ErrorCode::FAILED, LibXR::ErrorCode::FAILED,
                                     StreamSubmitMode::COMMIT);
}

void test_rw_stream_block_timeout_detaches_waiter() { VerifyStreamBlockTimeout(); }

void test_rw_stream_block_destructor_autocommit()
{
  VerifyStreamBlockPendingCompletion(LibXR::ErrorCode::OK, LibXR::ErrorCode::OK,
                                     StreamSubmitMode::DESTRUCT);
}

void test_rw_block_read_timeout_detaches_pending()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  uint8_t timed_out_rx[4] = {0xA1, 0xA2, 0xA3, 0xA4};
  Semaphore sem;
  ReadOperation block_op(sem, 0);

  auto ec = r(RawData{timed_out_rx, sizeof(timed_out_rx)}, block_op);
  ASSERT(ec == ErrorCode::TIMEOUT);

  static const uint8_t STALE_EXPECT[] = {0xA1, 0xA2, 0xA3, 0xA4};
  ASSERT(std::memcmp(timed_out_rx, STALE_EXPECT, sizeof(STALE_EXPECT)) == 0);

  static const uint8_t TX[] = {0x10, 0x20, 0x30, 0x40};
  WriteOperation wop;
  ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::OK);
  r.ProcessPendingReads(false);

  ASSERT(std::memcmp(timed_out_rx, STALE_EXPECT, sizeof(STALE_EXPECT)) == 0);
  ASSERT(sem.Value() == 0);

  uint8_t fresh_rx[sizeof(TX)] = {0};
  ReadOperation rop;
  ec = r(RawData{fresh_rx, sizeof(fresh_rx)}, rop);
  ASSERT(ec == ErrorCode::OK);
  ASSERT(std::memcmp(fresh_rx, TX, sizeof(TX)) == 0);
}

void test_rw_zero_read_pending_notifies_without_dequeue()
{
  using namespace LibXR;

  for (auto mode : ALL_MODES)
  {
    TrackingReadPort r(16);
    r = PendingReadFun;

    uint8_t dummy = 0xA0;
    ReadHarness read(mode);
    Semaphore done;
    Thread finisher;

    static const uint8_t TX[] = {0x31, 0x32};
    StartReadQueueCompleter(finisher, r, done, TX, sizeof(TX), "rd_zero_ready");

    auto ec = r(RawData{&dummy, 0}, read.op);
    ASSERT(ec == ErrorCode::OK);
    ExpectWaitOk(done, SHORT_WAIT_MS);
    JoinThreadIfNeeded(finisher);

    if (mode != TestMode::NONE && mode != TestMode::BLOCK)
    {
      read.ExpectFinal(ErrorCode::OK);
    }

    ASSERT(dummy == 0xA0);
    ASSERT(r.dequeue_count == 0);
    ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
    ASSERT(r.Size() == sizeof(TX));

    uint8_t follow_up[sizeof(TX)] = {};
    ReadOperation follow_op;
    ec = r(RawData{follow_up, sizeof(follow_up)}, follow_op);
    ASSERT(ec == ErrorCode::OK);
    ASSERT(std::memcmp(follow_up, TX, sizeof(TX)) == 0);
    ASSERT(r.dequeue_count == 1);
  }
}

void test_rw_block_write_timeout_detaches_waiter()
{
  using namespace LibXR;

  WritePort w(2, 64);
  w = PendingWriteFun;

  static const uint8_t TX1[] = {1, 2, 3};
  static const uint8_t TX2[] = {4, 5, 6};

  Semaphore sem1;
  WriteOperation op1(sem1, 0);
  auto ec = w(ConstRawData{TX1, sizeof(TX1)}, op1);
  ASSERT(ec == ErrorCode::TIMEOUT);
  ASSERT(sem1.Value() == 0);

  Semaphore sem2;
  WriteOperation op2(sem2, 0);
  ec = w(ConstRawData{TX2, sizeof(TX2)}, op2);
  ASSERT(ec == ErrorCode::BUSY);
  ASSERT(sem2.Value() == 0);

  WriteInfoBlock completed{};
  ASSERT(w.queue_info_->Pop(completed) == ErrorCode::OK);
  w.Finish(false, ErrorCode::OK, completed);
  ASSERT(sem1.Value() == 0);

  ec = w(ConstRawData{TX2, sizeof(TX2)}, op2);
  ASSERT(ec == ErrorCode::TIMEOUT);
  ASSERT(sem2.Value() == 0);
}

void test_rw_read_port_fail_and_clear_all_fails_block_waiter()
{
  using namespace LibXR;

  ReadPort r(16);
  r = PendingReadFun;

  uint8_t stale_rx[1] = {0xA5};
  Semaphore done;
  BlockingReadCallContext ctx{&r, RawData{stale_rx, sizeof(stale_rx)}, 20,
                              ErrorCode::FAILED, &done};
  Thread reader;
  StartBlockingReadCaller(reader, ctx, "rd_reset");

  while (r.busy_.load(std::memory_order_acquire) != ReadPort::BusyState::PENDING)
  {
    Thread::Yield();
  }

  r.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(reader);
  ASSERT(ctx.result == ErrorCode::INIT_ERR);
  ASSERT(stale_rx[0] == 0xA5);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
  ASSERT(r.Size() == 0);

  uint8_t tx = 0x5A;
  ASSERT(r.queue_data_->PushBatch(&tx, 1) == ErrorCode::OK);

  uint8_t fresh_rx[1] = {0};
  ReadOperation fresh_op;
  ASSERT(r(RawData{fresh_rx, sizeof(fresh_rx)}, fresh_op) == ErrorCode::OK);
  ASSERT(fresh_rx[0] == tx);
}

void test_rw_write_port_fail_and_clear_all_fails_block_waiter()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX1[] = {0x11, 0x22, 0x33};
  static const uint8_t TX2[] = {0x44, 0x55, 0x66};

  Semaphore done;
  BlockingWriteCallContext ctx{&w, ConstRawData{TX1, sizeof(TX1)}, 20, ErrorCode::FAILED,
                               &done};
  Thread writer;
  StartBlockingWriteCaller(writer, ctx, "wr_reset");

  while (w.busy_.load(std::memory_order_acquire) != WritePort::BusyState::BLOCK_WAITING)
  {
    Thread::Yield();
  }

  w.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(writer);
  ASSERT(ctx.result == ErrorCode::INIT_ERR);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
  ASSERT(w.Size() == 0);
  ASSERT(w.queue_info_->Size() == 0);

  Semaphore finish_done;
  Thread finisher;
  StartWriteFinisher(finisher, w, finish_done, ErrorCode::OK, "wr_reset_finish");

  Semaphore sem;
  WriteOperation op(sem, 100);
  ASSERT(w(ConstRawData{TX2, sizeof(TX2)}, op) == ErrorCode::OK);
  ExpectWaitOk(finish_done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
}

void test_rw_read_port_block_queue_completion_copies_data()
{
  using namespace LibXR;

  ReadPort r(16);
  r = PendingReadFun;

  static const uint8_t TX[] = {0x5A};
  uint8_t rx[sizeof(TX)] = {0};
  Semaphore sem;
  ReadOperation op(sem, 100);
  Semaphore done;
  Thread finisher;
  StartReadQueueCompleter(finisher, r, done, TX, sizeof(TX), "rd_queue_block");

  auto ec = r(RawData{rx, sizeof(rx)}, op);
  ASSERT(ec == ErrorCode::OK);
  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
  ASSERT(sem.Value() == 0);
}

void test_rw_write_port_block_pending_result_propagates()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x5A};
  Semaphore sem;
  WriteOperation op(sem, 100);
  Semaphore done;
  Thread finisher;
  StartWriteFinisher(finisher, w, done, ErrorCode::FAILED, "wr_finish");

  auto ec = w(ConstRawData{TX, sizeof(TX)}, op);
  ASSERT(ec == ErrorCode::FAILED);
  ExpectWaitOk(done, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher);
}

void test_rw_write_port_block_reused_waiter_discards_stale_signal()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX1[] = {0x6B};
  static const uint8_t TX2[] = {0x7C};
  Semaphore sem;
  WriteOperation op(sem, 100);
  Semaphore done1;
  Thread finisher1;
  StartWriteFinisher(finisher1, w, done1, ErrorCode::FAILED, "wr_stale1");

  auto ec = w(ConstRawData{TX1, sizeof(TX1)}, op);
  ASSERT(ec == ErrorCode::FAILED);
  ExpectWaitOk(done1, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher1);
  ASSERT(sem.Value() == 0);

  Semaphore done2;
  Thread finisher2;
  StartWriteFinisher(finisher2, w, done2, ErrorCode::OK, "wr_stale2");

  ec = w(ConstRawData{TX2, sizeof(TX2)}, op);
  ASSERT(ec == ErrorCode::OK);
  ExpectWaitOk(done2, SHORT_WAIT_MS);
  JoinThreadIfNeeded(finisher2);
  ASSERT(sem.Value() == 0);
}

struct DelayedPipeWriteContext
{
  LibXR::WritePort* port;
  WriteHarness* harness;
  const uint8_t* data;
  size_t size;
  uint32_t delay_ms;
  LibXR::ErrorCode result;
  LibXR::Semaphore* done;
};

void DelayedPipeWrite(DelayedPipeWriteContext* ctx)
{
  LibXR::Thread::Sleep(ctx->delay_ms);
  ctx->result = (*ctx->port)(LibXR::ConstRawData{ctx->data, ctx->size}, ctx->harness->op);
  ctx->done->Post();
}

void StartDelayedPipeWriter(LibXR::Thread& thread, DelayedPipeWriteContext& ctx,
                            const char* name)
{
  thread.Create<DelayedPipeWriteContext*>(&ctx, DelayedPipeWrite, name, 1024,
                                          LibXR::Thread::Priority::MEDIUM);
}

void FillPattern(std::vector<uint8_t>& buffer, uint8_t seed)
{
  for (size_t i = 0; i < buffer.size(); ++i)
  {
    buffer[i] = static_cast<uint8_t>(seed + i * 17u + (i % 5u));
  }
}

template <typename Harness>
void ExpectCallResult(Harness& harness, LibXR::ErrorCode call_result,
                      LibXR::ErrorCode final_result)
{
  if (harness.mode == TestMode::BLOCK)
  {
    ASSERT(call_result == final_result);
  }
  else
  {
    ASSERT(call_result == LibXR::ErrorCode::OK);
    harness.ExpectFinal(final_result);
  }
}

void test_pipe_stream_block_immediate_path()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  uint8_t rx[8] = {0};
  ReadOperation rop;
  ASSERT(r(RawData{rx, sizeof(rx)}, rop) == ErrorCode::OK);

  Semaphore sem;
  WriteOperation wop(sem, 100);
  WritePort::Stream ws(&w, wop);
  static const uint8_t A[] = {0x21, 0x22, 0x23};
  static const uint8_t B[] = {0x31, 0x32, 0x33, 0x34, 0x35};
  ws << ConstRawData{A, sizeof(A)} << ConstRawData{B, sizeof(B)};

  auto ec = ws.Commit();
  ASSERT(ec == ErrorCode::OK);

  static const uint8_t EXPECT[] = {0x21, 0x22, 0x23, 0x31, 0x32, 0x33, 0x34, 0x35};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
  ASSERT(sem.Value() == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

void test_pipe_block_reuse_stress()
{
  using namespace LibXR;

  constexpr size_t PIPE_CAPACITY = 64;
  constexpr size_t BLOCK_STRESS_ITERATIONS = 8;

  Pipe pipe(PIPE_CAPACITY);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  ReadHarness read(TestMode::BLOCK);
  WriteHarness write(TestMode::BLOCK);

  for (size_t iter = 0; iter < BLOCK_STRESS_ITERATIONS; ++iter)
  {
    const size_t size = 1 + (iter % 15);
    std::vector<uint8_t> tx(size);
    std::vector<uint8_t> rx(size, 0x88);
    FillPattern(tx, static_cast<uint8_t>(0x90 + iter));

    read.Reset();
    write.Reset();

    if ((iter & 1u) == 0)
    {
      Semaphore write_done;
      DelayedPipeWriteContext ctx{
          &w, &write, tx.data(), tx.size(), 5, ErrorCode::FAILED, &write_done};
      Thread writer;
      StartDelayedPipeWriter(writer, ctx, "pipe_block_async");

      ASSERT(r(RawData{rx.data(), rx.size()}, read.op) == ErrorCode::OK);
      ExpectWaitOk(write_done);
      JoinThreadIfNeeded(writer);
      ExpectCallResult(write, ctx.result, ErrorCode::OK);
    }
    else
    {
      ExpectCallResult(write, w(ConstRawData{tx.data(), tx.size()}, write.op),
                       ErrorCode::OK);
      ExpectCallResult(read, r(RawData{rx.data(), rx.size()}, read.op), ErrorCode::OK);
    }

    ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
    ASSERT(r.Size() == 0);
    ASSERT(w.Size() == 0);
  }
}

}  // namespace

void test_rw_runtime()
{
  test_rw_stream_block_pending_result_propagates();
  test_rw_stream_block_timeout_detaches_waiter();
  test_rw_stream_block_destructor_autocommit();
  test_rw_block_read_timeout_detaches_pending();
  test_rw_zero_read_pending_notifies_without_dequeue();
  test_rw_block_write_timeout_detaches_waiter();
  test_rw_read_port_fail_and_clear_all_fails_block_waiter();
  test_rw_write_port_fail_and_clear_all_fails_block_waiter();
  test_rw_read_port_block_queue_completion_copies_data();
  test_rw_write_port_block_pending_result_propagates();
  test_rw_write_port_block_reused_waiter_discards_stale_signal();
}

void test_pipe_runtime()
{
  test_pipe_stream_block_immediate_path();
  test_pipe_block_reuse_stress();
}
