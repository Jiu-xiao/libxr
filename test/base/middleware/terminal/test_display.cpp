/**
 * @file test_display.cpp
 * @brief `Terminal` 显示与历史渲染辅助测试。 `Terminal` display and history-rendering helper tests.
 *
 * 测试项目 / Test items:
 * 1. 三种换行模式输出。 Line-feed modes: verify `CRLF`, `LF` and `CR` render the correct line ending.
 * 2. prompt、清行和清屏序列。 Prompt and clear sequences: verify header rendering, clear-line and clear-screen escape output.
 * 3. 历史显示与恢复。 History display/restore: verify history recall renders the selected line and `CopyHistoryToInputLine()` restores the buffer state.
 * 4. 中途插入/删除时的重绘后缀。 Mid-line redraw: verify insertion and deletion with a non-zero cursor offset emit the expected redraw suffix.
 *
 * 测试原理 / Test principles:
 * 1. 把真实 write stream 刷到 pipe 后检查输出字节，因为这一层的契约是精确 escape 序列。 Flush the real terminal write stream into a pipe and inspect the emitted bytes, because this layer's contract is defined by exact rendered escape sequences.
 * 2. 配合内部 buffer/cursor 状态检查，保证可见输出和保留状态一致。 Pair rendered output checks with internal buffer/cursor checks so the test covers both visible and retained editor state.
 */
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

/**
 * @brief 测试项函数 `TestLineFeedModes`。 Test-item function `TestLineFeedModes`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestLineFeedModes()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  TerminalDisplayFixture<LibXR::Terminal<>::Mode::CRLF> crlf_fixture;
  crlf_fixture.terminal.LineFeed();
  ASSERT(crlf_fixture.FlushOutput() == "\r\n");

  TerminalDisplayFixture<LibXR::Terminal<>::Mode::LF> lf_fixture;
  lf_fixture.terminal.LineFeed();
  ASSERT(lf_fixture.FlushOutput() == "\n");

  TerminalDisplayFixture<LibXR::Terminal<>::Mode::CR> cr_fixture;
  cr_fixture.terminal.LineFeed();
  ASSERT(cr_fixture.FlushOutput() == "\r");
}

/**
 * @brief 测试项函数 `TestHeaderAndClearSequences`。 Test-item function `TestHeaderAndClearSequences`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestHeaderAndClearSequences()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  TerminalDisplayFixture<LibXR::Terminal<>::Mode::CRLF> fixture;

  fixture.terminal.ShowHeader();
  ASSERT(fixture.FlushOutput() == "ramfs:/$ ");

  auto dir = LibXR::RamFS::CreateDir("dir1");
  fixture.ramfs.Add(dir);
  fixture.terminal.current_dir_ = &dir;
  fixture.terminal.ShowHeader();
  ASSERT(fixture.FlushOutput() == "ramfs:dir1$ ");

  fixture.terminal.ClearLine();
  ASSERT(fixture.FlushOutput() == "\033[2K\r");

  fixture.terminal.Clear();
  ASSERT(fixture.FlushOutput() == "\033[2J\033[1H");
}

/**
 * @brief 测试项函数 `TestHistoryDisplayAndRestore`。 Test-item function `TestHistoryDisplayAndRestore`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestHistoryDisplayAndRestore()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  TerminalDisplayFixture<LibXR::Terminal<>::Mode::CRLF> fixture;

  FillInputLine(fixture.terminal, "alpha");
  fixture.terminal.AddHistory();
  FillInputLine(fixture.terminal, "beta");
  fixture.terminal.AddHistory();

  fixture.terminal.history_index_ = 0;
  fixture.terminal.offset_ = -3;
  fixture.terminal.ShowHistory();
  ASSERT(fixture.terminal.offset_ == 0);
  ASSERT(fixture.FlushOutput() == "\033[2K\rramfs:/$ beta");

  fixture.terminal.history_index_ = 1;
  fixture.terminal.ShowHistory();
  ASSERT(fixture.FlushOutput() == "\033[2K\rramfs:/$ alpha");

  fixture.terminal.CopyHistoryToInputLine();
  ASSERT(fixture.terminal.history_index_ == -1);
  ASSERT(fixture.terminal.offset_ == 0);
  ASSERT(fixture.terminal.input_line_.Size() == 5);
  ASSERT(std::strcmp(&fixture.terminal.input_line_[0], "alpha") == 0);
}

/**
 * @brief 测试项函数 `TestMidLineDisplayEditing`。 Test-item function `TestMidLineDisplayEditing`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestMidLineDisplayEditing()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  TerminalDisplayFixture<LibXR::Terminal<>::Mode::CRLF> fixture;

  FillInputLine(fixture.terminal, "ab");
  fixture.terminal.offset_ = -1;
  fixture.terminal.DisplayChar('X');
  ASSERT(std::strcmp(&fixture.terminal.input_line_[0], "aXb") == 0);
  ASSERT(fixture.FlushOutput() == "X\033[s\033[Kb\033[u");

  fixture.terminal.DeleteChar();
  ASSERT(std::strcmp(&fixture.terminal.input_line_[0], "ab") == 0);
  ASSERT(fixture.FlushOutput() == "\b \b\033[s\033[Kb\033[u");
}

}  // namespace

/**
 * @brief 测试入口函数 `test_terminal_display`。 Test entry function `test_terminal_display`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_terminal_display()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestLineFeedModes();
  TestHeaderAndClearSequences();
  TestHistoryDisplayAndRestore();
  TestMidLineDisplayEditing();
}
