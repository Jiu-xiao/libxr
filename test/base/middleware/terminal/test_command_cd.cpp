/**
 * @file test_command_cd.cpp
 * @brief `Terminal` `cd` 内建命令场景子测试。 Split test unit for `Terminal` `cd` builtin scenarios.
 * @details 测试项目：
 *          1. 相对路径、`.`、`..`、根路径切换。
 *          2. 无效路径不会破坏当前目录与 prompt。
 *          Test items:
 *          1. Relative-path, `.`, `..`, and root-path transitions.
 *          2. Invalid paths do not corrupt the current directory or prompt.
 */
#include "terminal_session_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestCdBuiltins`。 Test-item function `TestCdBuiltins`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestCdBuiltins()
{
  // 测试内容：验证 `cd` 路径切换后的目录状态与 prompt 输出。
  // Test coverage: verify directory state and prompt output after `cd` path transitions.
  TerminalFixture fixture;

  auto dir1 = LibXR::RamFS::CreateDir("dir1");
  auto dir2 = LibXR::RamFS::CreateDir("dir2");
  fixture.ramfs.Add(dir1);
  dir1.Add(dir2);

  auto output = fixture.SendText("cd dir1\n");
  ASSERT(fixture.terminal.current_dir_ == &dir1);
  ASSERT(output.find("ramfs:dir1$ ") != std::string::npos);

  output = fixture.SendText("cd .\n");
  ASSERT(fixture.terminal.current_dir_ == &dir1);
  ASSERT(output.find("ramfs:dir1$ ") != std::string::npos);

  output = fixture.SendText("cd dir2\n");
  ASSERT(fixture.terminal.current_dir_ == &dir2);
  ASSERT(output.find("ramfs:dir2$ ") != std::string::npos);

  output = fixture.SendText("cd ..\n");
  ASSERT(fixture.terminal.current_dir_ == &dir1);
  ASSERT(output.find("ramfs:dir1$ ") != std::string::npos);

  output = fixture.SendText("cd /\n");
  ASSERT(fixture.terminal.current_dir_ == &fixture.ramfs.root_);
  ASSERT(output.find("ramfs:/$ ") != std::string::npos);

  output = fixture.SendText("cd missing\n");
  ASSERT(fixture.terminal.current_dir_ == &fixture.ramfs.root_);
  ASSERT(output.find("Command not found.") == std::string::npos);
  ASSERT(output.find("ramfs:/$ ") != std::string::npos);
}

}  // namespace

/**
 * @brief 测试项函数 `RunTerminalCommandCdTests`。 Test-item function `RunTerminalCommandCdTests`.
 * @details 测试内容：执行 `Terminal` `cd` 内建命令子场景。 Execute `Terminal` `cd` builtin sub-scenarios.
 *          测试原理：把目录切换语义单独成组，避免与 `ls` 输出标记场景缠绕。 Group directory-transition semantics away from `ls` marker output scenarios.
 */
void RunTerminalCommandCdTests()
{
  TestCdBuiltins();
}
