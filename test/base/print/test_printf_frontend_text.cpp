/**
 * @file test_printf_frontend_text.cpp
 * @brief `print` printf 文本与杂项前端语义子测试。 Split test unit for `print` printf text/misc frontend semantics.
 * @details 测试项目：
 *          1. 混合格式、字符、字符串和指针语义。
 *          2. positional、对象字符串、数组字符串与空字符串语义。
 *          Test items:
 *          1. Mixed-format, character, string, and pointer semantics.
 *          2. Positional, object-string, bounded-array, and null-string semantics.
 */
#include "print_test_common.hpp"

namespace LibXRPrintTest
{
void TestPrintfFrontendTextSemantics()
{
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

  if (!SameAsSnprintf<"%2$u %1$s %2$#x">("ok", 7U))
  {
    Fail("positional argument semantics mismatch");
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

  if (!SamePrintfAsExpected<"[%s]">("[(null)]", static_cast<const char*>(nullptr)))
  {
    Fail("printf null string mismatch");
  }
}
}  // namespace LibXRPrintTest
