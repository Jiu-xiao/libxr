/**
 * @file test_command_ls.cpp
 * @brief `Terminal` `ls` 内建命令场景子测试。 Split test unit for `Terminal` `ls` builtin scenarios.
 * @details 测试项目：
 *          1. 根目录下不同节点类型的输出标记。
 *          2. 切换目录后 listing scope 跟随当前目录。
 *          Test items:
 *          1. Output markers for different node types under the root directory.
 *          2. Listing scope follows the current directory after changing directories.
 */
#include "terminal_session_test_common.hpp"

namespace
{

/**
 * @brief 测试项函数 `TestLsBuiltin`。 Test-item function `TestLsBuiltin`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestLsBuiltin()
{
  // 测试内容：验证 `ls` 在不同目录和节点类型下的输出标记与作用域。
  // Test coverage: verify `ls` output markers and scope under different directories and node types.
  TerminalFixture fixture;

  int file_value = 42;
  auto dir1 = LibXR::RamFS::CreateDir("dir1");
  auto run = LibXR::RamFS::CreateCommand<void*>(
      "run",
      [](void*, int argc, char** argv)
      {
        UNUSED(argc);
        UNUSED(argv);
        return 0;
      },
      nullptr);
  auto file = LibXR::RamFS::CreateFile("file", file_value);
  LibXR::RamFS::Custom custom("custom", 7);
  auto nested = LibXR::RamFS::CreateDir("nested");
  fixture.ramfs.Add(dir1);
  fixture.ramfs.Add(run);
  fixture.ramfs.Add(file);
  fixture.ramfs.Add(custom);
  dir1.Add(nested);

  auto output = fixture.SendText("ls\n");
  ASSERT(output.find("d dir1") != std::string::npos);
  ASSERT(output.find("x run") != std::string::npos);
  ASSERT(output.find("f file") != std::string::npos);
  ASSERT(output.find("? custom") != std::string::npos);
  ASSERT(output.find("ramfs:/$ ") != std::string::npos);

  output = fixture.SendText("cd dir1\n");
  ASSERT(fixture.terminal.current_dir_ == &dir1);

  output = fixture.SendText("ls\n");
  ASSERT(output.find("d nested") != std::string::npos);
  ASSERT(output.find("x run") == std::string::npos);
  ASSERT(output.find("f file") == std::string::npos);
  ASSERT(output.find("? custom") == std::string::npos);
  ASSERT(output.find("ramfs:dir1$ ") != std::string::npos);
}

}  // namespace

/**
 * @brief 测试项函数 `RunTerminalCommandLsTests`。 Test-item function `RunTerminalCommandLsTests`.
 * @details 测试内容：执行 `Terminal` `ls` 内建命令子场景。 Execute `Terminal` `ls` builtin sub-scenarios.
 *          测试原理：把 listing 作用域与节点类型标记单独成组，聚焦可见输出契约。 Group listing scope and node-type markers around the visible-output contract.
 */
void RunTerminalCommandLsTests()
{
  TestLsBuiltin();
}
