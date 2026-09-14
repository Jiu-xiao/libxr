/**
 * @file terminal_session_test_common.hpp
 * @brief Terminal 输入与命令测试辅助代码 / Helpers for Terminal input and command tests.
 *
 * 使用 Pipe 发送输入并读取回显，手动推进终端任务，同时记录命令执行次数。
 * Send input and collect output through Pipes, step terminal processing and count command
 * calls.
 */

#pragma once

#include <cstring>
#include <string>
#include <string_view>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "test.hpp"
#include "test_assert.hpp"

namespace
{

struct CommandState
{
  const char* expected_name = nullptr;
  int* count = nullptr;
};

int CountCommand(CommandState* state, int argc, char** argv)
{
  TEST_ASSERT(state != nullptr);
  TEST_ASSERT(state->count != nullptr);
  TEST_ASSERT(argc == 1);
  TEST_ASSERT(std::strcmp(argv[0], state->expected_name) == 0);
  (*state->count)++;
  return 0;
}

size_t CountSubstring(std::string_view text, std::string_view needle)
{
  size_t count = 0;
  size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string_view::npos)
  {
    ++count;
    pos += needle.size();
  }
  return count;
}

struct TerminalFixture
{
  LibXR::RamFS ramfs;
  LibXR::Pipe input;
  LibXR::Pipe output;
  LibXR::Terminal<> terminal;

  TerminalFixture()
      : input(128),
        output(4096),
        terminal(ramfs, nullptr, &input.GetReadPort(), &output.GetWritePort())
  {
  }

  void RunUntilIdle()
  {
    // 输入耗尽且下一次读取已挂起，才算处理完；八次调用是防止测试卡住的上限。
    // Idle means input is drained and the next read is pending; eight calls bound a
    // stalled test.
    for (size_t i = 0; i < 8; ++i)
    {
      LibXR::Terminal<>::TaskFun(&terminal);
      if (input.GetReadPort().Size() == 0 &&
          terminal.read_status_ == LibXR::ReadOperation::OperationPollingStatus::RUNNING)
      {
        return;
      }
    }
    TEST_ASSERT(false);
  }

  std::string DrainOutput()
  {
    const size_t output_size = output.GetReadPort().Size();
    if (output_size == 0)
    {
      return {};
    }

    std::string text(output_size, '\0');
    LibXR::ReadOperation read_op;
    TEST_ASSERT(output.GetReadPort()(LibXR::RawData{text.data(), output_size}, read_op) ==
                LibXR::ErrorCode::OK);
    return text;
  }

  std::string SendRaw(const void* data, size_t size)
  {
    LibXR::WriteOperation write_op;
    TEST_ASSERT(input.GetWritePort()(LibXR::ConstRawData{data, size}, write_op) ==
                LibXR::ErrorCode::OK);
    RunUntilIdle();
    return DrainOutput();
  }

  std::string SendText(const char* text) { return SendRaw(text, std::strlen(text)); }
};

}  // namespace
