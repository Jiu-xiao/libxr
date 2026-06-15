/**
 * @file test_pid_cycle.cpp
 * @brief PID 周期误差语义子测试。 Split test unit for PID cyclic-error semantics.
 */
#include "pid_test_common.hpp"

/**
 * @brief 测试项函数 `RunPidCycleTests`。 Test-item function `RunPidCycleTests`.
 * @details 测试内容：执行当前分组里的 PID 子场景。 Execute the grouped PID sub-scenarios for this split file.
 *          测试原理：把响应、防积分和状态语义拆开，避免一个测试文件继续承担过多控制器语义。 Split response, anti-windup, and state semantics so one test file does not keep carrying too many controller behaviors.
 */
void RunPidCycleTests()
{
  // 12) Cycle mode: error uses CycleValue(sp) - fb
  //     and actually wraps for an obviously out-of-range sp
  // -------------------------------------------------------
  {
    LibXR::PID<>::Param param;
    using S = decltype(param.k);

    param.k = static_cast<S>(1.0);
    param.p = static_cast<S>(1.0);  // output == err (since k=1, p=1, i=d=0)
    param.i = static_cast<S>(0.0);
    param.d = static_cast<S>(0.0);
    param.i_limit = static_cast<S>(0.0);
    param.out_limit = static_cast<S>(0.0);

    const S SP =
        static_cast<S>(1000.0);  // 明显超出常见周期范围，应该触发 CycleValue wrap
    const S FB = static_cast<S>(0.2);
    const S DT = static_cast<S>(0.1);

    // cycle = false: err = sp - fb
    {
      param.cycle = false;
      LibXR::PID<> pid(param);

      const double OUT = pid.Calculate(SP, FB, DT);
      const double EXPECT = static_cast<double>(SP - FB);

      ASSERT(near(OUT, EXPECT, 1e-6));
      ASSERT(near(pid.LastError(), EXPECT, 1e-6));
    }

    // cycle = true: err = CycleValue(sp) - fb
    {
      param.cycle = true;
      LibXR::PID<> pid(param);

      const double EXPECT_CYCLE = static_cast<double>(LibXR::CycleValue<S>(SP) - FB);
      const double EXPECT_LINEAR = static_cast<double>(SP - FB);

      ASSERT(std::isfinite(EXPECT_CYCLE));
      ASSERT(std::abs(EXPECT_CYCLE - EXPECT_LINEAR) > 1.0);

      const double OUT = pid.Calculate(SP, FB, DT);
      ASSERT(near(OUT, EXPECT_CYCLE, 1e-6));
      ASSERT(near(pid.LastError(), EXPECT_CYCLE, 1e-6));
    }
  }

}
