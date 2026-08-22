/**
 * @file test_pipe_stream.cpp
 * @brief base `Pipe` stream 语义场景子测试。 Split test unit for base `Pipe` stream
 * semantics.
 */
#include <type_traits>

#include "rw_test_common.hpp"

namespace
{

struct StreamCallbackReentry
{
  SynchronousWritePort* port = nullptr;
  LibXR::WritePort::Stream* stream = nullptr;
  bool try_reentry = true;
  size_t callback_count = 0U;
  LibXR::ErrorCode completion_result = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode direct_write = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode stream_write = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode stream_zero_write = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode stream_commit = LibXR::ErrorCode::FAILED;
};

struct PipeSettlementReentry
{
  LibXR::ReadPort* read = nullptr;
  LibXR::Callback<LibXR::ErrorCode>* read_callback = nullptr;
  uint8_t first_byte = 0U;
  uint8_t second_byte = 0U;
  size_t read_callback_count = 0U;
  bool write_saw_first_read = false;
  LibXR::ErrorCode second_read_submit = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode second_push = LibXR::ErrorCode::FAILED;
};

void OnPipeSettlementRead(bool, PipeSettlementReentry* context, LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  ++context->read_callback_count;
  if (context->read_callback_count == 1U)
  {
    LibXR::ReadOperation second_operation(*context->read_callback);
    context->second_read_submit =
        (*context->read)(LibXR::RawData{&context->second_byte, 1U}, second_operation);
  }
}

void OnPipeSettlementWrite(bool, PipeSettlementReentry* context, LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  context->write_saw_first_read = context->read_callback_count == 1U;
  static const uint8_t SECOND = 0xB2U;
  auto queue = context->read->GetReadQueue();
  context->second_push = queue.Push(SECOND);
  if (context->second_push == LibXR::ErrorCode::OK)
  {
    queue.Publish();
  }
}

void OnStreamWriteComplete(bool, StreamCallbackReentry* context, LibXR::ErrorCode result)
{
  using namespace LibXR;

  ++context->callback_count;
  context->completion_result = result;

  if (context->try_reentry)
  {
    context->try_reentry = false;
    static const uint8_t NESTED[] = {0xA1};
    WriteOperation direct_op;
    context->direct_write =
        (*context->port)(ConstRawData{NESTED, sizeof(NESTED)}, direct_op, false);
    context->stream_write = context->stream->Write(ConstRawData{NESTED, sizeof(NESTED)});
    context->stream_zero_write = context->stream->Write(ConstRawData{nullptr, 0});
    context->stream_commit = context->stream->Commit();
  }
}

static_assert(!std::is_copy_constructible_v<LibXR::WritePort::Stream>);
static_assert(!std::is_copy_assignable_v<LibXR::WritePort::Stream>);
static_assert(!std::is_move_constructible_v<LibXR::WritePort::Stream>);
static_assert(!std::is_move_assignable_v<LibXR::WritePort::Stream>);

}  // namespace

/**
 * @brief 测试入口函数 `test_pipe_stream_block_immediate_path`。 Test entry function
 * `test_pipe_stream_block_immediate_path`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_stream_block_immediate_path()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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
 * @brief 测试入口函数 `test_pipe_stream_commit_releases_lock_for_next_stream`。 Test
 * entry function `test_pipe_stream_commit_releases_lock_for_next_stream`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_stream_commit_releases_lock_for_next_stream()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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
}

/**
 * @brief 测试入口函数 `test_pipe_stream_commit_allows_persistent_and_external_streams`。
 * Test entry function `test_pipe_stream_commit_allows_persistent_and_external_streams`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared
 * in this file in order. 测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。
 * Validate the module contract through the scenarios assembled in this file.
 */
void test_pipe_stream_commit_allows_persistent_and_external_streams()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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
}

void test_pipe_stream_payload_is_invisible_before_commit()
{
  using namespace LibXR;

  Pipe pipe(8U);
  ReadPort& read = pipe.GetReadPort();
  WritePort& write = pipe.GetWritePort();

  OperationPollingStatus write_status;
  WriteOperation write_operation(write_status);
  WritePort::Stream stream(&write, write_operation);
  static const uint8_t PAYLOAD[] = {0x31U, 0x32U, 0x33U};
  ASSERT(stream.Write(ConstRawData{PAYLOAD, sizeof(PAYLOAD)}) == ErrorCode::OK);
  ASSERT(write_status.Load() == OperationPollingStatus::READY);
  ASSERT(write.Size() == sizeof(PAYLOAD));
  ASSERT(read.Size() == 0U);

  uint8_t received[sizeof(PAYLOAD)] = {0xA5U, 0xA5U, 0xA5U};
  OperationPollingStatus read_status;
  ReadOperation read_operation(read_status);
  ASSERT(read(RawData{received, sizeof(received)}, read_operation) == ErrorCode::OK);
  ASSERT(read_status.Load() == OperationPollingStatus::RUNNING);
  ASSERT(received[0] == 0xA5U && received[1] == 0xA5U && received[2] == 0xA5U);

  ASSERT(stream.Commit() == ErrorCode::OK);
  ASSERT(write_status.Load() == OperationPollingStatus::DONE);
  ASSERT(read_status.Load() == OperationPollingStatus::DONE);
  ASSERT(std::memcmp(received, PAYLOAD, sizeof(PAYLOAD)) == 0);
  ASSERT(write.Size() == 0U);
  ASSERT(read.Size() == 0U);
}

void test_pipe_write_completion_preserves_committed_payload_until_read_completes()
{
  using namespace LibXR;

  Pipe pipe(8U);
  ReadPort& read = pipe.GetReadPort();
  WritePort& write = pipe.GetWritePort();

  uint8_t received[5] = {};
  OperationPollingStatus read_status;
  ReadOperation read_operation(read_status);
  ASSERT(read(RawData{received, sizeof(received)}, read_operation) == ErrorCode::OK);
  ASSERT(read_status.Load() == OperationPollingStatus::RUNNING);

  static const uint8_t FIRST[] = {0x11U, 0x12U};
  OperationPollingStatus first_status;
  WriteOperation first_operation(first_status);
  ASSERT(write(ConstRawData{FIRST, sizeof(FIRST)}, first_operation) == ErrorCode::OK);
  ASSERT(first_status.Load() == OperationPollingStatus::DONE);
  ASSERT(read_status.Load() == OperationPollingStatus::RUNNING);
  ASSERT(read.Size() == sizeof(FIRST));

  static const uint8_t SECOND[] = {0x21U, 0x22U, 0x23U};
  OperationPollingStatus second_status;
  WriteOperation second_operation(second_status);
  ASSERT(write(ConstRawData{SECOND, sizeof(SECOND)}, second_operation) == ErrorCode::OK);
  ASSERT(second_status.Load() == OperationPollingStatus::DONE);
  ASSERT(read_status.Load() == OperationPollingStatus::DONE);
  ASSERT(read.Size() == 0U);

  static const uint8_t EXPECTED[] = {0x11U, 0x12U, 0x21U, 0x22U, 0x23U};
  ASSERT(std::memcmp(received, EXPECTED, sizeof(EXPECTED)) == 0);
}

void test_pipe_stream_callback_can_reuse_same_stream()
{
  using namespace LibXR;

  SynchronousWritePort port;
  StreamCallbackReentry context{&port};
  auto callback = Callback<ErrorCode>::Create(OnStreamWriteComplete, &context);
  WriteOperation operation(callback);
  WritePort::Stream stream(&port, operation);
  context.stream = &stream;

  static const uint8_t OUTER[] = {0x31, 0x32, 0x33};
  ASSERT(stream.Write(ConstRawData{OUTER, sizeof(OUTER)}) == ErrorCode::OK);
  ASSERT(stream.Commit() == ErrorCode::OK);

  ASSERT(context.completion_result == ErrorCode::OK);
  ASSERT(context.callback_count == 2U);
  ASSERT(context.direct_write == ErrorCode::OK);
  ASSERT(context.stream_write == ErrorCode::OK);
  ASSERT(context.stream_zero_write == ErrorCode::OK);
  ASSERT(context.stream_commit == ErrorCode::OK);
  static const uint8_t NESTED_BATCH[] = {0xA1, 0xA1};
  ASSERT(port.payload_size == sizeof(NESTED_BATCH));
  ASSERT(std::memcmp(port.payload, NESTED_BATCH, sizeof(NESTED_BATCH)) == 0);
  ASSERT(port.Size() == 0U);

  static const uint8_t REUSE[] = {0x41, 0x42};
  ASSERT(stream.Write(ConstRawData{REUSE, sizeof(REUSE)}) == ErrorCode::OK);
  ASSERT(stream.Commit() == ErrorCode::OK);
  ASSERT(port.payload_size == sizeof(REUSE));
  ASSERT(std::memcmp(port.payload, REUSE, sizeof(REUSE)) == 0);
}

void test_pipe_publishes_read_scope_before_write_settlement_callback()
{
  using namespace LibXR;

  Pipe pipe(8U);
  ReadPort& read = pipe.GetReadPort();
  WritePort& write = pipe.GetWritePort();
  PipeSettlementReentry context{.read = &read};

  auto read_callback = Callback<ErrorCode>::Create(OnPipeSettlementRead, &context);
  context.read_callback = &read_callback;
  ReadOperation read_operation(read_callback);
  ASSERT(read(RawData{&context.first_byte, 1U}, read_operation) == ErrorCode::OK);

  auto write_callback = Callback<ErrorCode>::Create(OnPipeSettlementWrite, &context);
  WriteOperation write_operation(write_callback);
  static const uint8_t FIRST = 0xA1U;
  ASSERT(write(ConstRawData{&FIRST, 1U}, write_operation) == ErrorCode::OK);

  ASSERT(context.write_saw_first_read);
  ASSERT(context.second_push == ErrorCode::OK);
  ASSERT(context.second_read_submit == ErrorCode::OK);
  ASSERT(context.read_callback_count == 2U);
  ASSERT(context.first_byte == FIRST);
  ASSERT(context.second_byte == 0xB2U);
  ASSERT(read.Size() == 0U);
}

void test_pipe_stream_reports_live_capacity_after_consumer_progress()
{
  using namespace LibXR;

  WritePort w(3, 8);
  w = PendingWriteFun;

  static const uint8_t OLD[] = {0x10, 0x11, 0x12, 0x13};
  WriteOperation old_op;
  ASSERT(w(ConstRawData{OLD, sizeof(OLD)}, old_op) == ErrorCode::OK);

  WriteOperation stream_op;
  WritePort::Stream stream(&w, stream_op);
  static const uint8_t FIRST[] = {0x20, 0x21, 0x22, 0x23};
  ASSERT(stream.Write(ConstRawData{FIRST, sizeof(FIRST)}) == ErrorCode::OK);
  ASSERT(stream.EmptySize() == 0);

  ASSERT(CompleteFrontWrite(w, ErrorCode::OK));

  ASSERT(stream.EmptySize() == sizeof(OLD));
  static const uint8_t SECOND[] = {0x30, 0x31, 0x32, 0x33};
  ASSERT(stream.Write(ConstRawData{SECOND, sizeof(SECOND)}) == ErrorCode::OK);
  ASSERT(stream.EmptySize() == 0);
  ASSERT(stream.Commit() == ErrorCode::OK);

  uint8_t combined[sizeof(FIRST) + sizeof(SECOND)] = {};
  size_t offset = 0U;
  {
    auto queue = w.GetWriteQueue();
    ASSERT(queue.front_size == sizeof(combined));
    ASSERT(queue.next_size == 0U);
    ASSERT(queue.PopWithWriter(sizeof(combined),
                               [&](const uint8_t* first, size_t first_size,
                                   const uint8_t* second, size_t second_size)
                               {
                                 std::memcpy(combined + offset, first, first_size);
                                 offset += first_size;
                                 if (second_size != 0U)
                                 {
                                   std::memcpy(combined + offset, second, second_size);
                                   offset += second_size;
                                 }
                                 return first_size + second_size;
                               }) == sizeof(combined));
  }
  static const uint8_t EXPECTED[] = {0x20, 0x21, 0x22, 0x23, 0x30, 0x31, 0x32, 0x33};
  ASSERT(std::memcmp(combined, EXPECTED, sizeof(EXPECTED)) == 0);
  ASSERT(w.Size() == 0U);
}

/**
 * @brief 测试项函数 `RunBasePipeStreamTests`。 Test-item function
 * `RunBasePipeStreamTests`.
 * @details 测试内容：执行当前分组里的 `rw`/`pipe` 子场景。 Execute the grouped
 * `rw`/`pipe` sub-scenarios for this split file.
 *          测试原理：把同类状态机场景收在一组，降低单文件体积并保留聚合入口。 Group
 * related state-machine scenarios together to shrink file size while preserving
 * aggregated entrypoints.
 */
void RunBasePipeStreamTests()
{
  test_pipe_stream_block_immediate_path();
  test_pipe_stream_commit_releases_lock_for_next_stream();
  test_pipe_stream_commit_allows_persistent_and_external_streams();
  test_pipe_stream_payload_is_invisible_before_commit();
  test_pipe_write_completion_preserves_committed_payload_until_read_completes();
  test_pipe_stream_callback_can_reuse_same_stream();
  test_pipe_publishes_read_scope_before_write_settlement_callback();
  test_pipe_stream_reports_live_capacity_after_consumer_progress();
}
