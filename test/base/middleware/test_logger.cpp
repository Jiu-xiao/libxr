#include <cstddef>
#include <string>
#include <string_view>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

static_assert(LibXR::Detail::LoggerLiteral::SelectFrontend<
                  LibXR::Detail::LoggerLiteral::Frontend::Auto, "brace {}", int>() ==
              LibXR::Detail::LoggerLiteral::Frontend::Format);
static_assert(LibXR::Detail::LoggerLiteral::SelectFrontend<
                  LibXR::Detail::LoggerLiteral::Frontend::Auto, "printf %d", int>() ==
              LibXR::Detail::LoggerLiteral::Frontend::Printf);
static_assert(LibXR::Detail::LoggerLiteral::SelectFrontend<
                  LibXR::Detail::LoggerLiteral::Frontend::Printf, "forced %d", int>() ==
              LibXR::Detail::LoggerLiteral::Frontend::Printf);

namespace
{

size_t CountSubstring(std::string_view text, std::string_view needle)
{
  size_t count = 0;
  size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string_view::npos)
  {
    ++count;
    pos += needle.size();
  }
  return count;
}

std::string ReadPipeText(LibXR::Pipe& pipe)
{
  const size_t output_size = pipe.GetReadPort().Size();
  ASSERT(output_size > 0);

  std::string text(output_size, '\0');
  LibXR::ReadOperation read_op;
  ASSERT(pipe.GetReadPort()(LibXR::RawData{text.data(), output_size}, read_op) ==
         LibXR::ErrorCode::OK);
  return text;
}

}  // namespace

void test_logger()
{
  LibXR::Pipe output(2048);
  auto* old_write = LibXR::STDIO::write_;
  auto* old_stream = LibXR::STDIO::write_stream_;

  LibXR::STDIO::write_ = &output.GetWritePort();
  LibXR::STDIO::write_stream_ = nullptr;

  LibXR::Logger::Publish<"brace {}">(LibXR::LogLevel::XR_LOG_LEVEL_ERROR,
                                     "logger_test.cpp", 123, 7);
  LibXR::Logger::Publish<XR_PRINTF("printf %d")>(
      LibXR::LogLevel::XR_LOG_LEVEL_ERROR, "logger_test.cpp", 124, 9);
  LibXR::Logger::Publish<"plain literal">(LibXR::LogLevel::XR_LOG_LEVEL_ERROR,
                                          "logger_test.cpp", 125);

  const auto text = ReadPipeText(output);

  LibXR::STDIO::write_ = old_write;
  LibXR::STDIO::write_stream_ = old_stream;

  ASSERT(text.find(LibXR::LIBXR_FOREGROUND_STR[static_cast<size_t>(
                        LibXR::Foreground::RED)]) != std::string::npos);
  ASSERT(text.find("(logger_test.cpp:123) brace 7") != std::string::npos);
  ASSERT(text.find("(logger_test.cpp:124) printf 9") != std::string::npos);
  ASSERT(text.find("(logger_test.cpp:125) plain literal") != std::string::npos);
  ASSERT(text.find(LibXR::LIBXR_TERMINAL_CONTROL_STR[static_cast<size_t>(
                        LibXR::TerminalControl::RESET)]) != std::string::npos);
  ASSERT(CountSubstring(text, "\r\n") == 3);
}
