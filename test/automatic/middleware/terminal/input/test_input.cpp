/**
 * @file test_input.cpp
 * @brief Terminal 输入处理测试 / Terminal input handling tests.
 *
 * 通过 Pipe 输入按键，检查 CRLF 只执行一次、历史切换，以及移动光标后插入字符。
 * Feed keys through a Pipe to check single execution for CRLF, history navigation and
 * cursor-based insertion.
 */

#include "middleware/terminal/terminal_session_test_common.hpp"
#include "test_assert.hpp"

namespace
{

void TestMidLineInputEditing()
{
  // 把光标移到行中再输入，检查插入后的命令文字和实际执行的命令名一致。
  // Type in the middle of a line and check that the edited text matches the command
  // actually executed.
  TerminalFixture fixture;

  int acbd_count = 0;
  CommandState acbd_state{"acbd", &acbd_count};
  auto acbd_cmd =
      LibXR::RamFS::CreateCommand<CommandState*>("acbd", CountCommand, &acbd_state);
  fixture.ramfs.Add(acbd_cmd);

  constexpr char KEY_LEFT_LEFT[] = "\033[D\033[D";

  fixture.SendText("abd");
  auto cursor_moves = fixture.SendRaw(KEY_LEFT_LEFT, sizeof(KEY_LEFT_LEFT) - 1);
  TEST_ASSERT(CountSubstring(cursor_moves, "\033[D") == 2);
  fixture.SendText("c\n");
  TEST_ASSERT(acbd_count == 1);
}

void TestInputCrLfAndHistory()
{
  // CRLF 只执行一次命令；上下箭头取回历史后，检查再次执行的次数和回显。
  // CRLF executes once; after recalling history with arrows, check execution counts and
  // echo.
  TerminalFixture fixture;

  int one_count = 0;
  int two_count = 0;
  CommandState one_state{"one", &one_count};
  CommandState two_state{"two", &two_count};

  auto one_cmd =
      LibXR::RamFS::CreateCommand<CommandState*>("one", CountCommand, &one_state);
  auto two_cmd =
      LibXR::RamFS::CreateCommand<CommandState*>("two", CountCommand, &two_state);

  fixture.ramfs.Add(one_cmd);
  fixture.ramfs.Add(two_cmd);

  fixture.SendText("one\r\n");
  TEST_ASSERT(one_count == 1);
  TEST_ASSERT(two_count == 0);

  fixture.SendText("two\n");
  TEST_ASSERT(two_count == 1);

  constexpr char KEY_UP[] = "\033[A";
  constexpr char KEY_DOWN[] = "\033[B";

  auto newest_history = fixture.SendRaw(KEY_UP, sizeof(KEY_UP) - 1);
  TEST_ASSERT(newest_history.find("\033[2K\r") != std::string::npos);
  TEST_ASSERT(newest_history.find("two") != std::string::npos);
  fixture.SendText("\n");
  TEST_ASSERT(two_count == 2);

  fixture.SendRaw(KEY_UP, sizeof(KEY_UP) - 1);
  fixture.SendRaw(KEY_UP, sizeof(KEY_UP) - 1);
  auto older_history = fixture.SendRaw(KEY_UP, sizeof(KEY_UP) - 1);
  TEST_ASSERT(older_history.find("one") != std::string::npos);
  auto move_forward_history = fixture.SendRaw(KEY_DOWN, sizeof(KEY_DOWN) - 1);
  TEST_ASSERT(move_forward_history.find("two") != std::string::npos);
  fixture.SendText("\n");
  TEST_ASSERT(two_count == 3);
}

}  // namespace

void test_terminal_input()
{
  TestInputCrLfAndHistory();
  TestMidLineInputEditing();
}
