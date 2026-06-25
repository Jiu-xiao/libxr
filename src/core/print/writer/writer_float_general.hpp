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
  auto initial = NormalizeDecimal(value);
  Float rounded = value;
  if (value != 0)
  {
    rounded += static_cast<Float>(0.5L) *
               Power10<Float>(initial.exponent - static_cast<int>(precision));
  }

  auto normalized = NormalizeDecimal(rounded);
  int exponent = (rounded == 0) ? 0 : normalized.exponent;
  Float scale = Power10<Float>(exponent);

  out_size = 0;
  if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                        static_cast<char>('0' + ExtractDigit(rounded, scale))))
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

  scale /= 10;
  for (uint8_t i = 0; i < precision; ++i)
  {
    if (!AppendBufferChar(out, float_buffer_capacity, out_size,
                          static_cast<char>('0' + ExtractDigit(rounded, scale))))
    {
      return false;
    }
    scale /= 10;
  }

  return AppendExponentText(out, out_size, exponent, upper_case);
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
      int exponent = (value == 0) ? 0 : NormalizeDecimal(value).exponent;
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
