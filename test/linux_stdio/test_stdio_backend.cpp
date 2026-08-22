#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "linux_stdio_print_test_common.hpp"

namespace LibXR::Detail
{
bool ServiceStdoOnce(WritePort& write_port, int output_fd);
}

namespace LibXRLinuxStdioPrintTest
{
namespace
{
void QueueOnlyWriteFun(LibXR::WritePort&, bool) {}

struct ReentrantWriteContext
{
  static constexpr size_t WRITE_COUNT = 3U;

  LibXR::WritePort* port;
  std::array<LibXR::WriteOperation*, WRITE_COUNT> operations;
  std::array<LibXR::ConstRawData, WRITE_COUNT> data;
  std::array<LibXR::ErrorCode, WRITE_COUNT> submit_results = {
      LibXR::ErrorCode::FAILED, LibXR::ErrorCode::FAILED, LibXR::ErrorCode::FAILED};
  size_t completion_count = 0U;
};

void QueueThirdWrite(bool, ReentrantWriteContext* context, LibXR::ErrorCode result)
{
  ASSERT(result == LibXR::ErrorCode::OK);
  context->completion_count++;
  for (size_t i = 0U; i < ReentrantWriteContext::WRITE_COUNT; ++i)
  {
    context->submit_results[i] =
        (*context->port)(context->data[i], *context->operations[i]);
  }
}

struct CompletionContext
{
  LibXR::ErrorCode result = LibXR::ErrorCode::OK;
  size_t count = 0U;
};

void RecordCompletion(bool, CompletionContext* context, LibXR::ErrorCode result)
{
  context->result = result;
  context->count++;
}

void ReadExact(int fd, uint8_t* output, size_t size)
{
  size_t offset = 0U;
  while (offset < size)
  {
    const ssize_t read_size = read(fd, output + offset, size - offset);
    if (read_size > 0)
    {
      offset += static_cast<size_t>(read_size);
      continue;
    }
    if (read_size < 0 && errno == EINTR)
    {
      continue;
    }
    ASSERT(false);
  }
}
}  // namespace

void TestStdioBackendTurns()
{
  using namespace LibXR;

  int pipe_fds[2] = {-1, -1};
  ASSERT(pipe(pipe_fds) == 0);

  WritePort port(4U, 16U);
  port = QueueOnlyWriteFun;
  OperationPollingStatus second_status;
  OperationPollingStatus third_status;
  OperationPollingStatus fourth_status;
  OperationPollingStatus fifth_status;
  WriteOperation second_operation(second_status);
  WriteOperation third_operation(third_status);
  WriteOperation fourth_operation(fourth_status);
  WriteOperation fifth_operation(fifth_status);
  static constexpr uint8_t FIRST[] = {0x11U, 0x12U};
  static constexpr uint8_t SECOND[] = {0x21U};
  static constexpr uint8_t THIRD[] = {0x31U, 0x32U};
  static constexpr uint8_t FOURTH[] = {0x41U};
  static constexpr uint8_t FIFTH[] = {0x51U, 0x52U, 0x53U};
  ReentrantWriteContext context{
      &port,
      {&third_operation, &fourth_operation, &fifth_operation},
      {ConstRawData{THIRD, sizeof(THIRD)}, ConstRawData{FOURTH, sizeof(FOURTH)},
       ConstRawData{FIFTH, sizeof(FIFTH)}}};
  auto first_callback = Callback<ErrorCode>::Create(QueueThirdWrite, &context);
  WriteOperation first_operation(first_callback);

  ASSERT(port(ConstRawData{FIRST, sizeof(FIRST)}, first_operation) == ErrorCode::OK);
  ASSERT(port(ConstRawData{SECOND, sizeof(SECOND)}, second_operation) == ErrorCode::OK);

  ASSERT(Detail::ServiceStdoOnce(port, pipe_fds[1]));
  std::array<uint8_t, sizeof(FIRST) + sizeof(SECOND)> first_turn{};
  ReadExact(pipe_fds[0], first_turn.data(), first_turn.size());
  static constexpr std::array<uint8_t, sizeof(FIRST) + sizeof(SECOND)> FIRST_EXPECTED = {
      0x11U, 0x12U, 0x21U};
  ASSERT(first_turn == FIRST_EXPECTED);
  ASSERT(context.completion_count == 1U);
  ASSERT(context.submit_results[0] == ErrorCode::OK);
  ASSERT(context.submit_results[1] == ErrorCode::OK);
  ASSERT(context.submit_results[2] == ErrorCode::OK);
  ASSERT(second_status == OperationPollingStatus::DONE);
  ASSERT(third_status == OperationPollingStatus::RUNNING);
  ASSERT(fourth_status == OperationPollingStatus::RUNNING);
  ASSERT(fifth_status == OperationPollingStatus::RUNNING);
  ASSERT(port.Size() == sizeof(THIRD) + sizeof(FOURTH) + sizeof(FIFTH));

  ASSERT(Detail::ServiceStdoOnce(port, pipe_fds[1]));
  std::array<uint8_t, sizeof(THIRD) + sizeof(FOURTH)> second_turn{};
  ReadExact(pipe_fds[0], second_turn.data(), second_turn.size());
  static constexpr std::array<uint8_t, sizeof(THIRD) + sizeof(FOURTH)> SECOND_EXPECTED = {
      0x31U, 0x32U, 0x41U};
  ASSERT(second_turn == SECOND_EXPECTED);
  ASSERT(third_status == OperationPollingStatus::DONE);
  ASSERT(fourth_status == OperationPollingStatus::DONE);
  ASSERT(fifth_status == OperationPollingStatus::RUNNING);
  ASSERT(port.Size() == sizeof(FIFTH));

  ASSERT(!Detail::ServiceStdoOnce(port, pipe_fds[1]));
  std::array<uint8_t, sizeof(FIFTH)> third_turn{};
  ReadExact(pipe_fds[0], third_turn.data(), third_turn.size());
  static constexpr std::array<uint8_t, sizeof(FIFTH)> THIRD_EXPECTED = {0x51U, 0x52U,
                                                                        0x53U};
  ASSERT(third_turn == THIRD_EXPECTED);
  ASSERT(fifth_status == OperationPollingStatus::DONE);
  ASSERT(port.Size() == 0U);

  CompletionContext failure_context;
  auto failure_callback = Callback<ErrorCode>::Create(RecordCompletion, &failure_context);
  WriteOperation failure_operation(failure_callback);
  static constexpr uint8_t FAILURE[] = {0x61U};
  ASSERT(port(ConstRawData{FAILURE, sizeof(FAILURE)}, failure_operation) ==
         ErrorCode::OK);
  ASSERT(!Detail::ServiceStdoOnce(port, -1));
  ASSERT(failure_context.count == 1U);
  ASSERT(failure_context.result == ErrorCode::FAILED);
  ASSERT(port.Size() == 0U);

  ASSERT(close(pipe_fds[0]) == 0);
  ASSERT(close(pipe_fds[1]) == 0);
}
}  // namespace LibXRLinuxStdioPrintTest
