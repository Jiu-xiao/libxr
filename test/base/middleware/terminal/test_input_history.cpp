/**
 * @file test_input_history.cpp
 * @brief `Terminal` 输入 CRLF 与历史导航场景子测试。 Split test unit for `Terminal` CRLF-suppression and history-navigation scenarios.
 * @details 测试项目：
 *          1. `\r\n` 双字节提交只执行一次命令。
 *          2. `Up` / `Down` 历史导航回放新旧命令。
 *          Test items:
 *          1. One `\r\n` submission executes the command only once.
 *          2. `Up` / `Down` history navigation recalls newer and older commands.
 */
#include "terminal_session_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestInputCrLfAndHistory`。 Test-item function `TestInputCrLfAndHistory`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestInputCrLfAndHistory()
{
  // 测试内容：验证 CRLF 去重和上下箭头历史导航的执行副作用与回显。
  // Test coverage: verify CRLF suppression plus the side effects and echoes of up/down history navigation.
  TerminalFixture fixture;

  int one_count = 0;
  int two_count = 0;
  CommandState one_state{"one", &one_count};
  CommandState two_state{"two", &two_count};

  auto one_cmd = LibXR::RamFS::CreateCommand<CommandState*>("one", CountCommand, &one_state);
  auto two_cmd = LibXR::RamFS::CreateCommand<CommandState*>("two", CountCommand, &two_state);

  fixture.ramfs.Add(one_cmd);
  fixture.ramfs.Add(two_cmd);

  fixture.SendText("one\r\n");
  ASSERT(one_count == 1);
  ASSERT(two_count == 0);

  fixture.SendText("two\n");
  ASSERT(two_count == 1);

  constexpr char KEY_UP[] = "\033[A";
  constexpr char KEY_DOWN[] = "\033[B";

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
}

}  // namespace

/**
 * @brief 测试项函数 `RunTerminalInputHistoryTests`。 Test-item function `RunTerminalInputHistoryTests`.
 * @details 测试内容：执行 `Terminal` CRLF 与历史导航子场景。 Execute `Terminal` CRLF-suppression and history-navigation sub-scenarios.
 *          测试原理：把提交去重和历史导航单独成组，聚焦 parser 与 history 的联动契约。 Group submission suppression and history navigation around the parser/history interaction contract.
 */
void RunTerminalInputHistoryTests()
{
  TestInputCrLfAndHistory();
}
