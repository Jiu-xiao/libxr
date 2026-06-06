/**
 * @file test_printf_frontend.cpp
 * @brief `print` printf 前端语义子测试。 Split test unit for `print` printf frontend semantics.
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 测试项函数 `TestPrintfFrontendSemantics`。 Test-item function `TestPrintfFrontendSemantics`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestPrintfFrontendSemantics()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  if (!SameAsSnprintf<"abc">())
  {
    Fail("plain text mismatch");
  }

  if (!SameAsSnprintf<"0123456789abcdef">())
  {
    Fail("long plain text mismatch");
  }

  if (!SameAsSnprintf<"%d|%+d|% d|%05d|%-5d|%.0d">(-7, 7, 7, 7, 7, 0))
  {
    Fail("signed integer semantics mismatch");
  }

  if (!SameAsSnprintf<"%+ d|% +d|%05d|%-05d|%+05d|% 05d">(7, 7, 7, 7, 7, 7))
  {
    Fail("integer flag precedence mismatch");
  }

  if (!SameAsSnprintf<"%05.3d|%5.0d|%5.0x|%#.0x">(7, 0, 0U, 0U))
  {
    Fail("integer precision precedence mismatch");
  }

  if (!SameAsSnprintf<"%u|%.0u|%5.3u|%-5u">(7U, 0U, 7U, 7U))
  {
    Fail("unsigned integer semantics mismatch");
  }

  if (!SameAsSnprintf<"%#x|%#X|%#.0x|%08x|%#08x">(42U, 42U, 0U, 42U, 42U))
  {
    Fail("hex integer semantics mismatch");
  }

  if (!SameAsSnprintf<"%o|%#o|%#.0o|%#.3o">(8U, 8U, 0U, 1U))
  {
    Fail("octal integer semantics mismatch");
  }

  if (!SamePrintfAsExpected<"%b|%B|%#b|%#B|%08b">("101|101|0b101|0B101|00000101", 5U, 5U,
                                                  5U, 5U, 5U))
  {
    Fail("binary integer semantics mismatch");
  }

  {
    unsigned long long max_value = std::numeric_limits<unsigned long long>::max();
    std::string binary = UnsignedBaseText(max_value, 2);
    std::string octal = UnsignedBaseText(max_value, 8);
    std::string hex_lower = UnsignedBaseText(max_value, 16);
    std::string hex_upper = UnsignedBaseText(max_value, 16, true);
    std::string expected = binary + "|0b" + binary + "|0" + octal + "|" + hex_lower +
                           "|" + hex_upper;
    if (!SamePrintfAsExpected<"%llb|%#llb|%#llo|%llx|%llX">(
            expected, max_value, max_value, max_value, max_value, max_value))
    {
      Fail("64-bit integer base semantics mismatch");
    }
  }

  if (!SameAsSnprintf<"x=%+08d y=%-4s z=%#x %% %.2f">(-12, "ok", 42U, 1.25))
  {
    Fail("mixed format mismatch");
  }

  {
    int value = 7;
    if (!SameAsSnprintf<"%c %.3s %p">('A', "abcdef", &value))
    {
      Fail("character string pointer mismatch");
    }
  }

  if (!SameAsSnprintf<"%c|%3c|%-3c">('A', 'B', 'C'))
  {
    Fail("character semantics mismatch");
  }

  if (!SameAsSnprintf<"[%s]">("abc"))
  {
    Fail("string format mismatch");
  }

  if (!SameAsSnprintf<"%s|%.3s|%6.3s|%-6.3s">("abcdef", "abcdef", "abcdef", "abcdef"))
  {
    Fail("string semantics mismatch");
  }

  {
    long signed_value = -12;
    size_t unsigned_value = 34;
    long double float_value = 1.25L;
    if (!SameAsSnprintf<"%ld %zu %.2Lf">(signed_value, unsigned_value, float_value))
    {
      Fail("length-modifier format mismatch");
    }
  }

  {
    signed char tiny_signed = -3;
    unsigned char tiny_unsigned = 4;
    short short_signed = -5;
    unsigned short short_unsigned = 6;
    long long long_long_signed = -7;
    unsigned long long long_long_unsigned = 8;
    intmax_t max_signed = -9;
    uintmax_t max_unsigned = 10;
    ptrdiff_t ptrdiff_signed = -11;
    std::make_unsigned_t<ptrdiff_t> ptrdiff_unsigned = 12;
    if (!SameAsSnprintf<"%hhd %hhu %hd %hu %lld %llu %jd %ju %td %tu">(
            tiny_signed, tiny_unsigned, short_signed, short_unsigned, long_long_signed,
            long_long_unsigned, max_signed, max_unsigned, ptrdiff_signed,
            ptrdiff_unsigned))
    {
      Fail("integer length family mismatch");
    }
  }

  if (!SameAsSnprintf<"%Lg|%Le|%Lf">(1.25L, 1.25L, 1.25L))
  {
    Fail("long double family mismatch");
  }

  if (!SameAsSnprintf<"%2$u %1$s %2$#x">("ok", 7U))
  {
    Fail("positional argument semantics mismatch");
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

  {
    std::string text = "hello";
    std::string_view view = "xy";
    if (!SamePrintfAsExpected<"[%s][%s]">("[hello][xy]", text, view))
    {
      Fail("printf string object mismatch");
    }
  }

  {
    char bounded_text[4] = {'i', 'm', 'u', '\0'};
    const char embedded_text[5] = {'a', 'b', '\0', 'c', 'd'};
    if (!SamePrintfAsExpected<"[%s][%s]">("[imu][ab]", bounded_text, embedded_text))
    {
      Fail("printf bounded char array mismatch");
    }
  }

  {
    enum PlainHex : unsigned
    {
      PLAIN_HEX = 42U
    };
    if (!SameAsSnprintf<"%#x|%u">(PLAIN_HEX, PLAIN_HEX))
    {
      Fail("printf enum mismatch");
    }
  }

  if (!SamePrintfAsExpected<"[%s]">("[(null)]", static_cast<const char*>(nullptr)))
  {
    Fail("printf null string mismatch");
  }
}
}  // namespace LibXRPrintTest
