#include <cstring>
#include <vector>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_pipe.hpp"
#include "test.hpp"

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
        ASSERT(argc == 1);
        ASSERT(std::strcmp(argv[0], "dir1/dir2/dir3/run") == 0 ||
               std::strcmp(argv[0], "/dir1/dir2/dir3/run") == 0);
        (*count)++;
        return 0;
      },
      &command_count);
  auto data_file = LibXR::RamFS::CreateFile("data", data_value);

  ramfs.Add(dir_1);
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

  auto write_line = [&](const char* command_line)
  {
    LibXR::WriteOperation write_op;
    ASSERT(input.GetWritePort()(LibXR::ConstRawData{command_line}, write_op) ==
           LibXR::ErrorCode::OK);
    run_terminal_until_idle();
  };

  write_line("dir1/dir2/dir3/run\n");
  ASSERT(command_count == 1);
  write_line("/dir1/dir2/dir3/run\n");
  ASSERT(command_count == 2);
  write_line("/dir1/dir2/dir3/data\n");
  ASSERT(command_count == 2);
  write_line("unknown\n");

  const size_t output_size = output.GetReadPort().Size();
  ASSERT(output_size > 0);

  std::vector<char> terminal_output(output_size + 1, '\0');
  LibXR::ReadOperation read_op;
  ASSERT(output.GetReadPort()(LibXR::RawData{terminal_output.data(), output_size},
                              read_op) == LibXR::ErrorCode::OK);
  ASSERT(std::strstr(terminal_output.data(), "Not an executable file.") != nullptr);
  ASSERT(std::strstr(terminal_output.data(), "Command not found.") != nullptr);
}
