#include "writer.hpp"

namespace LibXR::Print
{
Writer::CodeReader::CodeReader(const uint8_t* codes) : pos_(codes), base_(codes) {}

FormatOp Writer::CodeReader::ReadOp() { return static_cast<FormatOp>(*pos_++); }

FormatType Writer::CodeReader::ReadFormatType()
{
  return static_cast<FormatType>(*pos_++);
}

Writer::Spec Writer::CodeReader::ReadSpec()
{
  return Spec{.flags = *pos_++,
              .fill = static_cast<char>(*pos_++),
              .width = *pos_++,
              .precision = *pos_++};
}

std::string_view Writer::CodeReader::ReadInlineText()
{
  auto text = reinterpret_cast<const char*>(pos_);
  size_t size = std::strlen(text);
  pos_ += size + 1;
  return std::string_view(text, size);
}

std::string_view Writer::CodeReader::ReadTextRef()
{
  auto offset = Read<uint16_t>();
  auto size = Read<uint16_t>();
  auto text = reinterpret_cast<const char*>(base_ + offset);
  return std::string_view(text, size);
}

Writer::ArgumentReader::ArgumentReader(const uint8_t* data) : pos_(data) {}

bool Writer::AppendBufferChar(char* buffer, size_t capacity, size_t& size, char ch)
{
  if (size >= capacity)
  {
    return false;
  }
  buffer[size++] = ch;
  return true;
}

bool Writer::AppendBufferText(char* buffer, size_t capacity, size_t& size,
                              std::string_view text)
{
  if (size > capacity || text.size() > capacity - size)
  {
    return false;
  }
  std::memcpy(buffer + size, text.data(), text.size());
  size += text.size();
  return true;
}

bool Writer::AppendBufferU32ZeroPad(char* buffer, size_t capacity, size_t& size,
                                    uint32_t value, uint8_t width)
{
  char digits[UnsignedDigitCapacity<uint32_t, 10>()];
  size_t digit_count = AppendUnsigned<10>(digits, value);
  size_t zero_count =
      (width > digit_count) ? static_cast<size_t>(width) - digit_count : 0;

  for (size_t i = 0; i < zero_count; ++i)
  {
    if (!AppendBufferChar(buffer, capacity, size, '0'))
    {
      return false;
    }
  }

  return AppendBufferText(buffer, capacity, size, std::string_view(digits, digit_count));
}

#if LIBXR_PRINT_ENABLE_FLOAT

size_t Writer::TrimGeneralText(char* text, size_t size)
{
  size_t exponent_pos = size;
  for (size_t i = 0; i < size; ++i)
  {
    if (text[i] == 'e' || text[i] == 'E')
    {
      exponent_pos = i;
      break;
    }
  }

  size_t mantissa_end = exponent_pos;
  while (mantissa_end > 0 && text[mantissa_end - 1] == '0')
  {
    --mantissa_end;
  }
  if (mantissa_end > 0 && text[mantissa_end - 1] == '.')
  {
    --mantissa_end;
  }

  if (exponent_pos == size)
  {
    return mantissa_end;
  }

  std::memmove(text + mantissa_end, text + exponent_pos, size - exponent_pos);
  return mantissa_end + (size - exponent_pos);
}

bool Writer::AppendExponentText(char* out, size_t& out_size, int exponent,
                                bool upper_case)
{
  if (!AppendBufferChar(out, float_buffer_capacity, out_size, upper_case ? 'E' : 'e'))
  {
    return false;
  }
  if (!AppendBufferChar(out, float_buffer_capacity, out_size, exponent < 0 ? '-' : '+'))
  {
    return false;
  }

  char digits[UnsignedDigitCapacity<unsigned int, 10>()];
  unsigned int magnitude = static_cast<unsigned int>(exponent < 0 ? -exponent : exponent);
  size_t digit_count = AppendUnsigned<10>(digits, magnitude);
  if (digit_count < 2 && !AppendBufferChar(out, float_buffer_capacity, out_size, '0'))
  {
    return false;
  }

  return AppendBufferText(out, float_buffer_capacity, out_size,
                          std::string_view(digits, digit_count));
}

#endif
}  // namespace LibXR::Print
