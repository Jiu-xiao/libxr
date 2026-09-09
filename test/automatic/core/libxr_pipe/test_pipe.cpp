/**
 * @file test_pipe.cpp
 * @brief 检查 Pipe 收发顺序、操作模式、零长度请求和重复使用。 /
 * Tests Pipe transfer order, operation modes, empty requests and reuse.
 *
 * 检查分次写入、Stream 提交，以及提交后另一个 Stream 可以继续写入。
 * Checks chunked writes, Stream commits and write access after a previous commit.
 */

#include "core/libxr_pipe/pipe_transfer_test_common.hpp"
#include "test_assert.hpp"

namespace
{
void VerifyZeroWriteMode(TestMode mode)
{
  // 零长度写入立即完成，不影响紧接着的一次正常收发。
  // An empty write completes immediately and leaves the next transfer usable.
  using namespace LibXR;

  Pipe pipe(8);
  WritePort& w = pipe.GetWritePort();
  ReadPort& r = pipe.GetReadPort();

  WriteHarness write(mode);
  auto write_result = w(ConstRawData{nullptr, 0}, write.op);
  if (mode == TestMode::BLOCK)
  {
    TEST_ASSERT(write_result == ErrorCode::OK);
  }
  else
  {
    TEST_ASSERT(write_result == ErrorCode::OK);
    write.ExpectFinal(ErrorCode::OK);
  }
  TEST_ASSERT(w.Size() == 0);

  uint8_t tx = 0x5A;
  uint8_t rx = 0;
  WriteOperation plain_write;
  ReadOperation plain_read;
  TEST_ASSERT(w(ConstRawData{&tx, 1}, plain_write) == ErrorCode::OK);
  TEST_ASSERT(r(RawData{&rx, 1}, plain_read) == ErrorCode::OK);
  TEST_ASSERT(rx == tx);
}

void VerifyZeroReadMode(TestMode mode)
{
  // 已有数据时零长度读取立即完成，随后仍能读出原来的字节。
  // An empty read completes when data is ready and leaves that byte for the next read.
  using namespace LibXR;

  Pipe pipe(8);
  WritePort& w = pipe.GetWritePort();
  ReadPort& r = pipe.GetReadPort();

  uint8_t tx = 0xA7;
  WriteOperation write_op;
  TEST_ASSERT(w(ConstRawData{&tx, 1}, write_op) == ErrorCode::OK);

  uint8_t dummy = 0x11;
  ReadHarness read(mode);
  auto zero_result = r(RawData{&dummy, 0}, read.op);
  if (mode == TestMode::BLOCK)
  {
    TEST_ASSERT(zero_result == ErrorCode::OK);
  }
  else
  {
    TEST_ASSERT(zero_result == ErrorCode::OK);
    read.ExpectFinal(ErrorCode::OK);
  }

  uint8_t rx = 0;
  ReadOperation plain_read;
  TEST_ASSERT(r(RawData{&rx, 1}, plain_read) == ErrorCode::OK);
  TEST_ASSERT(rx == tx);
}

void test_pipe_basic()
{
  // 先挂起读取，再写入完整数据；同一 Pipe 的写入应推进这个读取。
  // Submit the read first; writing to the same Pipe must complete it.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t TX[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
  uint8_t rx[sizeof(TX)] = {0};

  ReadOperation rop;
  WriteOperation wop;

  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  TEST_ASSERT(ec == ErrorCode::OK);

  ec = w(ConstRawData{TX, sizeof(TX)}, wop);
  TEST_ASSERT(ec == ErrorCode::OK);

  TEST_ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
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
  TEST_ASSERT(ec == ErrorCode::OK);

  ec = r(RawData{rx, sizeof(rx)}, rop);
  TEST_ASSERT(ec == ErrorCode::OK);

  TEST_ASSERT(std::memcmp(rx, TX, sizeof(TX)) == 0);
}

void test_pipe_chunked_rw()
{
  // 一个读取请求等待两次写入，最终按写入顺序拼接数据。
  // One read waits for two writes and receives their bytes in order.
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
  TEST_ASSERT(ec == ErrorCode::OK);

  ec = w(ConstRawData{TX1, sizeof(TX1)}, w1);
  TEST_ASSERT(ec == ErrorCode::OK);
  ec = w(ConstRawData{TX2, sizeof(TX2)}, w2);
  TEST_ASSERT(ec == ErrorCode::OK);

  static const uint8_t EXPECT[] = {'H', 'e', 'l', 'l', 'o', ' ', 'X', 'R'};
  TEST_ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

void test_pipe_stream_api()
{
  // 两次追加在一次 Commit 中提交，读取端看到连续的字节序列。
  // Two appends are submitted by one Commit and arrive as a continuous byte sequence.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();
  WriteOperation wop;

  uint8_t rx[8] = {0};

  ReadOperation rop;
  ErrorCode ec = r(RawData{rx, sizeof(rx)}, rop);
  TEST_ASSERT(ec == ErrorCode::OK);

  WritePort::Stream ws(&w, wop);
  static const uint8_t A[] = {0xAA, 0xBB, 0xCC};
  static const uint8_t B[] = {0x11, 0x22, 0x33, 0x44, 0x55};
  ws << ConstRawData{A, sizeof(A)} << ConstRawData{B, sizeof(B)};
  ec = ws.Commit();
  TEST_ASSERT(ec == ErrorCode::OK);

  static const uint8_t EXPECT[] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33, 0x44, 0x55};
  TEST_ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

void test_pipe_stream_block_immediate_path()
{
  // Pipe 提交时即可完成写入，阻塞操作不应留下多余的信号量通知。
  // Pipe can complete the write during submission, leaving no extra semaphore token.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  uint8_t rx[8] = {0};
  ReadOperation rop;
  TEST_ASSERT(r(RawData{rx, sizeof(rx)}, rop) == ErrorCode::OK);

  Semaphore sem;
  WriteOperation wop(sem, 100);
  WritePort::Stream ws(&w, wop);
  static const uint8_t A[] = {0x21, 0x22, 0x23};
  static const uint8_t B[] = {0x31, 0x32, 0x33, 0x34, 0x35};
  ws << ConstRawData{A, sizeof(A)} << ConstRawData{B, sizeof(B)};

  auto ec = ws.Commit();
  TEST_ASSERT(ec == ErrorCode::OK);

  static const uint8_t EXPECT[] = {0x21, 0x22, 0x23, 0x31, 0x32, 0x33, 0x34, 0x35};
  TEST_ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
  TEST_ASSERT(sem.Value() == 0);
}

void test_pipe_stream_commit_releases_lock_for_next_stream()
{
  // ws1 仍在作用域内；Commit 应已释放写入权限，让 ws2 可以提交。
  // ws1 is still alive; Commit must release write access so ws2 can submit.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t A[] = {0x10, 0x11, 0x12};
  static const uint8_t B[] = {0x20, 0x21, 0x22, 0x23};
  uint8_t rx[sizeof(A) + sizeof(B)] = {0};

  ReadOperation rop;
  TEST_ASSERT(r(RawData{rx, sizeof(rx)}, rop) == ErrorCode::OK);

  WriteOperation op1;
  WritePort::Stream ws1(&w, op1);
  ws1 << ConstRawData{A, sizeof(A)};
  TEST_ASSERT(ws1.Commit() == ErrorCode::OK);

  WriteOperation op2;
  WritePort::Stream ws2(&w, op2);
  ws2 << ConstRawData{B, sizeof(B)};
  TEST_ASSERT(ws2.Commit() == ErrorCode::OK);

  static const uint8_t EXPECT[] = {0x10, 0x11, 0x12, 0x20, 0x21, 0x22, 0x23};
  TEST_ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

void test_pipe_stream_commit_allows_persistent_and_external_streams()
{
  // 长寿命 Stream 两次提交之间插入另一个 Stream，数据仍按提交顺序排列。
  // Insert another Stream between two commits of a persistent Stream; preserve commit
  // order.
  using namespace LibXR;

  Pipe pipe(64);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  static const uint8_t A[] = {'T', '1'};
  static const uint8_t B[] = {'E', 'X', 'T'};
  static const uint8_t C[] = {'T', '2', '!'};
  uint8_t rx[sizeof(A) + sizeof(B) + sizeof(C)] = {0};

  ReadOperation rop;
  TEST_ASSERT(r(RawData{rx, sizeof(rx)}, rop) == ErrorCode::OK);

  WriteOperation owner_op;
  WritePort::Stream owner(&w, owner_op);
  owner << ConstRawData{A, sizeof(A)};
  TEST_ASSERT(owner.Commit() == ErrorCode::OK);

  WriteOperation external_op;
  WritePort::Stream external(&w, external_op);
  external << ConstRawData{B, sizeof(B)};
  TEST_ASSERT(external.Commit() == ErrorCode::OK);

  owner << ConstRawData{C, sizeof(C)};
  TEST_ASSERT(owner.Commit() == ErrorCode::OK);

  static const uint8_t EXPECT[] = {'T', '1', 'E', 'X', 'T', 'T', '2', '!'};
  TEST_ASSERT(std::memcmp(rx, EXPECT, sizeof(EXPECT)) == 0);
}

void test_pipe_mode_matrix()
{
  // 分别检查先读后写、先写后读；完成通知方式不应改变数据内容。
  // Check both read-first and write-first order; completion mode must not change the
  // bytes.
  uint8_t seed = 0x21;

  for (auto read_mode : ASYNC_MODES)
  {
    for (auto write_mode : ASYNC_MODES)
    {
      VerifyPendingReadThenWrite(read_mode, write_mode, 7, seed++);
      VerifyWriteThenRead(write_mode, read_mode, 7, seed++);
    }
  }
}

void test_pipe_reuse_stress()
{
  using namespace LibXR;

  Pipe pipe(PIPE_CAPACITY);
  ReadPort& r = pipe.GetReadPort();
  WritePort& w = pipe.GetWritePort();

  ReadHarness read(TestMode::CALLBACK);
  WriteHarness write(TestMode::POLLING);

  for (size_t iter = 0; iter < MIXED_STRESS_ITERATIONS; ++iter)
  {
    const size_t size = 1 + (iter % 31);
    std::vector<uint8_t> tx(size);
    std::vector<uint8_t> rx(size, 0x44);
    FillPattern(tx, static_cast<uint8_t>(0x40 + iter));

    read.Reset();
    write.Reset();

    if ((iter & 1u) == 0)
    {
      TEST_ASSERT(r(RawData{rx.data(), rx.size()}, read.op) == ErrorCode::OK);
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

    TEST_ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
    TEST_ASSERT(r.Size() == 0);
    TEST_ASSERT(w.Size() == 0);
  }
}

void test_pipe_block_reuse_stress()
{
  using namespace LibXR;

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

      TEST_ASSERT(r(RawData{rx.data(), rx.size()}, read.op) == ErrorCode::OK);
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

    TEST_ASSERT(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);
    TEST_ASSERT(r.Size() == 0);
    TEST_ASSERT(w.Size() == 0);
  }
}

void test_pipe_edge_cases()
{
  for (auto read_mode : ASYNC_MODES)
  {
    for (auto write_mode : ASYNC_MODES)
    {
      VerifyPendingReadThenWrite(read_mode, write_mode, 1, 0x61);
      VerifyWriteThenRead(write_mode, read_mode, PIPE_CAPACITY, 0x91);
    }
  }
}
}  // namespace

void test_pipe()
{
  for (auto mode : ASYNC_MODES)
  {
    VerifyZeroWriteMode(mode);
    VerifyZeroReadMode(mode);
  }

  test_pipe_basic();
  test_pipe_write_then_read();
  test_pipe_chunked_rw();
  test_pipe_stream_api();
  test_pipe_stream_block_immediate_path();
  test_pipe_stream_commit_releases_lock_for_next_stream();
  test_pipe_stream_commit_allows_persistent_and_external_streams();
  test_pipe_mode_matrix();
  test_pipe_reuse_stress();
  test_pipe_block_reuse_stress();
  test_pipe_edge_cases();
}
