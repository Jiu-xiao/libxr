/**
 * @file test_printf_frontend_float.cpp
 * @brief 默认 profile 的 `printf` 浮点族运行时输出测试。 Default-profile `printf`
 * floating-point runtime output tests.
 * @details
 * 1. 对照 host `snprintf` 的代表性浮点语义点。
 * 2. 覆盖大小写、alternate form、零填充、`%g` 阈值、负零和 inf/nan。
 * 3. 完整舍入边界不在本文件展开。
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 覆盖默认 `printf` profile 下的代表性浮点输出语义。 Cover representative float
 * output semantics under the default `printf` profile.
 */
void TestPrintfFrontendFloatSemantics()
{
  // 类型/大小写族：确认 double、long double 和大写说明符都走到对应 writer 入口。
  // Type and case families: double, long double, and uppercase specifiers reach the right
  // writers.
  if (!SameAsSnprintf<"%Lg|%Le|%Lf">(1.25L, 1.25L, 1.25L))
  {
    Fail("long double family mismatch");
  }

  if (!SameAsSnprintf<"%F|%E|%G|%LG">(1.25, 1.25, 1.25, 2.25L))
  {
    Fail("uppercase float family mismatch");
  }

  // 格式标志：alternate form 保留小数点，零填充与符号/空格标志组合。
  // Format flags: alternate form keeps the radix point; zero padding combines with
  // sign/space flags.
  if (!SameAsSnprintf<"%#.0f|%#.0e|%#.3g">(12.0, 12.0, 1.2))
  {
    Fail("float alternate form mismatch");
  }

  // Halfway cases must follow the default printf nearest-even rounding behavior.
  if (!SameAsSnprintf<"%.0f|%.0f|%.0f|%.0f">(0.5, 1.5, 2.5, 3.5))
  {
    Fail("float half-even rounding mismatch");
  }

  if (!SameAsSnprintf<"%010f|%+010f|% 010f">(1.25, 1.25, 1.25))
  {
    Fail("float zero padding mismatch");
  }

  // inf/nan are text payloads; zero padding must not be inserted after the sign.
  if (!SameAsSnprintf<"%010f|%+010f|% 010f|%010f">(
          std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()))
  {
    Fail("float inf nan zero padding mismatch");
  }

  // 边界语义：`%g` fixed/scientific 阈值、负零和 inf/nan 文本。
  // Boundary semantics: `%g` fixed/scientific threshold, negative zero, and inf/nan text.
  if (!SameAsSnprintf<"%g|%g|%.0g|%#.0g">(1000000.0, 999999.0, 12.0, 12.0))
  {
    Fail("float general threshold mismatch");
  }

  // %g chooses fixed or scientific form after rounding the requested significant digits.
  if (!SameAsSnprintf<"%.6g|%.6g|%.5g|%.4g|%.6g|%.6g">(
          999999.4, 999999.5, 99999.9, 9999.9, 0.0000999999, 0.00009999999))
  {
    Fail("float general rounded exponent mismatch");
  }

  // Rounding at the largest finite double must not overflow the internal helper.
  if (!SameAsSnprintf<"%.2e|%.2g">(std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::max()))
  {
    Fail("float max double scientific mismatch");
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

  // 全族 smoke：一个格式串同时覆盖已启用的整数、指针、文本和浮点说明符组合。
  // Full-family smoke: one source string combines enabled integer, pointer, text, and
  // float specifiers.
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
