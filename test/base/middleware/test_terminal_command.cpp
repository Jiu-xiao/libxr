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
  TestCdBuiltins();
  TestLsBuiltin();
}
