/**
 * @file test_pipe_stream.cpp
 * @brief base `Pipe` stream 语义场景子测试。 Split test unit for base `Pipe` stream
 * semantics.
 */
#include <type_traits>

#include "rw_test_common.hpp"

namespace
{

LibXR::WritePort::Stream* callback_stream = nullptr;
LibXR::ErrorCode callback_direct_write = LibXR::ErrorCode::FAILED;
LibXR::ErrorCode callback_stream_write = LibXR::ErrorCode::FAILED;
LibXR::ErrorCode callback_stream_commit = LibXR::ErrorCode::FAILED;
uint8_t callback_payload[8] = {};
size_t callback_payload_size = 0;
bool callback_try_reentry = false;

LibXR::ErrorCode ConsumeWriteAndTryReentry(LibXR::WritePort& port, bool)
{
  using namespace LibXR;

  WriteInfoBlock info{};
  ASSERT(port.queue_info_->Pop(info) == ErrorCode::OK);
  ASSERT(info.data.size_ <= sizeof(callback_payload));
  callback_payload_size = info.data.size_;
  ASSERT(port.queue_data_->PopBatch(callback_payload, callback_payload_size) ==
         ErrorCode::OK);

  if (callback_try_reentry)
  {
    static const uint8_t NESTED[] = {0xA1};
    WriteOperation direct_op;
    callback_direct_write = port(ConstRawData{NESTED, sizeof(NESTED)}, direct_op, false);
    callback_stream_write = callback_stream->Write(ConstRawData{NESTED, sizeof(NESTED)});
    callback_stream_commit = callback_stream->Commit();
  }

  return ErrorCode::OK;
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
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
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
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
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
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);
}

void test_pipe_stream_rejects_same_object_callback_reentry()
{
  using namespace LibXR;

  WritePort w(2, 16);
  w = ConsumeWriteAndTryReentry;

  static const uint8_t OUTER[] = {0x31, 0x32, 0x33};
  WriteOperation op;
  WritePort::Stream stream(&w, op);
  callback_stream = &stream;
  callback_try_reentry = true;

  ASSERT(stream.Write(ConstRawData{OUTER, sizeof(OUTER)}) == ErrorCode::OK);
  ASSERT(stream.Commit() == ErrorCode::OK);

  ASSERT(callback_direct_write == ErrorCode::BUSY);
  ASSERT(callback_stream_write == ErrorCode::BUSY);
  ASSERT(callback_stream_commit == ErrorCode::BUSY);
  ASSERT(callback_payload_size == sizeof(OUTER));
  ASSERT(std::memcmp(callback_payload, OUTER, sizeof(OUTER)) == 0);
  ASSERT(w.queue_data_->Size() == 0);
  ASSERT(w.queue_info_->Size() == 0);

  callback_try_reentry = false;
  static const uint8_t REUSE[] = {0x41, 0x42};
  ASSERT(stream.Write(ConstRawData{REUSE, sizeof(REUSE)}) == ErrorCode::OK);
  ASSERT(stream.Commit() == ErrorCode::OK);
  ASSERT(callback_payload_size == sizeof(REUSE));
  ASSERT(std::memcmp(callback_payload, REUSE, sizeof(REUSE)) == 0);
  ASSERT(w.busy_.load(std::memory_order_acquire) == WritePort::BusyState::IDLE);

  callback_stream = nullptr;
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

  WriteInfoBlock old_info{};
  ASSERT(w.queue_info_->Pop(old_info) == ErrorCode::OK);
  ASSERT(w.queue_data_->PopBatch(nullptr, old_info.data.size_) == ErrorCode::OK);
  w.Finish(false, ErrorCode::OK, old_info);

  ASSERT(stream.EmptySize() == sizeof(OLD));
  static const uint8_t SECOND[] = {0x30, 0x31, 0x32, 0x33};
  ASSERT(stream.Write(ConstRawData{SECOND, sizeof(SECOND)}) == ErrorCode::OK);
  ASSERT(stream.EmptySize() == 0);
  ASSERT(stream.Commit() == ErrorCode::OK);

  WriteInfoBlock combined_info{};
  ASSERT(w.queue_info_->Pop(combined_info) == ErrorCode::OK);
  ASSERT(combined_info.data.size_ == sizeof(FIRST) + sizeof(SECOND));
  uint8_t combined[sizeof(FIRST) + sizeof(SECOND)] = {};
  ASSERT(w.queue_data_->PopBatch(combined, sizeof(combined)) == ErrorCode::OK);
  static const uint8_t EXPECTED[] = {0x20, 0x21, 0x22, 0x23, 0x30, 0x31, 0x32, 0x33};
  ASSERT(std::memcmp(combined, EXPECTED, sizeof(EXPECTED)) == 0);
  w.Finish(false, ErrorCode::OK, combined_info);
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
  test_pipe_stream_rejects_same_object_callback_reentry();
  test_pipe_stream_reports_live_capacity_after_consumer_progress();
}
