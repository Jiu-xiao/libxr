/**
 * @file test_assert.cpp
 * @brief 检查致命错误回调的注册、调用次数、位置和 ISR 标记。 /
 * Tests fatal callback registration, call count, source location and ISR flag.
 */

#include "test_assert.hpp"

#include <cstdint>
#include <string_view>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{

struct FatalProbe
{
  int hit_count = 0;
  bool in_isr = true;
  std::string_view file;
  uint32_t line = 0;
};

}  // namespace

void test_assert()
{
  // 临时替换 fatal 回调，测试后恢复，避免后续测试继续引用栈上的 probe。
  // Temporarily replace the fatal callback and restore it before the stack probe expires.
  auto old_callback = LibXR::Assert::FatalErrorCallback();

  FatalProbe probe;
  auto callback = LibXR::Assert::FatalCallback::Create(
      [](bool in_isr, FatalProbe* probe, const char* file, uint32_t line)
      {
        probe->hit_count++;
        probe->in_isr = in_isr;
        probe->file = file;
        probe->line = line;
      },
      &probe);

  LibXR::Assert::RegisterFatalErrorCallback(callback);
  TEST_ASSERT(!LibXR::Assert::FatalErrorCallback().Empty());

  // 通过公开入口调用，检查只分发一次，并原样传递位置和 ISR 标记。
  // Use the public entry; check one dispatch with the supplied location and ISR flag.
  LibXR::Assert::RunFatalErrorCallback(false, "test_assert.cpp", 42);

  TEST_ASSERT(probe.hit_count == 1);
  TEST_ASSERT(!probe.in_isr);
  TEST_ASSERT(probe.file == "test_assert.cpp");
  TEST_ASSERT(probe.line == 42);

  LibXR::Assert::RegisterFatalErrorCallback(old_callback);
}
