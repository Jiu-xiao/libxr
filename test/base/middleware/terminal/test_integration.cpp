/**
 * @file test_integration.cpp
 * @brief 混合 RamFS 树上的 `Terminal` 集成命令路径测试。 Integrated `Terminal` command-path test over a mixed RamFS tree.
 *
 * 测试项目 / Test items:
 * 1. 相对/绝对可执行路径执行。 Executable-path dispatch: verify relative and absolute executable paths both run the target command.
 * 2. 非可执行文件和未知命令报错。 Non-executable and unknown command handling: verify ordinary files and unknown commands emit the expected error text.
 * 3. 自动补全与非打印字符过滤。 Auto-complete and input sanitization: verify tab-complete lists matching entries and non-printable bytes are filtered from command parsing.
 *
 * 测试原理 / Test principles:
 * 1. 在真实 RamFS 树上驱动 `TaskFun()`，让路径解析、输入解析和输出报告保持集成。 Run a realistic RamFS tree through `Terminal::TaskFun()` so path resolution, input parsing and output reporting stay integrated.
 * 2. 以最终终端 transcript 为准，而不是只看局部 helper 状态。 Inspect the final terminal transcript rather than helper internals, because this test covers the end-to-end shell behavior contract.
 */
#include <cstring>
#include <vector>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "test.hpp"

void test_terminal()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXR::RamFS ramfs;
  LibXR::Pipe input(64);
  LibXR::Pipe output(256);
  LibXR::Terminal<> terminal(ramfs, nullptr, &input.GetReadPort(),
                             &output.GetWritePort());

  auto dir_1 = LibXR::RamFS::CreateDir("dir1");
  auto dir_2 = LibXR::RamFS::CreateDir("dir2");
  auto dir_3 = LibXR::RamFS::CreateDir("dir3");
  int command_count = 0;
  int data_value = 0;
  auto command = LibXR::RamFS::CreateCommand<int*>(
      "run",
      [](int* count, int argc, char** argv)
      {
        ASSERT(argc == 1);
        ASSERT(std::strcmp(argv[0], "dir1/dir2/dir3/run") == 0 ||
               std::strcmp(argv[0], "/dir1/dir2/dir3/run") == 0);
        (*count)++;
        return 0;
      },
      &command_count);
  auto data_file = LibXR::RamFS::CreateFile("data", data_value);
  auto alpha_file = LibXR::RamFS::CreateFile("alpha", data_value);
  auto alphabet_file = LibXR::RamFS::CreateFile("alphabet", data_value);

  ramfs.Add(dir_1);
  ramfs.Add(alpha_file);
  ramfs.Add(alphabet_file);
  dir_1.Add(dir_2);
  dir_2.Add(dir_3);
  dir_3.Add(command);
  dir_3.Add(data_file);

  char absolute_path[] = "/dir1/dir2/dir3";
  ASSERT(terminal.Path2Dir(absolute_path) == &dir_3);
  ASSERT(std::strcmp(absolute_path, "/dir1/dir2/dir3") == 0);

  char root_path[] = "/";
  ASSERT(terminal.Path2Dir(root_path) == &ramfs.root_);
  ASSERT(std::strcmp(root_path, "/") == 0);

  char relative_path[] = "dir1/dir2/dir3";
  ASSERT(terminal.Path2Dir(relative_path) == &dir_3);
  ASSERT(std::strcmp(relative_path, "dir1/dir2/dir3") == 0);

  auto run_terminal_until_idle = [&]()
  {
    for (size_t i = 0; i < 4; i++)
    {
      LibXR::Terminal<>::TaskFun(&terminal);
      if (input.GetReadPort().Size() == 0 &&
          terminal.read_status_ == LibXR::ReadOperation::OperationPollingStatus::RUNNING)
      {
        return;
      }
    }
    ASSERT(false);
  };

  auto write_raw = [&](const void* data, size_t size)
  {
    LibXR::WriteOperation write_op;
    ASSERT(input.GetWritePort()(LibXR::ConstRawData{data, size}, write_op) ==
           LibXR::ErrorCode::OK);
    run_terminal_until_idle();
  };

  auto write_line = [&](const char* command_line)
  {
    write_raw(command_line, std::strlen(command_line));
  };

  write_line("dir1/dir2/dir3/run\n");
  ASSERT(command_count == 1);
  write_line("/dir1/dir2/dir3/run\n");
  ASSERT(command_count == 2);
  write_line("/dir1/dir2/dir3/data\n");
  ASSERT(command_count == 2);
  write_line("unknown\n");
  write_line("alph\t\n");
  const unsigned char non_printable_unknown[] = {0xFF, 'u', 'n', 'k', 'n',
                                                 'o', 'w', 'n', '\n'};
  write_raw(non_printable_unknown, sizeof(non_printable_unknown));

  const size_t output_size = output.GetReadPort().Size();
  ASSERT(output_size > 0);

  std::vector<char> terminal_output(output_size + 1, '\0');
  LibXR::ReadOperation read_op;
  ASSERT(output.GetReadPort()(LibXR::RawData{terminal_output.data(), output_size},
                              read_op) == LibXR::ErrorCode::OK);
  ASSERT(std::strstr(terminal_output.data(), "Not an executable file.") != nullptr);
  ASSERT(std::strstr(terminal_output.data(), "Command not found.") != nullptr);
  ASSERT(std::memchr(terminal_output.data(), static_cast<unsigned char>(0xFF),
                     output_size) == nullptr);
  ASSERT(std::strstr(terminal_output.data(), "alpha") != nullptr);
  ASSERT(std::strstr(terminal_output.data(), "alphabet") != nullptr);

  size_t command_not_found_count = 0;
  const char* search = terminal_output.data();
  while ((search = std::strstr(search, "Command not found.")) != nullptr)
  {
    command_not_found_count++;
    search += std::strlen("Command not found.");
  }
  ASSERT(command_not_found_count == 2);
}
