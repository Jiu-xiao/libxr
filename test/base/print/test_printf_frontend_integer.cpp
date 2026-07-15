/**
 * @file test_printf_frontend_integer.cpp
 * @brief 默认 profile 的 `printf` 整数族运行时输出测试。 Default-profile `printf`
 * integer-family runtime output tests.
 * @details
 * 1. 标准 C printf 整数格式对照 host `snprintf`。
 * 2. LibXR 扩展 `%b/%B` 对照固定期望文本。
 * 3. 长度修饰符和 enum 参数覆盖参数打包边界。
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 覆盖默认 `printf` profile 下的整数输出语义。 Cover integer output semantics
 * under the default `printf` profile.
 */
void TestPrintfFrontendIntegerSemantics()
{
  // 无参数文本路径：确认字面量文本不会经过格式参数路径后被改写。
  // No-argument text path: literal text must survive without argument formatting.
  if (!SameAsSnprintf<"abc">())
  {
    Fail("plain text mismatch");
  }

  if (!SameAsSnprintf<"0123456789abcdef">())
  {
    Fail("long plain text mismatch");
  }

  // 有符号十进制：符号、空格、零填充、左对齐和零值精度语义。
  // Signed decimal: sign, space, zero padding, left alignment, and zero-value precision.
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

  // 无符号十进制和非十进制：标准 `%u/%x/%o` 对照 host，扩展 `%b/%B` 对照固定文本。
  // Unsigned and bases: standard `%u/%x/%o` compare with host; `%b/%B` use fixed text.
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

  // 64 位最大值覆盖多进制 writer 循环，避免只测小数值时漏掉高位路径。
  // Max 64-bit value covers the multi-base writer loops beyond small-value paths.
  {
    unsigned long long max_value = std::numeric_limits<unsigned long long>::max();
    std::string binary = UnsignedBaseText(max_value, 2);
    std::string octal = UnsignedBaseText(max_value, 8);
    std::string hex_lower = UnsignedBaseText(max_value, 16);
    std::string hex_upper = UnsignedBaseText(max_value, 16, true);
    std::string expected =
        binary + "|0b" + binary + "|0" + octal + "|" + hex_lower + "|" + hex_upper;
    if (!SamePrintfAsExpected<"%llb|%#llb|%#llo|%llx|%llX">(
            expected, max_value, max_value, max_value, max_value, max_value))
    {
      Fail("64-bit integer base semantics mismatch");
    }
  }

  // 长度修饰符和 enum：确认参数打包类型、整数提升和最终输出一致。
  // Length modifiers and enum arguments: verify packing, integer promotion, and output.
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
