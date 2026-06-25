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
