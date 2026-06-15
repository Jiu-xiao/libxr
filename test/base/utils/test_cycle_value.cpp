/**
 * @file test_cycle_value.cpp
 * @brief 循环角值归一化与算术测试。 Cyclic-angle normalization arithmetic tests.
 *
 * 测试项目 / Test items:
 * 1. 构造时超周期归一化。 Constructor normalization: verify values outside one full period are wrapped into the canonical range.
 * 2. 加减、取反与差值重建后的循环语义。 Arithmetic updates: verify `+=`, `-=`, unary negation and subtraction-based reconstruction preserve cyclic semantics.
 *
 * 测试原理 / Test principles:
 * 1. 选用 `PI` 的整倍数组合，让期望 wrap 结果容易推导和比较。 Use multiples of `PI` so the expected wrapped values stay easy to reason about and compare numerically.
 */
#include "cycle_value.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

/**
 * @brief 测试入口函数 `test_cycle_value`。 Test entry function `test_cycle_value`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_cycle_value()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
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
