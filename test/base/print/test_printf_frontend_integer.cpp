/**
 * @file test_printf_frontend_integer.cpp
 * @brief `print` printf 整数族前端语义子测试。 Split test unit for `print` printf integer-family frontend semantics.
 * @details 测试项目：
 *          1. 纯文本、十进制、有符号/无符号与进制转换语义。
 *          2. 标志位、精度和 64 位整数族语义。
 *          Test items:
 *          1. Plain text, decimal, signed/unsigned, and base-conversion semantics.
 *          2. Flag, precision, and 64-bit integer-family semantics.
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
void TestPrintfFrontendIntegerSemantics()
{
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
}
}  // namespace LibXRPrintTest
