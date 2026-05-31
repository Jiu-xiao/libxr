#pragma once

/**
 * @brief 定点精度浮点格式化辅助函数 / Fixed-precision float formatting helpers
 */

/**
 * @brief 将一个 float32 定点精度结果写入局部文本缓冲区 / Format one float32 fixed-precision payload into the local text buffer
 * @param value float32 绝对值 / Float32 magnitude
 * @param precision 请求的小数精度 / Requested fractional precision
 * @param out 目标文本缓冲区 / Destination text buffer
 * @param out_size 输出文本长度 / Output text size
 * @return 成功返回 `true`，否则返回 `false` / Returns `true` on success, otherwise `false`
 */
inline bool Writer::FormatF32FixedPrecText(float value, uint8_t precision, char* out,
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
    uint32_t fractional_part =
        (scaled_total >= scaled_integer)
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

/**
 * @brief 将一个通用定点浮点结果写入局部文本缓冲区 / Formats one generic fixed-point float payload into the local text buffer.
 * @tparam Float 浮点类型 / Float type
 * @param value 浮点绝对值 / Float magnitude
 * @param precision 请求的小数精度 / Requested fractional precision
 * @param alternate 是否启用备用格式 / Whether alternate form is enabled
 * @param out 目标文本缓冲区 / Destination text buffer
 * @param out_size 输出文本长度 / Output text size
 * @return 成功返回 `true`，否则返回 `false` / Returns `true` on success, otherwise `false`
 */
template <typename Float>
bool Writer::FormatFixedText(Float value, uint8_t precision, bool alternate, char* out,
                             size_t& out_size)
{
  Float rounded =
      value + static_cast<Float>(0.5L) * Power10<Float>(-static_cast<int>(precision));
  auto normalized = NormalizeDecimal(rounded);
  int integer_exponent = (rounded == 0) ? 0 : normalized.exponent;
  int start_pos = (integer_exponent > 0) ? integer_exponent : 0;
  Float scale = Power10<Float>(start_pos);

  out_size = 0;
  for (int pos = start_pos; pos >= 0; --pos)
  {
    if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                          static_cast<char>('0' + ExtractDigit(rounded, scale))))
    {
      return false;
    }
    scale /= 10;
  }

  if (precision != 0 || alternate)
  {
    if (!AppendBufferChar(out, float_buffer_capacity, out_size, '.'))
    {
      return false;
    }
  }

  for (uint8_t i = 0; i < precision; ++i)
  {
    if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                          static_cast<char>('0' + ExtractDigit(rounded, scale))))
    {
      return false;
    }
    scale /= 10;
  }

  return true;
}
