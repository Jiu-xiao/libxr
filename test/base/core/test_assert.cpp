/**
 * @file test_assert.cpp
 * @brief `LibXR::Assert` fatal-callback surface tests.
 *
 * Test items:
 * 1. Fatal callback registration and replacement: verify the global fatal callback handle can be installed and restored cleanly.
 * 2. Fatal callback dispatch argument propagation: verify `Run()` forwards the ISR flag, file name and line number to the bound callback.
 *
 * Test principle:
 * 1. Operate only through the public `LibXR::Assert` callback API, so the test documents the stable contract instead of private storage details.
 * 2. Observe the callback side effects after a real dispatch to confirm both registration and parameter forwarding paths.
 */
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
  ASSERT(!LibXR::Assert::FatalErrorCallback().Empty());

  LibXR::Assert::FatalErrorCallback().Run(false, "test_assert.cpp", 42);
  ASSERT(probe.hit_count == 1);
  ASSERT(!probe.in_isr);
  ASSERT(probe.file == "test_assert.cpp");
  ASSERT(probe.line == 42);

  LibXR::Assert::RegisterFatalErrorCallback(old_callback);
}
