/**
 * @file terminal_display_test_common.hpp
 * @brief `Terminal` 显示类测试共用 helper。 Shared helpers for `Terminal` display-oriented tests.
 * @details 测试项目：
 *          1. 提供不同换行模式下的显示 fixture。
 *          2. 提供输入行填充和输出刷出 helper。
 *          Test items:
 *          1. Provide display fixtures for different line-feed modes.
 *          2. Provide input-line fill and output-flush helpers.
 */
#pragma once

#include <cstring>
#include <string>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "test.hpp"

namespace
{

template <LibXR::Terminal<>::Mode ModeValue>
struct TerminalDisplayFixture
{
  LibXR::RamFS ramfs;
  LibXR::Pipe input;
  LibXR::Pipe output;
  LibXR::Terminal<> terminal;

  TerminalDisplayFixture()
      : input(64),
        output(1024),
        terminal(ramfs, nullptr, &input.GetReadPort(), &output.GetWritePort(), ModeValue)
  {
  }

  /**
   * @brief 辅助函数 `FlushOutput`。 Helper function `FlushOutput`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  std::string FlushOutput()
  {
    ASSERT(terminal.write_stream_.Commit() == LibXR::ErrorCode::OK);

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
};

/**
 * @brief 辅助函数 `FillInputLine`。 Helper function `FillInputLine`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
void FillInputLine(LibXR::Terminal<>& terminal, const char* text)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  terminal.input_line_.Reset();
  const size_t len = std::strlen(text);
  for (size_t i = 0; i < len; ++i)
  {
    terminal.input_line_.Push(text[i]);
  }
  terminal.input_line_[terminal.input_line_.Size()] = '\0';
}

}  // namespace
