/**
 * @file test_printf_frontend_text.cpp
 * @brief 默认 profile 的 `printf` 文本/杂项运行时输出测试。 Default-profile `printf`
 * text/misc runtime output tests.
 * @details
 * 1. 覆盖字符、字符串、指针、长度修饰符和 positional 参数。
 * 2. 覆盖 `std::string` / `std::string_view` / char array 的 LibXR 参数适配。
 * 3. 这些场景不属于纯整数或纯浮点格式族。
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
/**
 * @brief 覆盖默认 `printf` profile 下的文本、指针和 positional 输出语义。 Cover text,
 * pointer, and positional output semantics under the default `printf` profile.
 */
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
}  // namespace LibXRPrintTest
