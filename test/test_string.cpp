#include "libxr_string.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_string() {
  LibXR::String<16> str1("hello");
  ASSERT(str1.Length() == 5);

  str1 += " world";
  LibXR::String<16> str2("hello world");
  ASSERT(str1 == str2);

  ASSERT(str1.Find("world") == 6);
  auto sub = str1.Substr<5>(6);
  LibXR::String<5> expected("world");
  ASSERT(sub == expected);

  str1.Clear();
  ASSERT(str1.Length() == 0);
}
