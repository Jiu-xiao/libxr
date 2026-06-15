/**
 * @file test_printf_frontend_float.cpp
 * @brief `print` printf 浮点族前端语义子测试。 Split test unit for `print` printf floating-point frontend semantics.
 * @details 测试项目：
 *          1. long double、大小写浮点和 alternate form 语义。
 *          2. padding、阈值、负零、inf/nan 与全家桶格式语义。
 *          Test items:
 *          1. `long double`, uppercase float, and alternate-form semantics.
 *          2. Padding, threshold, negative-zero, inf/nan, and full-family format semantics.
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
void TestPrintfFrontendFloatSemantics()
{
  if (!SameAsSnprintf<"%Lg|%Le|%Lf">(1.25L, 1.25L, 1.25L))
  {
    Fail("long double family mismatch");
  }

  if (!SameAsSnprintf<"%F|%E|%G|%LG">(1.25, 1.25, 1.25, 2.25L))
  {
    Fail("uppercase float family mismatch");
  }

  if (!SameAsSnprintf<"%#.0f|%#.0e|%#.3g">(12.0, 12.0, 1.2))
  {
    Fail("float alternate form mismatch");
  }

  if (!SameAsSnprintf<"%010f|%+010f|% 010f">(1.25, 1.25, 1.25))
  {
    Fail("float zero padding mismatch");
  }

  if (!SameAsSnprintf<"%g|%g|%.0g|%#.0g">(1000000.0, 999999.0, 12.0, 12.0))
  {
    Fail("float general threshold mismatch");
  }

  if (!SameAsSnprintf<"%f|%e|%g">(-0.0, -0.0, -0.0))
  {
    Fail("negative zero float mismatch");
  }

  if (!SameAsSnprintf<"%f|%F|%e">(std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::quiet_NaN()))
  {
    Fail("float inf nan mismatch");
  }

  {
    int value = 0;
    if (!SameAsSnprintf<"a%d 0123456789abcdef %u %o %x %X %p %c %s %f %e %g %Lf %Le %Lg">(
            -1, 2U, 8U, 42U, 42U, &value, 'Q', "xy", 1.5, 1.5, 1.5, 2.25L, 2.25L, 2.25L))
    {
      Fail("full supported family mismatch");
    }
  }
}
}  // namespace LibXRPrintTest
