#include "pid.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_pid() {
  LibXR::PID<>::Param param;
  param.k = 1.0;
  param.p = 2.0;
  param.i = 0.5;
  param.d = 0.1;
  param.i_limit = 1.0;
  param.out_limit = 5.0;

  LibXR::PID<> pid(param);
  double out1 = pid.Calculate(1.0, 0.0, 0.1);
  ASSERT(equal(out1, 2.05));

  double out2 = pid.Calculate(1.0, 0.0, 0.1);
  ASSERT(equal(out2, 2.1));

  for (int i = 0; i < 50; ++i) {
    pid.Calculate(1.0, 0.0, 0.1);
  }
  ASSERT(std::abs(pid.GetIntegralError()) <= param.i_limit + 1e-6);
  ASSERT(std::abs(pid.LastOutput()) <= param.out_limit + 1e-6);
}
