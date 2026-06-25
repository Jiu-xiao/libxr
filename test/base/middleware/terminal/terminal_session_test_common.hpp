/**
 * @file terminal_session_test_common.hpp
 * @brief `Terminal` 输入与命令测试共用 session helper。 Shared session helpers for `Terminal` input and command tests.
 * @details 测试项目：
 *          1. 提供命令计数上下文和 ANSI 输出计数 helper。
 *          2. 提供可发送原始输入/文本并驱动 `TaskFun()` 的交互 fixture。
 *          Test items:
 *          1. Provide command-count contexts and ANSI-substring counting helpers.
 *          2. Provide an interactive fixture that sends raw input/text and drives `TaskFun()`.
 */
#pragma once

#include <cstring>
#include <string>
#include <string_view>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "test.hpp"

namespace
{

struct CommandState
{
  const char* expected_name = nullptr;
  int* count = nullptr;
};

/**
 * @brief 辅助函数 `CountCommand`。 Helper function `CountCommand`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
int CountCommand(CommandState* state, int argc, char** argv)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  ASSERT(state != nullptr);
  ASSERT(state->count != nullptr);
  ASSERT(argc == 1);
  ASSERT(std::strcmp(argv[0], state->expected_name) == 0);
  (*state->count)++;
  return 0;
}

/**
 * @brief 辅助函数 `CountSubstring`。 Helper function `CountSubstring`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
size_t CountSubstring(std::string_view text, std::string_view needle)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
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

  /**
   * @brief 执行辅助函数 `RunUntilIdle`。 Execution helper function `RunUntilIdle`.
   * @details 测试内容：执行一个子 case、子流程或基准场景。 Execute one sub-case, sub-flow, or benchmark scenario.
   *          测试原理：把重复执行逻辑集中封装，保证不同 case 走同一执行路径。 Centralize repeated execution logic so different cases use the same execution path.
   */
  void RunUntilIdle()
  {
    // 基准内容：执行当前子场景或 case。
    // Benchmark coverage: execute the current benchmark sub-case.
    for (size_t i = 0; i < 8; ++i)
    {
      LibXR::Terminal<>::TaskFun(&terminal);
      if (input.GetReadPort().Size() == 0 &&
          terminal.read_status_ == LibXR::ReadOperation::OperationPollingStatus::RUNNING)
      {
        return;
      }
    }
    ASSERT(false);
  }

  /**
   * @brief 辅助函数 `DrainOutput`。 Helper function `DrainOutput`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  std::string DrainOutput()
  {
    const size_t output_size = output.GetReadPort().Size();
    if (output_size == 0)
    {
      return {};
    }

    std::string text(output_size, '\0');
    LibXR::ReadOperation read_op;
    ASSERT(output.GetReadPort()(LibXR::RawData{text.data(), output_size}, read_op) ==
           LibXR::ErrorCode::OK);
    return text;
  }

  /**
   * @brief 辅助函数 `SendRaw`。 Helper function `SendRaw`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  std::string SendRaw(const void* data, size_t size)
  {
    LibXR::WriteOperation write_op;
    ASSERT(input.GetWritePort()(LibXR::ConstRawData{data, size}, write_op) ==
           LibXR::ErrorCode::OK);
    RunUntilIdle();
    return DrainOutput();
  }

  /**
   * @brief 辅助函数 `SendText`。 Helper function `SendText`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  std::string SendText(const char* text) { return SendRaw(text, std::strlen(text)); }
};

}  // namespace
