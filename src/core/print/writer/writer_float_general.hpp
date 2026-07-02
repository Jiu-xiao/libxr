#pragma once

/**
 * @brief 通用的浮点定点、科学计数法与通用格式文本辅助函数 / Generic float, scientific, and general text formatting helpers
 */

/**
 * @brief 将一个浮点值按科学计数法写入局部文本缓冲区 / Format one float in scientific notation into the local text buffer
 * @tparam Float 浮点类型 / Float type
 * @param value 浮点绝对值 / Float magnitude
 * @param precision 请求的小数精度 / Requested fractional precision
 * @param alternate 是否启用备用格式 / Whether alternate form is enabled
 * @param upper_case 指数标记是否使用 `E` / Whether the exponent marker should use `E`
 * @param out 目标文本缓冲区 / Destination text buffer
 * @param out_size 输出文本长度 / Output text size
 * @return 成功返回 `true`，否则返回 `false` / Returns `true` on success, otherwise `false`
 */
template <typename Float>
bool Writer::FormatScientificText(Float value, uint8_t precision, bool alternate,
                                  bool upper_case, char* out, size_t& out_size)
{
  auto rounded = RoundScientificDigits(value, precision);
  Float rounded_digits = rounded.digits;
  Float digit_scale = rounded.scale;

  out_size = 0;
  if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                        static_cast<char>('0' + ExtractDigit(rounded_digits, digit_scale))))
  {
    return false;
  }

  if (precision != 0 || alternate)
  {
    if (!AppendBufferChar(out, float_buffer_capacity, out_size, '.'))
    {
      return false;
    }
  }

  digit_scale /= 10;
  for (uint8_t i = 0; i < precision; ++i)
  {
    if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                          static_cast<char>('0' + ExtractDigit(rounded_digits, digit_scale))))
    {
      return false;
    }
    digit_scale /= 10;
  }

  return AppendExponentText(out, out_size, rounded.exponent, upper_case);
}

/**
 * @brief 将一个运行期浮点语义格式化到局部文本缓冲区 / Format one runtime float semantic into the local text buffer
 * @tparam Float 浮点类型 / Float type
 * @param type 运行期浮点字段类型 / Runtime float field type
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param value 浮点绝对值 / Float magnitude
 * @param out 目标文本缓冲区 / Destination text buffer
 * @param out_size 输出文本长度 / Output text size
 * @return 成功返回 `true`，否则返回 `false` / Returns `true` on success, otherwise `false`
 */
template <typename Float>
bool Writer::FormatFloatText(FormatType type, const Spec& spec, Float value, char* out,
                             size_t& out_size)
{
  out_size = 0;

  if (std::isnan(value))
  {
    return AppendBufferText(out, float_buffer_capacity, out_size,
                            spec.UpperCase() ? "NAN" : "nan");
  }
  if (std::isinf(value))
  {
    return AppendBufferText(out, float_buffer_capacity, out_size,
                            spec.UpperCase() ? "INF" : "inf");
  }

  uint8_t precision = spec.HasPrecision() ? spec.precision : DefaultFloatPrecision();
  switch (type)
  {
    case FormatType::FloatFixed:
    case FormatType::DoubleFixed:
    case FormatType::LongDoubleFixed:
      if (ExceedsFixedIntegerDigits(value, precision))
      {
        return false;
      }
      return FormatFixedText(value, precision, spec.Alternate(), out, out_size);
    case FormatType::FloatScientific:
    case FormatType::DoubleScientific:
    case FormatType::LongDoubleScientific:
      return FormatScientificText(value, precision, spec.Alternate(), spec.UpperCase(),
                                  out, out_size);
    case FormatType::FloatGeneral:
    case FormatType::DoubleGeneral:
    case FormatType::LongDoubleGeneral:
    {
      uint8_t significant = precision == 0 ? 1 : precision;
      // TODO(perf): RoundScientificDigits is called here only to read .exponent,
      // and then called again inside FormatScientificText. A future refactor can
      // add an overload that accepts a pre-computed ScientificDigits to avoid
      // the redundant calculation.
      int exponent =
          RoundScientificDigits(value, static_cast<uint8_t>(significant - 1)).exponent;
      if (exponent < -4 || exponent >= significant)
      {
        if (!FormatScientificText(value, static_cast<uint8_t>(significant - 1),
                                  spec.Alternate(), spec.UpperCase(), out, out_size))
        {
          return false;
        }
      }
      else
      {
        int fractional_precision = static_cast<int>(significant) - (exponent + 1);
        if (fractional_precision < 0)
        {
          fractional_precision = 0;
        }
        if (ExceedsFixedIntegerDigits(
                value, static_cast<uint8_t>(fractional_precision)))
        {
          return false;
        }
        if (!FormatFixedText(value, static_cast<uint8_t>(fractional_precision),
                             spec.Alternate(), out, out_size))
        {
          return false;
        }
      }

      if (!spec.Alternate())
      {
        out_size = TrimGeneralText(out, out_size);
      }
      return true;
    }
    default:
      return false;
  }
}
