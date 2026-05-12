#include "cycle_value.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_cycle_value()
{
  using LibXR::CycleValue;
  CycleValue<> val(4 * LibXR::PI + LibXR::PI / 2);
  ASSERT(equal(static_cast<double>(val), LibXR::PI / 2));

  val += LibXR::PI;
  ASSERT(equal(static_cast<double>(val), 3 * LibXR::PI / 2));

  ASSERT(equal(static_cast<double>(CycleValue<>(val - 0.0)), 3 * LibXR::PI / 2));

  val -= LibXR::PI;
  ASSERT(equal(static_cast<double>(val), LibXR::PI / 2));

  auto neg = -val;
  ASSERT(equal(static_cast<double>(neg), LibXR::TWO_PI - LibXR::PI / 2));
  UNUSED(neg);
}
