/**
 * @file main.cpp
 * @brief base/runtime 测试主执行器聚合入口。 Aggregation entry for the base/runtime main test runner.
 * @details 职责：
 *          1. 安装把断言失败转换成进程失败的 fatal 测试回调。
 *          2. 调用拆分后的测试分组注册表。
 *          Responsibilities:
 *          1. Register the fatal test callback that converts assertion failures into process failure.
 *          2. Invoke the split test-group registry.
 */
#include "test_main_sets.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace
{
#if defined(LIBXR_SYSTEM_none)
/**
 * @brief none 后端测试用时间基。 Test timebase used by the `none` backend.
 * @details 测试内容：给 host 上的 `LIBXR_SYSTEM=none` 测试提供单调时间源。 Provide a monotonic clock source for host-side `LIBXR_SYSTEM=none` tests.
 *          测试原理：裸机后端本身不创建默认 timebase，测试入口必须显式安装一个最小时间基，否则 sleep/timer/logger 相关路径无法被验证。 The bare-metal backend does not install a default timebase, so the test runner installs the minimal source required to verify sleep, timer, and timestamp paths.
 */
class NoneTestTimebase : public LibXR::Timebase
{
 public:
  /**
   * @brief 构造 none 测试时间基。 Construct the none test timebase.
   * @details 测试内容：记录测试进程启动后的单调起点。 Record the monotonic start point for the test process.
   *          测试原理：后续时间戳都以该起点为零点，避免依赖系统墙上时间。 Use this point as zero so timestamps do not depend on wall-clock time.
   */
  NoneTestTimebase() : start_(Clock::now()) {}

  /**
   * @brief 获取当前微秒时间。 Get the current microsecond timestamp.
   * @return 从测试时间基启动起计算的微秒数。 Microseconds elapsed since this test timebase started.
   */
  LibXR::MicrosecondTimestamp _get_microseconds() override
  {
    return LibXR::MicrosecondTimestamp(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start_)
            .count());
  }

  /**
   * @brief 获取当前毫秒时间。 Get the current millisecond timestamp.
   * @return 从测试时间基启动起计算的毫秒数。 Milliseconds elapsed since this test timebase started.
   */
  LibXR::MillisecondTimestamp _get_milliseconds() override
  {
    return LibXR::MillisecondTimestamp(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_)
            .count());
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point start_;
};

/**
 * @brief 安装 none 后端测试时间基。 Install the test timebase for the none backend.
 * @details 测试内容：确保 `LibXR::Timebase` 全局指针指向测试时间源。 Ensure the global `LibXR::Timebase` pointer references the test clock.
 *          测试原理：使用函数内静态对象保证时间基生命周期覆盖整个测试进程。 Use a function-local static object so the timebase lives for the whole test process.
 */
void InstallNoneTestTimebase()
{
  static NoneTestTimebase timebase;
  UNUSED(timebase);
}
#endif
}  // namespace

/**
 * @brief 辅助函数 `main`。 Helper function `main`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
bool equal(double a, double b) { return std::abs(a - b) < 1e-6; }

int main()
{
  LibXR::PlatformInit();
#if defined(LIBXR_SYSTEM_none)
  InstallNoneTestTimebase();
#endif

  auto err_cb = LibXR::Assert::FatalCallback::Create(
      [](bool in_isr, void* arg, const char* file, uint32_t line)
      {
        UNUSED(in_isr);
        UNUSED(arg);
        UNUSED(file);
        UNUSED(line);

        std::fprintf(stderr, "Error: Union test failed at step [%s].\r\n",
                     test_name);
        std::fflush(stderr);
        // NOLINTNEXTLINE
        *(volatile long long*)(nullptr) = 0;
        exit(-1);
      },
      reinterpret_cast<void*>(0));

  LibXR::Assert::RegisterFatalErrorCallback(err_cb);

  const int status = RunMainTestBinary();
  exit(status);
  return status;
}
