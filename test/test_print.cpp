#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "print/print.hpp"

namespace
{
struct StringSink
{
  LibXR::ErrorCode Write(std::string_view text)
  {
    buffer.append(text.data(), text.size());
    return LibXR::ErrorCode::OK;
  }

  std::string buffer;
};

template <LibXR::Print::Text Source, typename... Args>
bool SameAsSnprintf(Args... args)
{
  std::array<char, 1024> expected{};
  int expected_size =
      std::snprintf(expected.data(), expected.size(), Source.Data(), args...);
  if (expected_size < 0 ||
      static_cast<size_t>(expected_size) >= expected.size())
  {
    return false;
  }

  StringSink sink;
  constexpr auto format = LibXR::Print::Printf::Build<Source>();
  auto ec = LibXR::Print::Write(sink, format, args...);
  if (ec != LibXR::ErrorCode::OK)
  {
    return false;
  }

  return sink.buffer ==
         std::string_view(expected.data(), static_cast<size_t>(expected_size));
}

int Fail(const char* message)
{
  std::cerr << message << '\n';
  ASSERT(false);
  return 0;
}
}  // namespace

void test_print()
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

  if (!SameAsSnprintf<"%s|%.3s|%6.3s|%-6.3s">("abcdef", "abcdef", "abcdef",
                                              "abcdef"))
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
            tiny_signed, tiny_unsigned, short_signed, short_unsigned,
            long_long_signed, long_long_unsigned, max_signed, max_unsigned,
            ptrdiff_signed, ptrdiff_unsigned))
    {
      Fail("integer length family mismatch");
    }
  }

  if (!SameAsSnprintf<"%Lg|%Le|%Lf">(1.25L, 1.25L, 1.25L))
  {
    Fail("long double family mismatch");
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

  if (!SameAsSnprintf<"%f|%F|%e">(
          std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()))
  {
    Fail("float inf nan mismatch");
  }

  {
    int value = 0;
    if (!SameAsSnprintf<
            "a%d 0123456789abcdef %u %o %x %X %p %c %s %f %e %g %Lf %Le %Lg">(
            -1, 2U, 8U, 42U, 42U, &value, 'Q', "xy", 1.5, 1.5, 1.5, 2.25L,
            2.25L, 2.25L))
    {
      Fail("full supported family mismatch");
    }
  }
}
