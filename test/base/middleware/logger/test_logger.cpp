/**
 * @file test_logger.cpp
 * @brief logger 前端选择与终端输出渲染测试。 Logger frontend resolution and terminal-output rendering tests.
 *
 * 测试项目 / Test items:
 * 1. brace/printf 字面量前端选择。 Frontend selection: verify brace-style and printf-style literals resolve to the expected logger frontend at compile time.
 * 2. 运行时日志发布后的颜色、前缀和消息输出。 Runtime publish path: verify published logs carry level color, file/line prefix, formatted message text and terminal reset suffix.
 *
 * 测试原理 / Test principles:
 * 1. 把 `STDIO` 绑定到 `Pipe`，直接读 logger 真正输出的字节流。 Bind `STDIO` to a `Pipe` so the test reads the logger's real output bytes instead of inspecting intermediate buffers.
 * 2. 同时验证编译期前端选择和运行时渲染文本，因为 logger 跨两层工作。 Validate both compile-time frontend choice and runtime rendered text, because logger correctness spans both layers.
 */
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
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
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
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
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
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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
