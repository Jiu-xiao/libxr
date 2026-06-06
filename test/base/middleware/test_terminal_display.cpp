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
  TestLineFeedModes();
  TestHeaderAndClearSequences();
  TestHistoryDisplayAndRestore();
  TestMidLineDisplayEditing();
}
