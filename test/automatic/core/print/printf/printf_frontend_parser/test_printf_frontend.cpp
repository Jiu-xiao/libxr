/**
 * @file test_printf_frontend.cpp
 * @brief 检查 printf 格式的整数、文本、指针和浮点输出。 /
 * Tests printf integer, text, pointer and float formatting.
 *
 * 标准格式与 snprintf 比较，扩展格式使用固定预期文本。
 * Compares standard formats with snprintf and extensions with fixed expected text.
 */

#include "core/print/print_test_common.hpp"

namespace LibXRPrintTest
{
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

void TestPrintfFrontendTextSemantics()
{
  // 混合格式 smoke：一个格式串同时经过整数、字符串、转义百分号和浮点 writer。
  // Mixed-format smoke: one source string crosses integer, string, escaped percent, and
  // float writers.
  if (!SameAsSnprintf<"x=%+08d y=%-4s z=%#x %% %.2f">(-12, "ok", 42U, 1.25))
  {
    Fail("mixed format mismatch");
  }

  // 字符、字符串、指针：标准 printf 输出与 host `snprintf` 对齐。
  // Character, string, and pointer formatting should match host `snprintf`.
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

  // 长度修饰符和 positional 参数：验证参数索引、类型修饰和重复引用。
  // Length modifiers and positional arguments verify indexing, type modifiers, and reuse.
  {
    long signed_value = -12;
    size_t unsigned_value = 34;
    long double float_value = 1.25L;
    if (!SameAsSnprintf<"%ld %zu %.2Lf">(signed_value, unsigned_value, float_value))
    {
      Fail("length-modifier format mismatch");
    }
  }

  if (!SameAsSnprintf<"%2$u %1$s %2$#x">("ok", 7U))
  {
    Fail("positional argument semantics mismatch");
  }

  // LibXR 扩展参数适配：对象字符串、定长 char 数组和空 C 字符串。
  // LibXR argument adapters: object strings, bounded char arrays, and null C strings.
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

  if (!SamePrintfAsExpected<"[%s]">("[(null)]", static_cast<const char*>(nullptr)))
  {
    Fail("printf null string mismatch");
  }
}

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

void TestPrintfFrontendSemantics()
{
  // 默认 profile 下的三类 printf 输出语义。
  // Three printf output families under the default profile.
  TestPrintfFrontendIntegerSemantics();
  TestPrintfFrontendTextSemantics();
  TestPrintfFrontendFloatSemantics();
}
}  // namespace LibXRPrintTest
