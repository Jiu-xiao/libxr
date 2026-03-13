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
constexpr size_t kPipeCapacity = 64;
constexpr size_t kMixedStressIterations = 64;
constexpr size_t kBlockStressIterations = 8;

enum class TestMode : uint8_t
{
  NONE,
  POLLING,
  CALLBACK,
  BLOCK
};

constexpr TestMode kAllModes[] = {
    TestMode::NONE, TestMode::POLLING, TestMode::CALLBACK, TestMode::BLOCK};

constexpr TestMode kAsyncModes[] = {
    TestMode::NONE, TestMode::POLLING, TestMode::CALLBACK};

LibXR::ErrorCode PendingWriteFun(LibXR::WritePort&, bool) { return LibXR::ErrorCode::PENDING; }

LibXR::ErrorCode PendingReadFun(LibXR::ReadPort&, bool) { return LibXR::ErrorCode::PENDING; }

LibXR::ErrorCode FailWriteFun(LibXR::WritePort&, bool) { return LibXR::ErrorCode::INIT_ERR; }

LibXR::ErrorCode FailReadFun(LibXR::ReadPort&, bool) { return LibXR::ErrorCode::INIT_ERR; }

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
        ASSERT(polling_status ==
               ((expected == LibXR::ErrorCode::OK) ? PollingStatus::DONE
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

void ExpectWaitOk(LibXR::Semaphore& sem, uint32_t timeout = kAsyncTimeoutMs)
{
  ASSERT(sem.Wait(timeout) == LibXR::ErrorCode::OK);
}

void StartReadFinisher(LibXR::Thread& thread, LibXR::ReadPort& port, LibXR::Semaphore& done,
                       LibXR::ErrorCode result, const char* name)
{
  thread.Create(ReadFinishContext{&port, &done, result}, FinishPendingRead, name, 1024,
                LibXR::Thread::Priority::MEDIUM);
}

void StartWriteFinisher(LibXR::Thread& thread, LibXR::WritePort& port, LibXR::Semaphore& done,
                        LibXR::ErrorCode result, const char* name)
{
  thread.Create(WriteFinishContext{&port, &done, result}, FinishPendingWrite, name, 1024,
                LibXR::Thread::Priority::MEDIUM);
}

void StartDelayedPipeWriter(LibXR::Thread& thread, DelayedPipeWriteContext& ctx,
                            const char* name)
{
  thread.Create<DelayedPipeWriteContext*>(&ctx, DelayedPipeWrite, name, 1024,
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

enum class StreamSubmitMode : uint8_t
{
  COMMIT,
  DESTRUCT
};

void VerifyPendingReadThenWrite(TestMode read_mode, TestMode write_mode, size_t size,
                                uint8_t seed)
{
  using namespace LibXR;

  Pipe pipe((size > 0) ? size : kPipeCapacity);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  std::vector<uint8_t> tx(size);
  std::vector<uint8_t> rx(size, 0xCD);
  FillPattern(tx, seed);

  ReadHarness read(read_mode);
  WriteHarness write(write_mode);

  if (read_mode == TestMode::BLOCK)
  {
    Semaphore write_done;
    DelayedPipeWriteContext ctx{&w, &write, tx.data(), tx.size(), 5, ErrorCode::FAILED,
                                &write_done};
    Thread writer;
    StartDelayedPipeWriter(writer, ctx, "pipe_write_async");

    auto read_result = r(RawData{rx.data(), rx.size()}, read.op);
    ASSERT(read_result == ErrorCode::OK);
    ExpectWaitOk(write_done);
    JoinThreadIfNeeded(writer);
    ExpectCallResult(write, ctx.result, ErrorCode::OK);
  }
  else
  {
    auto read_result = r(RawData{rx.data(), rx.size()}, read.op);
    ASSERT(read_result == ErrorCode::OK);
    read.ExpectPendingSubmitted();

    auto write_result = w(ConstRawData{tx.data(), tx.size()}, write.op);
    ExpectCallResult(write, write_result, ErrorCode::OK);
    read.ExpectFinal(ErrorCode::OK);
  }

  ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
  ASSERT(r.busy_.load(std::memory_order_acquire) == ReadPort::BusyState::IDLE);
}

void VerifyWriteThenRead(TestMode write_mode, TestMode read_mode, size_t size, uint8_t seed)
{
  using namespace LibXR;

  Pipe pipe((size > 0) ? size : kPipeCapacity);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  std::vector<uint8_t> tx(size);
  std::vector<uint8_t> rx(size, 0xEF);
  FillPattern(tx, seed);

  ReadHarness read(read_mode);
  WriteHarness write(write_mode);

  auto write_result = w(ConstRawData{tx.data(), tx.size()}, write.op);
  ExpectCallResult(write, write_result, ErrorCode::OK);

  auto read_result = r(RawData{rx.data(), rx.size()}, read.op);
  ExpectCallResult(read, read_result, ErrorCode::OK);

  ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
  ASSERT(r.Size() == 0);
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
  ExpectCallResult(write, write_result, ErrorCode::OK);
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
  ExpectCallResult(read, zero_result, ErrorCode::OK);

  uint8_t rx = 0;
  ReadOperation plain_read;
  ASSERT(r(RawData{&rx, 1}, plain_read) == ErrorCode::OK);
  ASSERT(rx == tx);
}
}  // namespace

void test_pipe_basic()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
  uint8_t rx[sizeof(TX)] = {0};

  ReadOperation rop;
  WriteOperation wop;

  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::OK);

  r.ProcessPendingReads(false);
  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
}

void test_pipe_write_then_read()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX[] = {1, 2, 3, 4, 5, 6, 7};
  uint8_t rx[sizeof(TX)] = {0};

  ReadOperation rop;
  WriteOperation wop;

  ErrorCode ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  ASSERT(ec == ErrorCode::OK);

  ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  r.ProcessPendingReads(false);
  ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
}

void test_pipe_chunked_rw()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX1[] = {'H', 'e', 'l'};
  static const uint8_t TX2[] = {'l', 'o', ' ', 'X', 'R'};
  uint8_t rx[sizeof(TX1) + sizeof(TX2)] = {0};

  ReadOperation rop;
  WriteOperation w1;
  WriteOperation w2;

  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  ec = w(ConstRawData{TX1, sizeof(TX1)}, w1);
  ASSERT(ec == ErrorCode::OK);
  ec = w(ConstRawData{TX2, sizeof(TX2)}, w2);
  ASSERT(ec == ErrorCode::OK);

  r.ProcessPendingReads(false);

  static const uint8_t EXPECT[] = {'H', 'e', 'l', 'l', 'o', ' ', 'X', 'R'};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

void test_pipe_stream_api()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();
  WriteOperation wop;

  uint8_t rx[8] = {0};

  ReadOperation rop;
  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  ASSERT(ec == ErrorCode::OK);

  WritePort::Stream ws(&w, wop);
  static const uint8_t A[] = {0xAA, 0xBB, 0xCC};
  static const uint8_t B[] = {0x11, 0x22, 0x33, 0x44, 0x55};
  ws << ConstRawData{A, sizeof(A)} << ConstRawData{B, sizeof(B)};
  ec = ws.Commit();
  ASSERT(ec == ErrorCode::OK);

  r.ProcessPendingReads(false);

  static const uint8_t EXPECT[] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33, 0x44, 0x55};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
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
  StartWriteFinisher(finisher, w, done, finish_result,
                     (submit_mode == StreamSubmitMode::COMMIT) ? "wr_stream_commit"
                                                                : "wr_stream_dtor");

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

void test_write_port_stream_block_pending_result_propagates()
{
  VerifyStreamBlockPendingCompletion(LibXR::ErrorCode::FAILED, LibXR::ErrorCode::FAILED,
                                     StreamSubmitMode::COMMIT);
}

void test_write_port_stream_block_timeout_detaches_waiter()
{
  VerifyStreamBlockTimeout();
}

void test_write_port_stream_block_destructor_autocommit()
{
  VerifyStreamBlockPendingCompletion(LibXR::ErrorCode::OK, LibXR::ErrorCode::OK,
                                     StreamSubmitMode::DESTRUCT);
}

void test_pipe_stream_commit_releases_lock_for_next_stream()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t A[] = {0x10, 0x11, 0x12};
  static const uint8_t B[] = {0x20, 0x21, 0x22, 0x23};
  uint8_t rx[sizeof(A) + sizeof(B)] = {0};

  ReadOperation rop;
  ASSERT(r(RawData{rx, sizeof(rx)}, rop) == ErrorCode::OK);

  WriteOperation op1;
  WritePort::Stream ws1(&w, op1);
  ws1 << ConstRawData{A, sizeof(A)};
  ASSERT(ws1.Commit() == ErrorCode::OK);

  WriteOperation op2;
  WritePort::Stream ws2(&w, op2);
  ws2 << ConstRawData{B, sizeof(B)};
  ASSERT(ws2.Commit() == ErrorCode::OK);

  static const uint8_t EXPECT[] = {0x10, 0x11, 0x12, 0x20, 0x21, 0x22, 0x23};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

void test_pipe_stream_commit_allows_persistent_and_external_streams()
{
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t A[] = {'T', '1'};
  static const uint8_t B[] = {'E', 'X', 'T'};
  static const uint8_t C[] = {'T', '2', '!'};
  uint8_t rx[sizeof(A) + sizeof(B) + sizeof(C)] = {0};

  ReadOperation rop;
  ASSERT(r(RawData{rx, sizeof(rx)}, rop) == ErrorCode::OK);

  WriteOperation owner_op;
  WritePort::Stream owner(&w, owner_op);
  owner << ConstRawData{A, sizeof(A)};
  ASSERT(owner.Commit() == ErrorCode::OK);

  WriteOperation external_op;
  WritePort::Stream external(&w, external_op);
  external << ConstRawData{B, sizeof(B)};
  ASSERT(external.Commit() == ErrorCode::OK);

  owner << ConstRawData{C, sizeof(C)};
  ASSERT(owner.Commit() == ErrorCode::OK);

  static const uint8_t EXPECT[] = {'T', '1', 'E', 'X', 'T', 'T', '2', '!'};
  ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

void test_pipe_mode_matrix()
{
  uint8_t seed = 0x21;

  for (auto read_mode : kAsyncModes)
  {
    for (auto write_mode : kAsyncModes)
    {
      VerifyPendingReadThenWrite(read_mode, write_mode, 7, seed++);
      VerifyWriteThenRead(write_mode, read_mode, 7, seed++);
    }
  }
}

void test_pipe_pending_mode_matrix()
{
  for (auto mode : kAsyncModes)
  {
    VerifyPendingReadMode(mode, LibXR::ErrorCode::FAILED);
    VerifyPendingWriteMode(mode, LibXR::ErrorCode::FAILED);
  }
}

void test_pipe_reuse_stress()
{
  using namespace LibXR;

  Pipe pipe(kPipeCapacity);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  ReadHarness read(TestMode::CALLBACK);
  WriteHarness write(TestMode::POLLING);

  for (size_t iter = 0; iter < kMixedStressIterations; ++iter)
  {
    const size_t size = 1 + (iter % 31);
    std::vector<uint8_t> tx(size);
    std::vector<uint8_t> rx(size, 0x44);
    FillPattern(tx, static_cast<uint8_t>(0x40 + iter));

    read.Reset();
    write.Reset();

    if ((iter & 1u) == 0)
    {
      ASSERT(r(RawData{rx.data(), rx.size()}, read.op) == ErrorCode::OK);
      read.ExpectPendingSubmitted();
      ExpectCallResult(write, w(ConstRawData{tx.data(), tx.size()}, write.op),
                       ErrorCode::OK);
      read.ExpectFinal(ErrorCode::OK);
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

void test_pipe_block_reuse_stress()
{
  using namespace LibXR;

  Pipe pipe(kPipeCapacity);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  ReadHarness read(TestMode::BLOCK);
  WriteHarness write(TestMode::BLOCK);

  for (size_t iter = 0; iter < kBlockStressIterations; ++iter)
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
      DelayedPipeWriteContext ctx{&w, &write, tx.data(), tx.size(), 5, ErrorCode::FAILED,
                                  &write_done};
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

void test_pipe_edge_cases()
{
  using namespace LibXR;

  for (auto mode : kAsyncModes)
  {
    VerifyZeroWriteMode(mode);
    VerifyZeroReadMode(mode);
  }

  for (auto read_mode : kAsyncModes)
  {
    for (auto write_mode : kAsyncModes)
    {
      VerifyPendingReadThenWrite(read_mode, write_mode, 1, 0x61);
      VerifyWriteThenRead(write_mode, read_mode, kPipeCapacity, 0x91);
    }
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

void test_pipe_block_read_timeout_detaches_pending()
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

void test_pipe_block_write_timeout_detaches_waiter()
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

void test_pipe_block_immediate_error_propagates()
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

void test_read_port_reset_detaches_block_waiter()
{
  using namespace LibXR;

  ReadPort r(16);
  r = PendingReadFun;

  uint8_t stale_rx[1] = {0xA5};
  Semaphore done;
  BlockingReadCallContext ctx{
      &r, RawData{stale_rx, sizeof(stale_rx)}, 20, ErrorCode::FAILED, &done};
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

void test_write_port_reset_detaches_block_waiter()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX1[] = {0x11, 0x22, 0x33};
  static const uint8_t TX2[] = {0x44, 0x55, 0x66};

  Semaphore done;
  BlockingWriteCallContext ctx{
      &w, ConstRawData{TX1, sizeof(TX1)}, 20, ErrorCode::FAILED, &done};
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

void test_read_port_block_pending_result_propagates()
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

void test_write_port_block_pending_result_propagates()
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

void test_read_port_block_ignores_stale_waiter_token()
{
  using namespace LibXR;

  ReadPort r(16);
  r = PendingReadFun;

  uint8_t rx[] = {0};
  Semaphore sem(1);
  ReadOperation op(sem, 100);
  Semaphore done;
  Thread finisher;
  StartReadFinisher(finisher, r, done, ErrorCode::OK, "rd_stale_token");

  auto ec = r(RawData{rx, sizeof(rx)}, op);
  ASSERT(ec == ErrorCode::OK);
  ExpectWaitOk(done, kShortWaitMs);
  JoinThreadIfNeeded(finisher);
  ASSERT(sem.Value() == 0);
}

void test_write_port_block_ignores_stale_waiter_token()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = PendingWriteFun;

  static const uint8_t TX[] = {0x6B};
  Semaphore sem(1);
  WriteOperation op(sem, 100);
  Semaphore done;
  Thread finisher;
  StartWriteFinisher(finisher, w, done, ErrorCode::OK, "wr_stale_token");

  auto ec = w(ConstRawData{TX, sizeof(TX)}, op);
  ASSERT(ec == ErrorCode::OK);
  ExpectWaitOk(done, kShortWaitMs);
  JoinThreadIfNeeded(finisher);
  ASSERT(sem.Value() == 0);
}

void test_write_port_block_reused_waiter_discards_stale_signal()
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

void test_pipe()
{
  test_pipe_basic();
  test_pipe_write_then_read();
  test_pipe_chunked_rw();
  test_pipe_stream_api();
  test_pipe_stream_block_immediate_path();
  test_write_port_stream_block_pending_result_propagates();
  test_write_port_stream_block_timeout_detaches_waiter();
  test_write_port_stream_block_destructor_autocommit();
  test_pipe_stream_commit_releases_lock_for_next_stream();
  test_pipe_stream_commit_allows_persistent_and_external_streams();
  test_pipe_mode_matrix();
  test_pipe_pending_mode_matrix();
  test_pipe_reuse_stress();
  test_pipe_block_reuse_stress();
  test_pipe_edge_cases();
  test_pipe_block_read_timeout_detaches_pending();
  test_pipe_block_write_timeout_detaches_waiter();
  test_pipe_block_immediate_error_propagates();
  test_read_port_reset_detaches_block_waiter();
  test_write_port_reset_detaches_block_waiter();
  test_read_port_block_pending_result_propagates();
  test_write_port_block_pending_result_propagates();
  test_read_port_block_ignores_stale_waiter_token();
  test_write_port_block_ignores_stale_waiter_token();
  test_write_port_block_reused_waiter_discards_stale_signal();
}
