/**
 * @file test_command.cpp
 * @brief Terminal 内置命令测试 / Terminal built-in command tests.
 *
 * 检查 cd 的目录与提示符变化，以及 ls 对当前目录中不同节点的显示。
 * Check directory and prompt changes from cd, and ls output for the current directory's
 * node types.
 */

#include "middleware/terminal/terminal_session_test_common.hpp"
#include "test_assert.hpp"

namespace
{

void TestCdBuiltins()
{
  // 切换目录后，当前目录和提示符都应更新；无效路径不能改变当前目录。
  // Changing directories updates both location and prompt; an invalid path leaves the
  // location unchanged.
  TerminalFixture fixture;

  auto dir1 = LibXR::RamFS::CreateDir("dir1");
  auto dir2 = LibXR::RamFS::CreateDir("dir2");
  fixture.ramfs.Add(dir1);
  dir1.Add(dir2);

  auto output = fixture.SendText("cd dir1\n");
  TEST_ASSERT(fixture.terminal.current_dir_ == &dir1);
  TEST_ASSERT(output.find("ramfs:dir1$ ") != std::string::npos);

  output = fixture.SendText("cd .\n");
  TEST_ASSERT(fixture.terminal.current_dir_ == &dir1);
  TEST_ASSERT(output.find("ramfs:dir1$ ") != std::string::npos);

  output = fixture.SendText("cd dir2\n");
  TEST_ASSERT(fixture.terminal.current_dir_ == &dir2);
  TEST_ASSERT(output.find("ramfs:dir2$ ") != std::string::npos);

  output = fixture.SendText("cd ..\n");
  TEST_ASSERT(fixture.terminal.current_dir_ == &dir1);
  TEST_ASSERT(output.find("ramfs:dir1$ ") != std::string::npos);

  output = fixture.SendText("cd /\n");
  TEST_ASSERT(fixture.terminal.current_dir_ == &fixture.ramfs.root_);
  TEST_ASSERT(output.find("ramfs:/$ ") != std::string::npos);

  output = fixture.SendText("cd missing\n");
  TEST_ASSERT(fixture.terminal.current_dir_ == &fixture.ramfs.root_);
  TEST_ASSERT(output.find("Command not found.") == std::string::npos);
  TEST_ASSERT(output.find("ramfs:/$ ") != std::string::npos);
}

void TestLsBuiltin()
{
  // 分别列出根目录和子目录，检查文件、目录和命令的显示标记。
  // List the root and a subdirectory, checking the markers for files, directories and
  // commands.
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
  TEST_ASSERT(output.find("d dir1") != std::string::npos);
  TEST_ASSERT(output.find("x run") != std::string::npos);
  TEST_ASSERT(output.find("f file") != std::string::npos);
  TEST_ASSERT(output.find("? custom") != std::string::npos);
  TEST_ASSERT(output.find("ramfs:/$ ") != std::string::npos);

  output = fixture.SendText("cd dir1\n");
  TEST_ASSERT(fixture.terminal.current_dir_ == &dir1);

  output = fixture.SendText("ls\n");
  TEST_ASSERT(output.find("d nested") != std::string::npos);
  TEST_ASSERT(output.find("x run") == std::string::npos);
  TEST_ASSERT(output.find("f file") == std::string::npos);
  TEST_ASSERT(output.find("? custom") == std::string::npos);
  TEST_ASSERT(output.find("ramfs:dir1$ ") != std::string::npos);
}

}  // namespace

void test_terminal_command()
{
  TestCdBuiltins();
  TestLsBuiltin();
}
