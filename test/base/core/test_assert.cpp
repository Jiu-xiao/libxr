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
