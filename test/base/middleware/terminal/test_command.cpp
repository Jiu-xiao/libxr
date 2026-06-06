/**
 * @file test_command.cpp
 * @brief `Terminal` 内建命令测试。 `Terminal` built-in command tests.
 *
 * 测试项目 / Test items:
 * 1. `cd` 的相对、`.`、`..`、根路径和无效路径行为。 `cd` built-ins: verify relative path, `.`, `..`, root-path and invalid-path transitions update `current_dir_` and prompt output correctly.
 * 2. `ls` 对目录/可执行/普通文件/自定义节点的输出标记。 `ls` built-in: verify node-type markers for directory, executable file, ordinary file and custom node, and verify listing scope follows the current directory.
 *
 * 测试原理 / Test principles:
 * 1. 通过 `Pipe` 喂命令并驱动 `TaskFun()`，保证解析、执行和 prompt 刷新走真实路径。 Feed commands through a `Pipe` and drive `Terminal::TaskFun()` so parsing, execution and prompt refresh all happen on the production path.
 * 2. 同时检查内部目录状态和终端输出，因为命令契约既有状态变化也有用户可见文本。 Check both internal directory state and rendered output, because command correctness here is both semantic state change and user-visible terminal behavior.
 */
#include <cstring>
#include <string>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "test.hpp"

namespace
{

struct TerminalCommandFixture
{
  LibXR::RamFS ramfs;
  LibXR::Pipe input;
  LibXR::Pipe output;
  LibXR::Terminal<> terminal;

  TerminalCommandFixture()
      : input(128),
        output(2048),
        terminal(ramfs, nullptr, &input.GetReadPort(), &output.GetWritePort())
  {
  }

  void RunUntilIdle()
  {
  // 基准内容：执行当前子场景或 case。
  // Benchmark coverage: execute the current benchmark sub-case.
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

  std::string SendText(const char* text)
  {
    LibXR::WriteOperation write_op;
    ASSERT(input.GetWritePort()(LibXR::ConstRawData{text, std::strlen(text)}, write_op) ==
           LibXR::ErrorCode::OK);
    RunUntilIdle();
    return DrainOutput();
  }
};

void TestCdBuiltins()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  TerminalCommandFixture fixture;

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

void TestLsBuiltin()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  TerminalCommandFixture fixture;

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

void test_terminal_command()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestCdBuiltins();
  TestLsBuiltin();
}
