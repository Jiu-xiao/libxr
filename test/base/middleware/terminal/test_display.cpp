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

void test_terminal_display()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestLineFeedModes();
  TestHeaderAndClearSequences();
  TestHistoryDisplayAndRestore();
  TestMidLineDisplayEditing();
}
