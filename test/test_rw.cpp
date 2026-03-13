#include <atomic>
#include <cstring>
#include <vector>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "libxr_rw.hpp"
#include "test.hpp"

namespace
{
constexpr uint32_t kAsyncTimeoutMs = 200;
constexpr uint32_t kShortWaitMs = 100;

enum class TestMode : uint8_t
{
  NONE,
  POLLING,
  CALLBACK,
  BLOCK
};

constexpr TestMode kAsyncModes[] = {TestMode::NONE, TestMode::POLLING,
                                    TestMode::CALLBACK};

LibXR::ErrorCode PendingWriteFun(LibXR::WritePort&, bool)
{
  return LibXR::ErrorCode::PENDING;
}

LibXR::ErrorCode PendingReadFun(LibXR::ReadPort&, bool)
{
  return LibXR::ErrorCode::PENDING;
}

LibXR::ErrorCode FailWriteFun(LibXR::WritePort&, bool)
{
  return LibXR::ErrorCode::INIT_ERR;
}

LibXR::ErrorCode FailReadFun(LibXR::ReadPort&, bool)
{
  return LibXR::ErrorCode::INIT_ERR;
}

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

  explicit ModeHarness(TestMode mode, uint32_t timeout = kAsyncTimeoutMs)
      : mode(mode), callback(CallbackType::Create(OnCallback, this)), sem(0), op()
  {
    Bind(timeout);
    Reset();
  }

  ModeHarness(const ModeHarness&) = delete;
  ModeHarness& operator=(const ModeHarness&) = delete;

  void Reset()
  {
    polling_status = PollingStatus::READY;
    probe.Reset();
  }

  void ExpectPendingSubmitted() const
  {
    if (mode == TestMode::POLLING)
    {
      ASSERT(polling_status == PollingStatus::RUNNING);
    }
    else if (mode == TestMode::CALLBACK)
    {
      ASSERT(probe.count.load(std::memory_order_acquire) == 0);
    }
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
        ASSERT(probe.sem.Wait(kAsyncTimeoutMs) == LibXR::ErrorCode::OK);
        ASSERT(probe.count.load(std::memory_order_acquire) == 1);
        ASSERT(static_cast<LibXR::ErrorCode>(
                   probe.last.load(std::memory_order_acquire)) == expected);
        return;
      case TestMode::BLOCK:
        ASSERT(false);
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

void JoinThreadIfNeeded(LibXR::Thread& thread)
{
#if defined(LIBXR_SYSTEM_Linux) || defined(LIBXR_SYSTEM_Webots)
  pthread_join(thread, nullptr);
#else
  UNUSED(thread);
#endif
}

struct ReadFinishContext
{
  LibXR::ReadPort* port;
  LibXR::Semaphore* done;
  LibXR::ErrorCode result;
};

void FinishPendingRead(ReadFinishContext ctx)
{
  while (ctx.port->busy_.load(std::memory_order_acquire) !=
         LibXR::ReadPort::BusyState::PENDING)
  {
    LibXR::Thread::Yield();
  }

  ctx.port->Finish(false, ctx.result, ctx.port->info_);
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

struct BlockingWriteExternalOpCallContext
{
  LibXR::WritePort* port;
  LibXR::ConstRawData data;
  LibXR::WriteOperation* op;
  LibXR::ErrorCode result;
  LibXR::Semaphore* done;
};

void BlockingWriteExternalOpCall(BlockingWriteExternalOpCallContext* ctx)
{
  ctx->result = (*ctx->port)(ctx->data, *ctx->op);
  ctx->done->Post();
}

void ExpectWaitOk(LibXR::Semaphore& sem, uint32_t timeout = kAsyncTimeoutMs)
{
  ASSERT(sem.Wait(timeout) == LibXR::ErrorCode::OK);
}

void StartReadFinisher(LibXR::Thread& thread, LibXR::ReadPort& port,
                       LibXR::Semaphore& done, LibXR::ErrorCode result, const char* name)
{
  thread.Create(ReadFinishContext{&port, &done, result}, FinishPendingRead, name, 1024,
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

void StartBlockingWriteExternalOpCaller(LibXR::Thread& thread,
                                        BlockingWriteExternalOpCallContext& ctx,
                                        const char* name)
{
  thread.Create<BlockingWriteExternalOpCallContext*>(
      &ctx, BlockingWriteExternalOpCall, name, 1024, LibXR::Thread::Priority::MEDIUM);
}

void VerifyPendingReadMode(TestMode mode, LibXR::ErrorCode result)
{
  using namespace LibXR;

  ReadPort r(16);
  r = PendingReadFun;

  std::vector<uint8_t> rx(4, 0x7A);
  ReadHarness read(mode);
  Semaphore done;
  Thread finisher;
  StartReadFinisher(finisher, r, done, result, "rd_finish");

  auto call_result = r(RawData{rx.data(), rx.size()}, read.op);

  if (mode == TestMode::BLOCK)
  {
    ASSERT(call_result == result);
  }
  else
  {
    ASSERT(call_result == ErrorCode::OK);
    read.ExpectPendingSubmitted();
  }

  ExpectWaitOk(done);
  JoinThreadIfNeeded(finisher);
  if (mode != TestMode::NONE && mode != TestMode::BLOCK)
  {
    read.ExpectFinal(result);
  }
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

void VerifyPendingWriteMode(TestMode mode, LibXR::ErrorCode result)
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  std::vector<uint8_t> tx = {0x31, 0x41, 0x59, 0x26};
  WriteHarness write(mode);
  Semaphore done;
  Thread finisher;
  StartWriteFinisher(finisher, w, done, result, "wr_finish");

  auto call_result = w(ConstRawData{tx.data(), tx.size()}, write.op);

  if (mode == TestMode::BLOCK)
  {
    ASSERT(call_result == result);
  }
  else
  {
    ASSERT(call_result == ErrorCode::OK);
  }

  ExpectWaitOk(done);
  JoinThreadIfNeeded(finisher);
  if (mode != TestMode::NONE && mode != TestMode::BLOCK)
  {
    write.ExpectFinal(result);
  }
  ASSERT(w.queue_info_->Size() == 0);
}

void VerifyZeroWriteMode(TestMode mode)
{
  using namespace LibXR;

  Pipe pipe(8);
  WritePort& w = pipe.GetWritePort();
  ReadPort& r = pipe.GetReadPort();

  WriteHarness write(mode);
  auto write_result = w(ConstRawData{nullptr, 0}, write.op);
  if (mode == TestMode::BLOCK)
  {
    ASSERT(write_result == ErrorCode::OK);
  }
  else
  {
    ASSERT(write_result == ErrorCode::OK);
    write.ExpectFinal(ErrorCode::OK);
  }
  ASSERT(w.Size() == 0);

  uint8_t tx = 0x5A;
  uint8_t rx = 0;
  WriteOperation plain_write;
  ReadOperation plain_read;
  ASSERT(w(ConstRawData{&tx, 1}, plain_write) == ErrorCode::OK);
  ASSERT(r(RawData{&rx, 1}, plain_read) == ErrorCode::OK);
  ASSERT(rx == tx);
}

void VerifyZeroReadMode(TestMode mode)
{
  using namespace LibXR;

  Pipe pipe(8);
  WritePort& w = pipe.GetWritePort();
  ReadPort& r = pipe.GetReadPort();

  uint8_t tx = 0xA7;
  WriteOperation write_op;
  ASSERT(w(ConstRawData{&tx, 1}, write_op) == ErrorCode::OK);

  uint8_t dummy = 0x11;
  ReadHarness read(mode);
  auto zero_result = r(RawData{&dummy, 0}, read.op);
  if (mode == TestMode::BLOCK)
  {
    ASSERT(zero_result == ErrorCode::OK);
  }
  else
  {
    ASSERT(zero_result == ErrorCode::OK);
    read.ExpectFinal(ErrorCode::OK);
  }

  uint8_t rx = 0;
  ReadOperation plain_read;
  ASSERT(r(RawData{&rx, 1}, plain_read) == ErrorCode::OK);
  ASSERT(rx == tx);
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
  WriteOperation op(sem, kShortWaitMs);
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

  ExpectWaitOk(done, kShortWaitMs);
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
}  // namespace

void test_rw_pending_mode_matrix()
{
  for (auto mode : kAsyncModes)
  {
    VerifyPendingReadMode(mode, LibXR::ErrorCode::FAILED);
    VerifyPendingWriteMode(mode, LibXR::ErrorCode::FAILED);
  }
}

void test_rw_edge_cases()
{
  using namespace LibXR;

  for (auto mode : kAsyncModes)
  {
    VerifyZeroWriteMode(mode);
    VerifyZeroReadMode(mode);
  }

  WritePort w(1, 4);
  w = PendingWriteFun;
  const uint8_t tx2[] = {5};
  WriteOperation op1;
  WriteOperation op2;
  std::vector<uint8_t> tx1(w.EmptySize(), 0x3C);

  ASSERT(!tx1.empty());
  ASSERT(w(ConstRawData{tx1.data(), tx1.size()}, op1) == ErrorCode::OK);
  auto second_result = w(ConstRawData{tx2, sizeof(tx2)}, op2);
  ASSERT(second_result == ErrorCode::FULL);

  WriteInfoBlock completed{};
  ASSERT(w.queue_info_->Pop(completed) == ErrorCode::OK);
  w.Finish(false, ErrorCode::OK, completed);
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

void test_rw_block_immediate_error_propagates()
{
  using namespace LibXR;

  ReadPort r(16);
  r = FailReadFun;

  uint8_t rx[1] = {0};
  Semaphore read_sem;
  ReadOperation rop(read_sem, 0);
  auto ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::INIT_ERR);

  WritePort w(2, 16);
  w = FailWriteFun;

  static const uint8_t TX[] = {0x55};
  Semaphore write_sem;
  WriteOperation wop(write_sem, 0);
  ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::INIT_ERR);
}

void test_rw_read_port_reset_detaches_block_waiter()
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

  r.Reset();
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::BLOCK_DETACHED);

  uint8_t blocked_rx[1] = {0};
  Semaphore blocked_sem;
  ReadOperation blocked_op(blocked_sem, 0);
  ASSERT(r(RawData{blocked_rx, sizeof(blocked_rx)}, blocked_op) == ErrorCode::BUSY);

  ExpectWaitOk(done, kShortWaitMs);
  JoinThreadIfNeeded(reader);
  ASSERT(ctx.result == ErrorCode::TIMEOUT);
  ASSERT(stale_rx[0] == 0xA5);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);

  uint8_t tx = 0x5A;
  ASSERT(r.queue_data_->PushBatch(&tx, 1) == ErrorCode::OK);

  uint8_t fresh_rx[1] = {0};
  ReadOperation fresh_op;
  ASSERT(r(RawData{fresh_rx, sizeof(fresh_rx)}, fresh_op) == ErrorCode::OK);
  ASSERT(fresh_rx[0] == tx);
}

void test_rw_write_port_reset_detaches_block_waiter()
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

  w.Reset();
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::BLOCK_DETACHED);

  Semaphore blocked_sem;
  WriteOperation blocked_op(blocked_sem, 0);
  ASSERT(w(ConstRawData{TX2, sizeof(TX2)}, blocked_op) == ErrorCode::BUSY);

  ExpectWaitOk(done, kShortWaitMs);
  JoinThreadIfNeeded(writer);
  ASSERT(ctx.result == ErrorCode::TIMEOUT);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);

  Semaphore finish_done;
  Thread finisher;
  StartWriteFinisher(finisher, w, finish_done, ErrorCode::OK, "wr_reset_finish");

  Semaphore sem;
  WriteOperation op(sem, 100);
  ASSERT(w(ConstRawData{TX2, sizeof(TX2)}, op) == ErrorCode::OK);
  ExpectWaitOk(finish_done, kShortWaitMs);
  JoinThreadIfNeeded(finisher);
}

void test_rw_write_port_reset_late_finish_before_timeout_wake()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX1[] = {0x91, 0x92, 0x93};
  static const uint8_t TX2[] = {0xA1, 0xA2};

  Semaphore sem1(0);
  WriteOperation op1(sem1, 20);
  Semaphore done;
  BlockingWriteExternalOpCallContext ctx{&w, ConstRawData{TX1, sizeof(TX1)}, &op1,
                                         ErrorCode::FAILED, &done};
  Thread writer;
  StartBlockingWriteExternalOpCaller(writer, ctx, "wr_reset_late_finish");

  while (w.busy_.load(std::memory_order_acquire) != WritePort::BusyState::BLOCK_WAITING)
  {
    Thread::Yield();
  }

  w.Reset();
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::BLOCK_DETACHED);

  WriteInfoBlock completed{ConstRawData{TX1, sizeof(TX1)}, op1};
  w.Finish(false, ErrorCode::OK, completed);

  ExpectWaitOk(done, kShortWaitMs);
  JoinThreadIfNeeded(writer);
  ASSERT(ctx.result == ErrorCode::TIMEOUT);
  ASSERT(sem1.Value() == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);

  WriteOperation next_op;
  ASSERT(w(ConstRawData{TX2, sizeof(TX2)}, next_op) == ErrorCode::OK);
}

void test_rw_read_port_block_pending_result_propagates()
{
  using namespace LibXR;

  ReadPort r(16);
  r = PendingReadFun;

  uint8_t rx[1] = {0};
  Semaphore sem;
  ReadOperation op(sem, 100);
  Semaphore done;
  Thread finisher;
  StartReadFinisher(finisher, r, done, ErrorCode::FAILED, "rd_finish");

  auto ec = r(RawData{rx, sizeof(rx)}, op);
  ASSERT(ec == ErrorCode::FAILED);
  ExpectWaitOk(done, kShortWaitMs);
  JoinThreadIfNeeded(finisher);
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
  ExpectWaitOk(done, kShortWaitMs);
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
  ExpectWaitOk(done1, kShortWaitMs);
  JoinThreadIfNeeded(finisher1);
  ASSERT(sem.Value() == 0);

  Semaphore done2;
  Thread finisher2;
  StartWriteFinisher(finisher2, w, done2, ErrorCode::OK, "wr_stale2");

  ec = w(ConstRawData{TX2, sizeof(TX2)}, op);
  ASSERT(ec == ErrorCode::OK);
  ExpectWaitOk(done2, kShortWaitMs);
  JoinThreadIfNeeded(finisher2);
  ASSERT(sem.Value() == 0);
}

void test_rw()
{
  test_rw_pending_mode_matrix();
  test_rw_edge_cases();
  test_rw_stream_block_pending_result_propagates();
  test_rw_stream_block_timeout_detaches_waiter();
  test_rw_stream_block_destructor_autocommit();
  test_rw_block_read_timeout_detaches_pending();
  test_rw_block_write_timeout_detaches_waiter();
  test_rw_block_immediate_error_propagates();
  test_rw_read_port_reset_detaches_block_waiter();
  test_rw_write_port_reset_detaches_block_waiter();
  test_rw_write_port_reset_late_finish_before_timeout_wake();
  test_rw_read_port_block_pending_result_propagates();
  test_rw_write_port_block_pending_result_propagates();
  test_rw_write_port_block_reused_waiter_discards_stale_signal();
}
