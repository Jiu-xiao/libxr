/**
 * @file test_pipe_runtime_cases.cpp
 * @brief Pipe 阻塞提交与重复读写测试
 *        / Pipe blocking submission and repeated transfer tests.
 */
#include "rw_runtime_test_common.hpp"

namespace
{

/**
 * @brief 验证 Pipe 阻塞流提交不残留信号量
 *        / Verify Pipe BLOCK stream commits leave no token.
 */
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
}

/**
 * @brief 验证交替读写顺序下阻塞操作可重复使用
 *        / Verify BLOCK reuse with alternating read/write order.
 */
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

/**
 * @brief 运行 Pipe 阻塞提交与复用测试 / Run Pipe BLOCK submission and reuse tests.
 */
void RunRuntimePipeTests()
{
  test_pipe_stream_block_immediate_path();
  test_pipe_block_reuse_stress();
}
