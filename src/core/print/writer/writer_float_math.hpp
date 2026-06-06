#pragma once

/**
 * @brief 通用浮点文本格式化器使用的数学辅助函数。 / Math helpers used by the generic float text formatter.
 */

/**
 * @brief 使用 float32 位级算术对 `value * scale` 做舍入 / Round `value * scale` using float32 bit-level arithmetic
 * @param value 待缩放的 float32 绝对值 / Float32 magnitude to scale
 * @param scale 十进制缩放因子 / Decimal scale factor
 * @return 舍入后的缩放整数 / Returns the rounded scaled integer
 */
/**
 * @brief 浮点文本输出归一化过程中使用的十进制缩放对 / Decimal-scale pair used while normalizing one float for text output.
 * @tparam Float Float type. / 浮点类型。
 */
template <typename Float>
struct Writer::DecimalScale
{
  int exponent = 0;  ///< decimal exponent / 十进制指数
  Float scale = 1;   ///< 10 ^ exponent / 10 的 exponent 次幂
};

/**
 * @brief 返回 10 的指定十进制指数次幂 / Returns 10 raised to the given decimal exponent.
 * @tparam Float Float type. / 浮点类型。
 * @param exponent Decimal exponent. / 十进制指数。
 * @return Returns the corresponding power of ten. / 返回对应的十进制幂。
 */
template <typename Float>
Float Writer::Power10(int exponent)
{
  Float result = 1;
  Float base = 10;
  unsigned int remaining =
      static_cast<unsigned int>(exponent < 0 ? -exponent : exponent);

  while (remaining != 0)
  {
    if ((remaining & 1U) != 0U)
    {
      if (exponent < 0)
      {
        result /= base;
      }
      else
      {
        result *= base;
      }
    }

    remaining >>= 1U;
    if (remaining != 0U)
    {
      base *= base;
    }
  }

  return result;
}

/**
 * @brief 将一个浮点值归一化为十进制指数与缩放因子 / Normalizes one float into a decimal exponent plus scale pair.
 * @tparam Float Float type. / 浮点类型。
 * @param value Finite positive magnitude. / 有限正数绝对值。
 * @return Returns the normalized decimal scale description. /
 *         返回规范化后的十进制缩放描述。
 */
template <typename Float>
Writer::DecimalScale<Float> Writer::NormalizeDecimal(Float value)
{
  DecimalScale<Float> normalized{};
  if (value == 0)
  {
    return normalized;
  }

  int binary_exponent = 0;
  std::frexp(value, &binary_exponent);
  constexpr Float log10_of_2 =
      static_cast<Float>(0.30102999566398119521373889472449L);
  normalized.exponent =
      static_cast<int>(static_cast<Float>(binary_exponent - 1) * log10_of_2);
  normalized.scale = Power10<Float>(normalized.exponent);

  Float scaled = value / normalized.scale;
  while (scaled < 1)
  {
    normalized.scale /= 10;
    --normalized.exponent;
    scaled *= 10;
  }
  while (scaled >= 10)
  {
    normalized.scale *= 10;
    ++normalized.exponent;
    scaled /= 10;
  }

  return normalized;
}

/**
 * @brief 在当前缩放位提取一个十进制数字并推进剩余值 / Extracts one decimal digit at the current scale and advances the remainder.
 * @tparam Float Float type. / 浮点类型。
 * @param value Remaining normalized value; reduced in place. / 剩余规范化值；
 *        会原地减少。
 * @param scale Current decimal scale. / 当前十进制权重。
 * @return Returns the extracted decimal digit. / 返回提取出的十进制数字。
 */
template <typename Float>
uint8_t Writer::ExtractDigit(Float& value, Float scale)
{
  Float scaled = value / scale;
  auto digit = static_cast<int>(scaled + static_cast<Float>(1e-12L));
  if (digit < 0)
  {
    digit = 0;
  }
  else if (digit > 9)
  {
    digit = 9;
  }

  value -= static_cast<Float>(digit) * scale;
  Float epsilon = scale * static_cast<Float>(1e-9L);
  if (value < 0 && value > -epsilon)
  {
    value = 0;
  }

  return static_cast<uint8_t>(digit);
}

/**
 * @brief 在通用格式中裁掉末尾零和多余的小数点 / Trim trailing zeros and the spare decimal point in general format
 * @param text 可修改的浮点文本缓冲区 / Mutable float text buffer
 * @param size 当前文本长度 / Current text size
 * @return 返回修剪后的文本长度 / Returns the trimmed text size
 */
