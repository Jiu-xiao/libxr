/**
 * @file test_pid_state.cpp
 * @brief PID 状态访问器与重置子测试。 Split test unit for PID state-accessor and reset behavior.
 */
#include "pid_test_common.hpp"

/**
 * @brief 测试项函数 `RunPidStateTests`。 Test-item function `RunPidStateTests`.
 * @details 测试内容：执行当前分组里的 PID 子场景。 Execute the grouped PID sub-scenarios for this split file.
 *          测试原理：把响应、防积分和状态语义拆开，避免一个测试文件继续承担过多控制器语义。 Split response, anti-windup, and state semantics so one test file does not keep carrying too many controller behaviors.
 */
void RunPidStateTests()
{
  // 6) LastFeedback() returns raw (unscaled) feedback
  // -------------------------------------------------
  {
    LibXR::PID<>::Param param;
    param.k = 3.0;
    param.p = 0.0;
    param.i = 0.0;
    param.d = 0.0;
    param.i_limit = 0.0;
    param.out_limit = 0.0;

    LibXR::PID<> pid(param);

    (void)pid.Calculate(0.0, 0.3, 0.1);
    // eps 放宽：兼容默认 Scalar 为 float 的实现
    ASSERT(near(pid.LastFeedback(), 0.3, 1e-6));
  }

  // ---------------------------------------------------------
  // 7) LastDerivative() meaning: stores k * d(fb)/dt (diff mode)
  // ---------------------------------------------------------
  {
    LibXR::PID<>::Param param;
    param.k = 2.0;
    param.p = 0.0;
    param.i = 0.0;
    param.d = 0.0;
    param.i_limit = 0.0;
    param.out_limit = 0.0;

    LibXR::PID<> pid(param);

    (void)pid.Calculate(0.0, 0.0, 0.1);  // establishes last_fb = 0
    (void)pid.Calculate(0.0, 1.0, 0.1);  // fb_dot = 10, scaled by k => 20

    ASSERT(near(pid.LastDerivative(), 20.0, 1e-6));
  }

  // -------------------------------------------------
  // 8) Invalid inputs: dt<=0 / NaN return last output
  // -------------------------------------------------
  {
    LibXR::PID<>::Param param;
    param.k = 1.0;
    param.p = 2.0;
    param.i = 0.5;
    param.d = 0.1;
    param.i_limit = 1.0;
    param.out_limit = 5.0;

    LibXR::PID<> pid(param);

    const double OUT1 = pid.Calculate(1.0, 0.0, 0.1);
    ASSERT(std::isfinite(OUT1));

    const double OUT_DT0 = pid.Calculate(1.0, 0.0, 0.0);
    ASSERT(near(OUT_DT0, OUT1, 1e-12));

    const double STD_NAN = std::numeric_limits<double>::quiet_NaN();
    const double OUT_NAN = pid.Calculate(STD_NAN, 0.0, 0.1);
    ASSERT(near(OUT_NAN, OUT1, 1e-12));
  }

  // -----------------------
  // 9) Reset clears states
  // -----------------------
  {
    LibXR::PID<>::Param param;
    param.k = 1.0;
    param.p = 2.0;
    param.i = 0.5;
    param.d = 0.1;
    param.i_limit = 1.0;
    param.out_limit = 5.0;

    LibXR::PID<> pid(param);

    (void)pid.Calculate(1.0, 0.0, 0.1);
    pid.Reset();

    ASSERT(near(pid.GetIntegralError(), 0.0, 1e-12));
    ASSERT(near(pid.LastError(), 0.0, 1e-12));
    ASSERT(near(pid.LastFeedback(), 0.0, 1e-12));
    ASSERT(near(pid.LastOutput(), 0.0, 1e-12));
    ASSERT(near(pid.LastDerivative(), 0.0, 1e-12));
  }

  // -------------------------------------------------------
  // 10) Semantics: i_limit==0 disables I and forces i_ = 0
  // -------------------------------------------------------
  {
    LibXR::PID<>::Param param;
    param.k = 1.0;
    param.p = 0.0;  // isolate I
    param.i = 1.0;  // I enabled by gain, but should be disabled by i_limit==0
    param.d = 0.0;
    param.i_limit = 0.0;  // disable I (force i_=0)
    param.out_limit = 0.0;

    LibXR::PID<> pid(param);

    pid.SetIntegralError(1.0);  // even if preset, Calculate() should clear it
    (void)pid.Calculate(1.0, 0.0, 0.1);

    ASSERT(near(pid.GetIntegralError(), 0.0, 1e-12));
    ASSERT(near(pid.LastOutput(), 0.0, 1e-12));
  }

  // -------------------------------------------------------
  // 11) Behavior: out_limit==0 disables output clamping
  // -------------------------------------------------------
  {
    LibXR::PID<>::Param param;
    param.k = 1.0;
    param.p = 100.0;  // would clamp if out_limit active
    param.i = 0.0;
    param.d = 0.0;
    param.i_limit = 0.0;
    param.out_limit = 0.0;  // disable clamp

    LibXR::PID<> pid(param);

    const double OUT = pid.Calculate(1.0, 0.0, 0.1);
    ASSERT(near(OUT, 100.0, 1e-6));
    ASSERT(near(pid.LastOutput(), 100.0, 1e-6));
  }

  // -------------------------------------------------------
  //
}
