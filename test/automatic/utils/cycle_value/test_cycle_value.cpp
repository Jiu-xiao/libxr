/**
 * @file test_cycle_value.cpp
 * @brief CycleValue 周期归一化测试 / CycleValue normalization tests.
 *
 * 检查超出周期的初值，以及加、减和取负后的周期值。
 * Check an out-of-range initial value and cyclic results of addition, subtraction and
 * negation.
 */

#include "cycle_value.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"
#include "test_assert.hpp"

void test_cycle_value()
{
  using LibXR::CycleValue;
  CycleValue<> val(4 * LibXR::PI + LibXR::PI / 2);
  TEST_ASSERT(equal(static_cast<double>(val), LibXR::PI / 2));

  val += LibXR::PI;
  TEST_ASSERT(equal(static_cast<double>(val), 3 * LibXR::PI / 2));

  TEST_ASSERT(equal(static_cast<double>(CycleValue<>(val - 0.0)), 3 * LibXR::PI / 2));

  val -= LibXR::PI;
  TEST_ASSERT(equal(static_cast<double>(val), LibXR::PI / 2));

  auto neg = -val;
  TEST_ASSERT(equal(static_cast<double>(neg), LibXR::TWO_PI - LibXR::PI / 2));
  UNUSED(neg);
}
