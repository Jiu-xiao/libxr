/**
 * @file test_timebase.cpp
 * @brief runtime timebase 递进与 wrap-range 配置测试。 Runtime timebase progression and wrap-range configuration tests.
 *
 * 测试项目 / Test items:
 * 1. live 时钟递进。 Live progression: verify runtime microsecond and millisecond clocks advance by roughly the expected amount across a sleep interval.
 * 2. 自定义 wrap range 下的时间差语义。 Configured wrap semantics: verify custom wrap ranges are applied to timestamp subtraction on both millisecond and microsecond scales.
 *
 * 测试原理 / Test principles:
 * 1. 把 live progression 和显式 wrap 覆盖放在同一文件，兼顾后端取时和静态算术策略。 Pair live time progression with explicit wrap-range overrides so the test covers both backend clock sourcing and static arithmetic policy.
 */
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

namespace
{
struct TimebaseWrapProbe : LibXR::Timebase
{
  /**
   * @brief 辅助函数 `Set`。 Helper function `Set`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  static void Set(uint64_t max_valid_us, uint32_t max_valid_ms)
  {
    ConfigureWrapRange(max_valid_us, max_valid_ms);
  }

  static uint64_t GetUs() { return GetConfiguredWrapRangeUs(); }
  static uint32_t GetMs() { return GetConfiguredWrapRangeMs(); }
};
}  // namespace

/**
 * @brief 测试入口函数 `test_timebase`。 Test entry function `test_timebase`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_timebase()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  LibXR::MillisecondTimestamp start_ms(1000), end_ms(2005);
  LibXR::MicrosecondTimestamp start_us(1000), end_us(2005);
  const uint64_t old_max_valid_us = TimebaseWrapProbe::GetUs();
  const uint32_t old_max_valid_ms = TimebaseWrapProbe::GetMs();

  start_ms = LibXR::Timebase::GetMilliseconds();
  start_us = LibXR::Timebase::GetMicroseconds();
  LibXR::Thread::Sleep(100);
  end_us = LibXR::Timebase::GetMicroseconds();
  end_ms = LibXR::Timebase::GetMilliseconds();

  ASSERT(std::fabs((end_ms - start_ms).ToMillisecond() - 100.0f) < 10);
  ASSERT(std::fabs((end_us - start_us).ToMicrosecond() - 100000.0f) < 10000);

  TimebaseWrapProbe::Set(old_max_valid_us, 999u);
  ASSERT((LibXR::MillisecondTimestamp(3u) - LibXR::MillisecondTimestamp(998u))
             .ToMillisecond() == 5u);

  TimebaseWrapProbe::Set(999999u, 999u);
  ASSERT((LibXR::MicrosecondTimestamp(7u) - LibXR::MicrosecondTimestamp(999995u))
             .ToMicrosecond() == 12u);

  TimebaseWrapProbe::Set(old_max_valid_us, old_max_valid_ms);
}
