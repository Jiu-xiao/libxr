/**
 * @file test_format_frontend.cpp
 * @brief 默认 profile 的 brace `Format` 运行时输出测试。 Default-profile brace
 * `Format` runtime output tests.
 * @details
 * 1. 覆盖 `{}` 字面量编译后的 writer 输出。
 * 2. 覆盖转义、显式索引、整数进制、字符串、浮点和指针。
 * 3. 覆盖 `WriteTo` 直接写入路径。
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 覆盖默认 brace `Format` profile 下的输出语义。 Cover output semantics under the
 * default brace `Format` profile.
 * @details 期望文本写在测试内；brace 格式不是 host `snprintf` 方言。
 */
void TestFormatFrontendSemantics()
{
  // 文本、转义和索引：确认 `{}` 前端最基础的 source 解析和参数重排。
  // Text, escapes, and indexing: basic source parsing plus argument reordering.
  if (!SameFormatAsExpected<"abc">("abc"))
  {
    Fail("format frontend plain text mismatch");
  }

  if (!SameFormatAsExpected<"{{x}}={0} {1} {0}">("{x}=7 ok 7", 7, "ok"))
  {
    Fail("format frontend reorder and escapes mismatch");
  }

  if (!SameFormatAsExpected<"x={:+08d} y={:*^6s} z={:#x} {:.2f}">(
          "x=-0000012 y=**ok** z=0x2a 1.25", -12, "ok", 42U, 1.25))
  {
    Fail("format frontend mixed mismatch");
  }

  // 整数进制：二/八/十六进制和 64 位最大值覆盖 base writer 的宽路径。
  // Integer bases: binary/octal/hex plus max 64-bit values cover the wider base-writer
  // path.
  if (!SameFormatAsExpected<"{:#b} {:#B} {:#o}">("0b101 0B101 010", 5U, 5U, 8U))
  {
    Fail("format frontend non-decimal mismatch");
  }

  {
    uint64_t max_value = std::numeric_limits<uint64_t>::max();
    std::string binary = UnsignedBaseText(max_value, 2);
    std::string octal = UnsignedBaseText(max_value, 8);
    std::string hex_lower = UnsignedBaseText(max_value, 16);
    std::string hex_upper = UnsignedBaseText(max_value, 16, true);
    std::string expected =
        binary + "|0b" + binary + "|0" + octal + "|" + hex_lower + "|" + hex_upper;
    if (!SameFormatAsExpected<"{:b}|{:#b}|{:#o}|{:x}|{:X}">(
            expected, max_value, max_value, max_value, max_value, max_value))
    {
      Fail("format frontend 64-bit base mismatch");
    }
  }

  // 文本族：字符串精度/填充、字符输出和默认左对齐。
  // Text family: string precision/fill, character output, and default left alignment.
  if (!SameFormatAsExpected<"[{:.3s}] [{:_>6s}] [{:*^7s}]">("[abc] [___abc] [**abc**]",
                                                            "abcdef", "abc", "abc"))
  {
    Fail("format frontend string field mismatch");
  }

  if (!SameFormatAsExpected<"{:c} {:c}">("A B", 'A', 66))
  {
    Fail("format frontend character mismatch");
  }

  if (!SameFormatAsExpected<"{:.2f}|{:.1E}|{:g}">("1.25|1.2E+01|12", 1.25, 12.0, 12.0))
  {
    Fail("format frontend float mismatch");
  }

  // Keep brace-format float behavior aligned with the shared printf writer path.
  if (!SameFormatAsExpected<"{:.0f}|{:.0f}|{:.0f}|{:.0f}">("0|2|2|4", 0.5, 1.5,
                                                                2.5, 3.5))
  {
    Fail("format frontend float half-even mismatch");
  }

  if (!SameFormatAsExpected<"{:010f}|{:+010f}|{: 010f}|{:010f}">(
          "       inf|      +inf|       inf|       nan",
          std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()))
  {
    Fail("format frontend inf nan zero padding mismatch");
  }

  if (!SameFormatAsExpected<"{:.2e}|{:.2g}">("1.80e+308|1.8e+308",
                                             std::numeric_limits<double>::max(),
                                             std::numeric_limits<double>::max()))
  {
    Fail("format frontend max double scientific mismatch");
  }

  if (!SameFormatAsExpected<"{:.6g}|{:.6g}|{:.5g}|{:.4g}|{:.6g}|{:.6g}">(
          "999999|1e+06|1e+05|1e+04|9.99999e-05|0.0001", 999999.4,
          999999.5, 99999.9, 9999.9, 0.0000999999, 0.00009999999))
  {
    Fail("format frontend rounded exponent mismatch");
  }

  if (!SameFormatAsExpected<"{:+d}|{: d}|{:08d}|{:#x}">("+7| 7|00000007|0x2a", 7, 7, 7,
                                                        42U))
  {
    Fail("format frontend integer flag mismatch");
  }

  // 显式索引和浮点边界：参数重排、负零、inf/nan 文本。
  // Explicit indexes and float boundaries: argument reorder, negative zero, and inf/nan
  // text.
  if (!SameFormatAsExpected<"{1:+08d} {0:#x} {1}">("-0000012 0x2a -12", 42U, -12))
  {
    Fail("format frontend indexed spec mismatch");
  }

  if (!SameFormatAsExpected<"{:f}|{:E}|{:g}">("-0.000000|-0.000000E+00|-0", -0.0, -0.0,
                                              -0.0))
  {
    Fail("format frontend negative zero mismatch");
  }

  if (!SameFormatAsExpected<"{:f}|{:F}|{:e}">("inf|-INF|nan",
                                              std::numeric_limits<double>::infinity(),
                                              -std::numeric_limits<double>::infinity(),
                                              std::numeric_limits<double>::quiet_NaN()))
  {
    Fail("format frontend inf nan mismatch");
  }

  // Near-boundary digit extraction: values just below a rounding threshold
  // must not carry over. Regression for ExtractDigit() epsilon over-bias.
  if (!SameFormatAsExpected<"{:.6f}|{:.6f}">(
          "1.999999|0.999999", 1.999999f, 0.999999f))
  {
    Fail("f32 fixed near-boundary digit extraction mismatch");
  }

  // F32 fixed fast-path (FormatOp::F32FixedPrec) non-finite coverage.
  // {:.2f} with a float hits the fast path; these verify it delegates to the
  // generic writer instead of misbehaving on NaN/inf.
  if (!SameFormatAsExpected<"{:.2f}|{:.2f}|{:.2f}">(
          "nan|inf|-inf",
          std::numeric_limits<float>::quiet_NaN(),
          std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()))
  {
    Fail("f32 fast-path nan/inf mismatch");
  }

  // NaN sign policy: signbit is ignored; explicit sign flag is honored.
  if (!SameFormatAsExpected<"{:.2f}|{:-.2f}|{:+.2f}|{: .2f}">(
          "nan|nan|+nan| nan",
          std::numeric_limits<float>::quiet_NaN(),
          std::numeric_limits<float>::quiet_NaN(),
          std::numeric_limits<float>::quiet_NaN(),
          std::numeric_limits<float>::quiet_NaN()))
  {
    Fail("f32 nan sign flag mismatch");
  }

  // F32 fixed fast-path overflow: float::max() * 10^2 overflows; must
  // return OUT_OF_RANGE, not NO_BUFF or undefined behavior.
  {
    constexpr LibXR::Format<"{:.2f}"> format{};
    StringSink sink;
    auto ec = LibXR::Print::Write(sink, format, std::numeric_limits<float>::max());
    if (ec != LibXR::ErrorCode::OUT_OF_RANGE)
    {
      Fail("f32 fast-path overflow should return OUT_OF_RANGE");
    }
  }

  // LibXR 参数适配：对象字符串、定长 char 数组和空 C 字符串。
  // LibXR argument adapters: object strings, bounded char arrays, and null C strings.
  if (!SameFormatAsExpected<"[{:3}] [{:6}]">("[A  ] [abc   ]", 'A', "abc"))
  {
    Fail("format frontend default text alignment mismatch");
  }

  {
    std::string text = "hello";
    std::string_view view = "xy";
    if (!SameFormatAsExpected<"[{}][{}]">("[hello][xy]", text, view))
    {
      Fail("format frontend string object mismatch");
    }
  }

  {
    char bounded_text[4] = {'i', 'm', 'u', '\0'};
    const char embedded_text[5] = {'a', 'b', '\0', 'c', 'd'};
    if (!SameFormatAsExpected<"[{}][{}]">("[imu][ab]", bounded_text, embedded_text))
    {
      Fail("format frontend bounded char array mismatch");
    }
  }

  if (!SameFormatAsExpected<"[{}]">("[(null)]", static_cast<const char*>(nullptr)))
  {
    Fail("format frontend null string mismatch");
  }

  // 直接 writer 路径和指针默认格式：不经过 helper 包装也应生成同一类输出。
  // Direct writer path and pointer defaults: output should match without helper wrappers.
  {
    constexpr LibXR::Format<"{:c} {:.3s} {:p}"> format{};
    int value = 7;
    StringSink sink;
    auto ec = format.WriteTo(sink, 'A', "abcdef", &value);
    if (ec != LibXR::ErrorCode::OK || !sink.buffer.starts_with("A abc 0x"))
    {
      Fail("format frontend writeto mismatch");
    }
  }

  {
    int value = 0;
    std::string expected = "ptr=" + PointerText(&value);
    if (!SameFormatAsExpected<"ptr={}">(expected, &value))
    {
      Fail("format frontend pointer default mismatch");
    }
  }

  {
    int value = 0;
    std::string pointer = PointerText(&value);
    std::string expected = "[";
    if (pointer.size() < 32)
    {
      expected.append(32 - pointer.size(), ' ');
    }
    expected += pointer;
    expected.push_back(']');
    if (!SameFormatAsExpected<"[{:32}]">(expected, &value))
    {
      Fail("format frontend pointer default alignment mismatch");
    }
  }
}
}  // namespace LibXRPrintTest
