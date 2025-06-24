#include "cycle_value.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_cycle_value()
{
  using LibXR::CycleValue;
  CycleValue<> val(4 * M_PI + M_PI / 2);
  ASSERT(equal(static_cast<double>(val), M_PI / 2));

  val += M_PI;
  ASSERT(equal(static_cast<double>(val), 3 * M_PI / 2));

  double diff = CycleValue<>(val - 0.0);

  ASSERT(equal(diff, 3 * M_PI / 2));

  val -= M_PI;
  ASSERT(equal(static_cast<double>(val), M_PI / 2));

  auto neg = -val;
  ASSERT(equal(static_cast<double>(neg), M_2PI - M_PI / 2));
}
