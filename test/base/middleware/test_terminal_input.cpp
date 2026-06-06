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

int CountCommand(CommandState* state, int argc, char** argv)
{
  ASSERT(state != nullptr);
  ASSERT(state->count != nullptr);
  ASSERT(argc == 1);
  ASSERT(std::strcmp(argv[0], state->expected_name) == 0);
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

  std::string SendRaw(const void* data, size_t size)
  {
    LibXR::WriteOperation write_op;
    ASSERT(input.GetWritePort()(LibXR::ConstRawData{data, size}, write_op) ==
           LibXR::ErrorCode::OK);
    RunUntilIdle();
    return DrainOutput();
  }

  std::string SendText(const char* text) { return SendRaw(text, std::strlen(text)); }
};

}  // namespace

void test_terminal_input()
{
  TerminalFixture fixture;

  int one_count = 0;
  int two_count = 0;
  int acbd_count = 0;

  CommandState one_state{"one", &one_count};
  CommandState two_state{"two", &two_count};
  CommandState acbd_state{"acbd", &acbd_count};

  auto one_cmd = LibXR::RamFS::CreateCommand<CommandState*>("one", CountCommand, &one_state);
  auto two_cmd = LibXR::RamFS::CreateCommand<CommandState*>("two", CountCommand, &two_state);
  auto acbd_cmd =
      LibXR::RamFS::CreateCommand<CommandState*>("acbd", CountCommand, &acbd_state);

  fixture.ramfs.Add(one_cmd);
  fixture.ramfs.Add(two_cmd);
  fixture.ramfs.Add(acbd_cmd);

  fixture.SendText("one\r\n");
  ASSERT(one_count == 1);
  ASSERT(two_count == 0);

  fixture.SendText("two\n");
  ASSERT(two_count == 1);

  constexpr char KEY_UP[] = "\033[A";
  constexpr char KEY_DOWN[] = "\033[B";
  constexpr char KEY_LEFT_LEFT[] = "\033[D\033[D";

  auto newest_history = fixture.SendRaw(KEY_UP, sizeof(KEY_UP) - 1);
  ASSERT(newest_history.find("\033[2K\r") != std::string::npos);
  ASSERT(newest_history.find("two") != std::string::npos);
  fixture.SendText("\n");
  ASSERT(two_count == 2);

  fixture.SendRaw(KEY_UP, sizeof(KEY_UP) - 1);
  auto older_history = fixture.SendRaw(KEY_UP, sizeof(KEY_UP) - 1);
  ASSERT(older_history.find("one") != std::string::npos);
  auto move_forward_history = fixture.SendRaw(KEY_DOWN, sizeof(KEY_DOWN) - 1);
  ASSERT(move_forward_history.find("two") != std::string::npos);
  fixture.SendText("\n");
  ASSERT(two_count == 3);

  fixture.SendText("abd");
  auto cursor_moves = fixture.SendRaw(KEY_LEFT_LEFT, sizeof(KEY_LEFT_LEFT) - 1);
  ASSERT(CountSubstring(cursor_moves, "\033[D") == 2);
  fixture.SendText("c\n");
  ASSERT(acbd_count == 1);
}
