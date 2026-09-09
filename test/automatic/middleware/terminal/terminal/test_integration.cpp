/**
 * @file test_integration.cpp
 * @brief Terminal 命令处理集成测试 / Terminal command-processing integration tests.
 *
 * 检查路径解析、命令执行、无效命令和普通文件的报错，以及补全与不可打印字符处理。
 * Check paths, command execution, error messages, completion and handling of
 * non-printable input.
 */

#include <cstring>
#include <vector>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_terminal()
{
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
        TEST_ASSERT(argc == 1);
        TEST_ASSERT(std::strcmp(argv[0], "dir1/dir2/dir3/run") == 0 ||
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
  TEST_ASSERT(terminal.Path2Dir(absolute_path) == &dir_3);
  TEST_ASSERT(std::strcmp(absolute_path, "/dir1/dir2/dir3") == 0);

  char root_path[] = "/";
  TEST_ASSERT(terminal.Path2Dir(root_path) == &ramfs.root_);
  TEST_ASSERT(std::strcmp(root_path, "/") == 0);

  char relative_path[] = "dir1/dir2/dir3";
  TEST_ASSERT(terminal.Path2Dir(relative_path) == &dir_3);
  TEST_ASSERT(std::strcmp(relative_path, "dir1/dir2/dir3") == 0);

  // 手动推进终端任务，直到输入处理完并等待下一次输入，不启动常驻终端线程。
  // Drive the task until it has consumed the input and awaits more; no permanent terminal
  // thread is started.
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
    TEST_ASSERT(false);
  };

  auto write_raw = [&](const void* data, size_t size)
  {
    LibXR::WriteOperation write_op;
    TEST_ASSERT(input.GetWritePort()(LibXR::ConstRawData{data, size}, write_op) ==
                LibXR::ErrorCode::OK);
    run_terminal_until_idle();
  };

  auto write_line = [&](const char* command_line)
  { write_raw(command_line, std::strlen(command_line)); };

  // 相对路径和绝对路径各执行一次命令；普通文件和未知名称只输出错误。
  // Run the command once by each path form; a plain file and unknown names only report
  // errors.
  write_line("dir1/dir2/dir3/run\n");
  TEST_ASSERT(command_count == 1);
  write_line("/dir1/dir2/dir3/run\n");
  TEST_ASSERT(command_count == 2);
  write_line("/dir1/dir2/dir3/data\n");
  TEST_ASSERT(command_count == 2);
  write_line("unknown\n");
  // 两个文件共享 alph 前缀，Tab 应列出候选而不是任意选中其中一个。
  // Two files share the alph prefix; Tab must list candidates rather than choose one
  // arbitrarily.
  write_line("alph\t\n");
  const unsigned char non_printable_unknown[] = {0xFF, 'u', 'n', 'k', 'n',
                                                 'o',  'w', 'n', '\n'};
  write_raw(non_printable_unknown, sizeof(non_printable_unknown));

  const size_t output_size = output.GetReadPort().Size();
  TEST_ASSERT(output_size > 0);

  std::vector<char> terminal_output(output_size + 1, '\0');
  LibXR::ReadOperation read_op;
  TEST_ASSERT(output.GetReadPort()(LibXR::RawData{terminal_output.data(), output_size},
                                   read_op) == LibXR::ErrorCode::OK);
  TEST_ASSERT(std::strstr(terminal_output.data(), "Not an executable file.") != nullptr);
  TEST_ASSERT(std::strstr(terminal_output.data(), "Command not found.") != nullptr);
  TEST_ASSERT(std::memchr(terminal_output.data(), static_cast<unsigned char>(0xFF),
                          output_size) == nullptr);
  TEST_ASSERT(std::strstr(terminal_output.data(), "alpha") != nullptr);
  TEST_ASSERT(std::strstr(terminal_output.data(), "alphabet") != nullptr);

  size_t command_not_found_count = 0;
  const char* search = terminal_output.data();
  while ((search = std::strstr(search, "Command not found.")) != nullptr)
  {
    command_not_found_count++;
    search += std::strlen("Command not found.");
  }
  TEST_ASSERT(command_not_found_count == 2);
}
