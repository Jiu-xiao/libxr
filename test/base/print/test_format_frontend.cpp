/**
 * @file test_format_frontend.cpp
 * @brief `print` format 前端语义子测试。 Split test unit for `print` format frontend semantics.
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 测试项函数 `TestFormatFrontendSemantics`。 Test-item function `TestFormatFrontendSemantics`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestFormatFrontendSemantics()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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
    std::string expected = binary + "|0b" + binary + "|0" + octal + "|" + hex_lower +
                           "|" + hex_upper;
    if (!SameFormatAsExpected<"{:b}|{:#b}|{:#o}|{:x}|{:X}">(
            expected, max_value, max_value, max_value, max_value, max_value))
    {
      Fail("format frontend 64-bit base mismatch");
    }
  }

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
