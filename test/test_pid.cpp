#include <cmath>
#include <limits>

#include "libxr_def.hpp"
#include "pid.hpp"
#include "test.hpp"

static inline bool near(double a, double b, double eps = 1e-6)
{
  return std::abs(a - b) <= eps;
}

void test_pid()
{
  // -----------------------
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
