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

uint64_t Writer::RoundScaledF32(float value, uint32_t scale)
{
  uint32_t bits = std::bit_cast<uint32_t>(value);
  uint32_t exponent_bits = (bits >> 23) & 0xFFU;
  uint32_t fraction_bits = bits & 0x7FFFFFU;
  uint32_t significand =
      (exponent_bits == 0) ? fraction_bits : ((1U << 23) | fraction_bits);
  int exponent2 = (exponent_bits == 0) ? -149 : static_cast<int>(exponent_bits) - 150;
  uint64_t numerator = static_cast<uint64_t>(significand) * scale;

  if (exponent2 >= 0)
  {
    return numerator << exponent2;
  }

  unsigned int shift = static_cast<unsigned int>(-exponent2);
  if (shift >= 64)
  {
    return 0;
  }

  uint64_t quotient = numerator >> shift;
  uint64_t remainder = numerator & ((uint64_t{1} << shift) - 1U);
  uint64_t halfway = uint64_t{1} << (shift - 1);
  if (remainder > halfway || (remainder == halfway && (quotient & 1U) != 0U))
  {
    ++quotient;
  }
  return quotient;
}

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

bool Writer::FormatF32FixedPrecText(float value, uint8_t precision, char* out,
                                    size_t& out_size)
{
  out_size = 0;

  if (std::isnan(value))
  {
    return AppendBufferText(out, float_buffer_capacity, out_size, "nan");
  }
  if (std::isinf(value))
  {
    return AppendBufferText(out, float_buffer_capacity, out_size, "inf");
  }

  if (precision < f32_decimal_scales_u32.size() && value < f32_u32_overflow_limit)
  {
    uint32_t integer_part = static_cast<uint32_t>(value);
    uint32_t scale = f32_decimal_scales_u32[precision];
    uint64_t scaled_total = RoundScaledF32(value, scale);
    uint64_t scaled_integer = static_cast<uint64_t>(integer_part) * scale;
    uint32_t fractional_part = (scaled_total >= scaled_integer)
                                   ? static_cast<uint32_t>(scaled_total - scaled_integer)
                                   : 0U;

    if (fractional_part >= scale)
    {
      fractional_part -= scale;
      if (integer_part == std::numeric_limits<uint32_t>::max())
      {
        if (!AppendBufferText(out, float_buffer_capacity, out_size, "4294967296"))
        {
          return false;
        }
        if (precision == 0)
        {
          return true;
        }
        if (!AppendBufferChar(out, float_buffer_capacity, out_size, '.'))
        {
          return false;
        }
        for (uint8_t i = 0; i < precision; ++i)
        {
          if (!AppendBufferChar(out, float_buffer_capacity, out_size, '0'))
          {
            return false;
          }
        }
        return true;
      }
      ++integer_part;
    }

    if (!AppendBufferU32ZeroPad(out, float_buffer_capacity, out_size, integer_part, 1))
    {
      return false;
    }
    if (precision == 0)
    {
      return true;
    }
    if (!AppendBufferChar(out, float_buffer_capacity, out_size, '.'))
    {
      return false;
    }
    return AppendBufferU32ZeroPad(out, float_buffer_capacity, out_size, fractional_part,
                                  precision);
  }

  float rounded = value;
  float rounding = 0.5f;
  for (uint8_t i = 0; i < precision; ++i)
  {
    rounding *= 0.1f;
  }
  rounded += rounding;

  if (rounded < 1.0f)
  {
    if (!AppendBufferChar(out, float_buffer_capacity, out_size, '0'))
    {
      return false;
    }
  }
  else
  {
    float integer_scale = 1.0f;
    while (true)
    {
      float next_scale = integer_scale * 10.0f;
      if (!std::isfinite(next_scale) || rounded < next_scale)
      {
        break;
      }
      integer_scale = next_scale;
    }

    while (integer_scale >= 1.0f)
    {
      int digit = static_cast<int>(rounded / integer_scale);
      if (digit < 0)
      {
        digit = 0;
      }
      else if (digit > 9)
      {
        digit = 9;
      }

      if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                            static_cast<char>('0' + digit)))
      {
        return false;
      }

      rounded -= static_cast<float>(digit) * integer_scale;
      float epsilon = integer_scale * 1e-6f;
      if (rounded < 0.0f && rounded > -epsilon)
      {
        rounded = 0.0f;
      }
      integer_scale *= 0.1f;
    }
  }

  if (precision == 0)
  {
    return true;
  }
  if (!AppendBufferChar(out, float_buffer_capacity, out_size, '.'))
  {
    return false;
  }

  for (uint8_t i = 0; i < precision; ++i)
  {
    rounded *= 10.0f;
    int digit = static_cast<int>(rounded + 1e-6f);
    if (digit < 0)
    {
      digit = 0;
    }
    else if (digit > 9)
    {
      digit = 9;
    }

    if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                          static_cast<char>('0' + digit)))
    {
      return false;
    }

    rounded -= static_cast<float>(digit);
    if (rounded < 0.0f && rounded > -1e-5f)
    {
      rounded = 0.0f;
    }
  }

  return true;
}

#endif
}  // namespace LibXR::Print
