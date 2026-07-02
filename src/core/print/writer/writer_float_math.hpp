#pragma once

/**
 * @brief 通用浮点文本格式化器使用的数学辅助函数。 / Math helpers used by the generic float text formatter.
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

template <typename Float>
struct Writer::ScientificDigits
{
  Float digits = 0;
  Float scale = 1;
  int exponent = 0;
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
 * @brief 把一个值舍入到指定小数位 / Round one value to the requested decimal precision.
 * @tparam Float Float type. / 浮点类型。
 * @param value Finite non-negative value. / 有限非负值。
 * @param precision Decimal places to retain. / 保留的小数位数。
 * @return Rounded value, or +infinity if scaling would overflow (input is
 *         expected to be a non-negative magnitude). /
 *         返回舍入后的值；若缩放溢出且输入为非负数，则返回 +infinity。
 */
template <typename Float>
Float Writer::RoundDecimal(Float value, uint8_t precision)
{
  Float scale = Power10<Float>(static_cast<int>(precision));
  Float scaled = value * scale;
  if (!std::isfinite(scaled))
  {
    // Scaling overflowed: propagate as infinity so callers' isfinite() guard
    // rejects the result cleanly instead of silently returning the original
    // value and later producing OUT_OF_RANGE. Input is expected to be a
    // non-negative magnitude, so plain infinity is sufficient.
    return std::numeric_limits<Float>::infinity();
  }

  return std::nearbyint(scaled) / scale;
}

template <typename Float>
Writer::ScientificDigits<Float> Writer::RoundScientificDigits(Float value,
                                                              uint8_t precision)
{
  ScientificDigits<Float> result{};
  auto normalized = NormalizeDecimal(value);
  result.exponent = (value == 0) ? 0 : normalized.exponent;
  Float decimal_scale = Power10<Float>(result.exponent);
  Float mantissa = (value == 0) ? 0 : value / decimal_scale;
  result.scale = Power10<Float>(static_cast<int>(precision));
  result.digits = std::nearbyint(mantissa * result.scale);
  if (result.digits >= static_cast<Float>(10) * result.scale)
  {
    result.digits /= 10;
    ++result.exponent;
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
  // Use a type-appropriate bias (10x machine epsilon) to correct for
  // floating-point rounding when converting to int. The hardcoded 1e-12
  // was calibrated for double and has no effect on float or long double.
  auto digit = static_cast<int>(scaled + static_cast<Float>(10) * std::numeric_limits<Float>::epsilon());
  if (digit < 0)
  {
    digit = 0;
  }
  else if (digit > 9)
  {
    digit = 9;
  }

  value -= static_cast<Float>(digit) * scale;
  // Type-appropriate zero-clamping epsilon: scale relative to machine precision
  // instead of hardcoded 1e-9 (calibrated for double only).
  Float epsilon = scale * static_cast<Float>(10) * std::numeric_limits<Float>::epsilon();
  if (value < 0 && value > -epsilon)
  {
    value = 0;
  }

  return static_cast<uint8_t>(digit);
}
