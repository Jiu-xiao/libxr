/**
 * @file test_pid_response.cpp
 * @brief PID 响应与防饱和子测试。 Split test unit for PID response and anti-windup behavior.
 */
#include "pid_test_common.hpp"

/**
 * @brief 测试项函数 `RunPidResponseTests`。 Test-item function `RunPidResponseTests`.
 * @details 测试内容：执行当前分组里的 PID 子场景。 Execute the grouped PID sub-scenarios for this split file.
 *          测试原理：把响应、防积分和状态语义拆开，避免一个测试文件继续承担过多控制器语义。 Split response, anti-windup, and state semantics so one test file does not keep carrying too many controller behaviors.
 */
void RunPidResponseTests()
{
  // 1) Basic step response
  // -----------------------
  {
    LibXR::PID<>::Param param;
    param.k = 1.0;
    param.p = 2.0;
    param.i = 0.5;
    param.d = 0.1;
    param.i_limit = 1.0;
    param.out_limit = 5.0;
    param.cycle = false;

    LibXR::PID<> pid(param);

    const double OUT1 = pid.Calculate(1.0, 0.0, 0.1);
    ASSERT(equal(OUT1, 2.05));

    const double OUT2 = pid.Calculate(1.0, 0.0, 0.1);
    ASSERT(equal(OUT2, 2.1));

    for (int i = 0; i < 50; ++i)
    {
      (void)pid.Calculate(1.0, 0.0, 0.1);
    }
    ASSERT(std::abs(pid.GetIntegralError()) <= param.i_limit + 1e-6);
    ASSERT(std::abs(pid.LastOutput()) <= param.out_limit + 1e-6);
  }

  // -----------------------
  // 2) Output clamp behavior
  // -----------------------
  {
    LibXR::PID<>::Param param;
    param.k = 1.0;
    param.p = 100.0;
    param.i = 0.0;
    param.d = 0.0;
    param.i_limit = 0.0;
    param.out_limit = 1.0;

    LibXR::PID<> pid(param);

    const double OUT = pid.Calculate(1.0, 0.0, 0.1);
    ASSERT(std::abs(OUT) <= param.out_limit + 1e-6);
    ASSERT(near(OUT, 1.0, 1e-6));
    ASSERT(near(pid.LastOutput(), 1.0, 1e-6));
  }

  // ---------------------------------------------------
  // 3) Anti-windup gating: saturation prevents windup
  // ---------------------------------------------------
  {
    LibXR::PID<>::Param param;
    param.k = 1.0;
    param.p = 100.0;  // PD alone saturates immediately
    param.i = 1.0;    // try to wind up
    param.d = 0.0;
    param.i_limit = 10.0;
    param.out_limit = 1.0;

    LibXR::PID<> pid(param);

    for (int i = 0; i < 50; ++i)
    {
      (void)pid.Calculate(1.0, 0.0, 0.1);
    }

    // When already saturated, integral update is rejected if it increases saturation
    // magnitude.
    ASSERT(std::abs(pid.GetIntegralError()) <= 1e-6);
    ASSERT(std::abs(pid.LastOutput()) <= param.out_limit + 1e-6);
  }

  // ---------------------------------------------------
  // 4) Anti-windup unwind: allow integral to reduce sat
  // ---------------------------------------------------
  {
    LibXR::PID<>::Param param;
    param.k = 1.0;
    param.p = 0.0;  // isolate I behavior
    param.i = 1.0;
    param.d = 0.0;
    param.i_limit = 10.0;
    param.out_limit = 0.5;

    LibXR::PID<> pid(param);
    pid.SetIntegralError(1.0);  // start saturated from I term

    const double OUT0 = pid.Calculate(0.0, 0.0, 0.1);
    ASSERT(std::abs(OUT0) <= param.out_limit + 1e-6);

    // Apply negative error to drive integral down (unwind).
    for (int i = 0; i < 10; ++i)
    {
      (void)pid.Calculate(-1.0, 0.0, 0.1);
    }

    ASSERT(pid.GetIntegralError() < 1.0);
    ASSERT(std::abs(pid.LastOutput()) <= param.out_limit + 1e-6);
  }

  // -----------------------------------------
  // 5) External derivative overload semantics
  // -----------------------------------------
  {
    LibXR::PID<>::Param param;
    param.k = 1.0;
    param.p = 2.0;
    param.i = 0.5;
    param.d = 0.1;
    param.i_limit = 1.0;
    param.out_limit = 5.0;

    LibXR::PID<> pid(param);

    // err=1 => e_k=1
    // I: i_ = 0 + 1*0.1 = 0.1 => i_out = 0.1*0.5 = 0.05
    // D: fb_dot=10 => fb_d_k=10 => d_out = 10*0.1 = 1.0
    // out = p*e_k + i_out - d_out = 2 + 0.05 - 1.0 = 1.05
    const double OUT = pid.Calculate(1.0, 0.0, 10.0, 0.1);
    ASSERT(near(OUT, 1.05, 1e-6));
  }

  // -------------------------------------------------
  //
}
