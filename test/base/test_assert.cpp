/**
 * @file test_assert.cpp
 * @brief `LibXR::Assert` fatal 回调表面测试。 `LibXR::Assert` fatal-callback surface tests.
 *
 * 测试项目 / Test items:
 * 1. Fatal 回调的注册与替换。 Fatal callback registration and replacement: verify the global fatal callback handle can be installed and restored cleanly.
 * 2. Fatal 回调分发参数透传。 Fatal callback dispatch argument propagation: verify `Run()` forwards the ISR flag, file name and line number to the bound callback.
 *
 * 测试原理 / Test principles:
 * 1. 只通过公开的 `LibXR::Assert` 回调接口操作，避免把私有存储细节当成契约。 Operate only through the public `LibXR::Assert` callback API, so the test documents the stable contract instead of private storage details.
 * 2. 通过真实一次分发后的副作用来确认注册路径和参数透传路径都正确。 Observe the callback side effects after a real dispatch to confirm both registration and parameter forwarding paths.
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

/**
 * @brief 测试入口函数 `test_assert`。 Test entry function `test_assert`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_assert()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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

  LibXR::Assert::RunFatalErrorCallback(false, "test_assert.cpp", 42);
  ASSERT(probe.hit_count == 1);
  ASSERT(!probe.in_isr);
  ASSERT(probe.file == "test_assert.cpp");
  ASSERT(probe.line == 42);

  LibXR::Assert::RegisterFatalErrorCallback(old_callback);
}
