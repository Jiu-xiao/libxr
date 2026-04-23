#include <iostream>
#include <string>

#include "libxr_format.hpp"

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

using InlineText = LibXR::Format<"abc">;
using RefText = LibXR::Format<"0123456789abcdef">;

static_assert(InlineText::ArgumentCount() == 0);
static_assert(RefText::ArgumentCount() == 0);
static_assert(InlineText::Data()[0] ==
              static_cast<uint8_t>(InlineText::RecordType::TextInline));
static_assert(RefText::Data()[0] ==
              static_cast<uint8_t>(RefText::RecordType::TextRef));
static_assert(RefText::RecordBytes() == 1 + sizeof(LibXR::TextOffset) +
                                            sizeof(LibXR::TextLength) + 1);

int Fail(const char* message)
{
  std::cerr << message << '\n';
  return 1;
}
}  // namespace

int main()
{
  {
    StringSink sink;
    constexpr LibXR::Format<"x=%+08d y=%-4s z=%#x %% %.2f"> format;
    auto ec = LibXR::Print::Write(sink, format, -12, "ok", 42U, 1.25);
    if (ec != LibXR::ErrorCode::OK)
    {
      return Fail("format write failed");
    }
    if (sink.buffer != "x=-0000012 y=ok   z=0x2a % 1.25")
    {
      return Fail("formatted output mismatch");
    }
  }

  {
    StringSink sink;
    constexpr LibXR::Format<"0123456789abcdef"> format;
    auto text_base =
        reinterpret_cast<const char*>(format.Data().data() + format.RecordBytes());
    if (std::string_view(text_base, 16) != "0123456789abcdef")
    {
      return Fail("text-pool content mismatch");
    }

    auto ec = format.WriteTo(sink);
    if (ec != LibXR::ErrorCode::OK)
    {
      return Fail("text-ref write failed");
    }
    if (sink.buffer != "0123456789abcdef")
    {
      return Fail("text-ref output mismatch");
    }
  }

  {
    int value = 7;
    StringSink sink;
    constexpr LibXR::Format<"%c %.3s %p"> format;
    auto ec = format.WriteTo(sink, 'A', "abcdef", &value);
    if (ec != LibXR::ErrorCode::OK)
    {
      return Fail("mixed write failed");
    }
    if (!sink.buffer.starts_with("A abc 0x"))
    {
      return Fail("pointer output mismatch");
    }
  }

  {
    char text[8] = "abc";
    StringSink sink;
    constexpr LibXR::Format<"[%s]"> format;
    auto ec = format.WriteTo(sink, text);
    if (ec != LibXR::ErrorCode::OK)
    {
      return Fail("char-array write failed");
    }
    if (sink.buffer != "[abc]")
    {
      return Fail("char-array output mismatch");
    }
  }

  {
    long signed_value = -12;
    size_t unsigned_value = 34;
    long double float_value = 1.25L;
    StringSink sink;
    constexpr LibXR::Format<"%ld %zu %.2Lf"> format;
    auto ec = format.WriteTo(sink, signed_value, unsigned_value, float_value);
    if (ec != LibXR::ErrorCode::OK)
    {
      return Fail("length-modifier write failed");
    }
    if (sink.buffer != "-12 34 1.25")
    {
      return Fail("length-modifier output mismatch");
    }
  }

  return 0;
}
