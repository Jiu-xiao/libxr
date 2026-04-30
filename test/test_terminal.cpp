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

  static constexpr char COMMAND[] = "unknown\n";
  LibXR::WriteOperation write_op;
  ASSERT(input.GetWritePort()(LibXR::ConstRawData{COMMAND, sizeof(COMMAND) - 1},
                              write_op) == LibXR::ErrorCode::OK);

  LibXR::Terminal<>::TaskFun(&terminal);

  const size_t output_size = output.GetReadPort().Size();
  ASSERT(output_size > 0);

  std::vector<char> terminal_output(output_size + 1, '\0');
  LibXR::ReadOperation read_op;
  ASSERT(output.GetReadPort()(LibXR::RawData{terminal_output.data(), output_size},
                              read_op) == LibXR::ErrorCode::OK);
  ASSERT(std::strstr(terminal_output.data(), "Command not found.") != nullptr);
}
