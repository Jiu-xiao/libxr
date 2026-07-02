#pragma once

/**
 * @brief 浮点运行期后端的功能开关判断与有界文本缓冲辅助函数 / Runtime float-backend feature gates and bounded text-buffer helpers
 */

/**
 * @brief 判断某个运行期浮点语义是否走共享文本后端 / Return whether one runtime float semantic uses the shared text backend
 * @param type 运行期浮点字段类型 / Runtime float field type
 * @return 该字段类型走共享浮点文本后端时返回 `true`，否则返回 `false` / Returns `true` when this field type uses the shared float-text backend, otherwise `false`
 */
constexpr bool Writer::UsesFloatTextBackend(FormatType type)
{
  switch (type)
  {
    case FormatType::FloatScientific:
    case FormatType::DoubleScientific:
    case FormatType::LongDoubleScientific:
    case FormatType::FloatGeneral:
    case FormatType::DoubleGeneral:
    case FormatType::LongDoubleGeneral:
    case FormatType::FloatFixed:
    case FormatType::DoubleFixed:
    case FormatType::LongDoubleFixed:
      return true;
    default:
      return false;
  }
}

/**
 * @brief 判断某个运行期浮点语义是否在当前配置下启用 / Return whether one runtime float semantic is enabled under current config
 * @param type 运行期浮点字段类型 / Runtime float field type
 * @return 当前配置启用了该字段类型时返回 `true`，否则返回 `false` / Returns `true` when this field type is enabled, otherwise `false`
 */
constexpr bool Writer::FloatEnabled(FormatType type)
{
  switch (type)
  {
    case FormatType::FloatFixed:
      return Config::enable_float_fixed;
    case FormatType::FloatScientific:
      return Config::enable_float_scientific;
    case FormatType::FloatGeneral:
      return Config::enable_float_general;
    case FormatType::DoubleFixed:
      return Config::enable_float_double && Config::enable_float_fixed;
    case FormatType::DoubleScientific:
      return Config::enable_float_double && Config::enable_float_scientific;
    case FormatType::DoubleGeneral:
      return Config::enable_float_double && Config::enable_float_general;
    case FormatType::LongDoubleFixed:
      return Config::enable_float_long_double && Config::enable_float_fixed;
    case FormatType::LongDoubleScientific:
      return Config::enable_float_long_double && Config::enable_float_scientific;
    case FormatType::LongDoubleGeneral:
      return Config::enable_float_long_double && Config::enable_float_general;
    default:
      return false;
  }
}

/**
 * @brief 返回格式串没有显式指定精度时使用的默认浮点精度 / Return the default float precision used when the format string did not specify one
 * @return 返回默认浮点精度 / Returns the default float precision
 */
constexpr uint8_t Writer::DefaultFloatPrecision()
{
  return Config::max_float_precision < 6 ? Config::max_float_precision : 6;
}

/**
 * @brief 判断定点形态的浮点输出是否会超出当前配置的整数位数上限 / Return whether fixed-form float output would exceed the configured integer-digit limit
 * @tparam Float 浮点类型 / Float type
 * @param value 待检查的浮点绝对值 / Float magnitude to test
 * @param precision 请求的小数精度 / Requested fractional precision
 * @return 若定点输出的整数部分会超出配置上限则返回 `true`，否则返回 `false` / Returns `true` when the fixed-form integer part would exceed the configured limit, otherwise `false`
 */
template <typename Float>
bool Writer::ExceedsFixedIntegerDigits(Float value, uint8_t precision)
{
  if (!std::isfinite(value))
  {
    return false;
  }

  Float rounded = RoundDecimal(value, precision);
  if (!std::isfinite(rounded))
  {
    // RoundDecimal returns signed infinity when value * 10^precision overflows.
    // Treat this as exceeding the integer digit limit.
    return true;
  }
  if (rounded == 0)
  {
    return 1 > Config::max_float_integer_digits;
  }

  auto normalized = NormalizeDecimal(rounded);
  size_t integer_digits =
      (normalized.exponent >= 0) ? static_cast<size_t>(normalized.exponent) + 1U : 1U;
  return integer_digits > Config::max_float_integer_digits;
}

/**
 * @brief 向局部有界浮点格式缓冲区追加一个字符 / Append one character into a bounded local float-format buffer
 * @param buffer 目标缓冲区 / Destination buffer
 * @param capacity 缓冲区总容量 / Total buffer capacity
 * @param size 当前已保留长度；成功时会推进 / Current retained size; advanced on success
 * @param ch 待追加字符 / Character to append
 * @return 成功返回 `true`，否则返回 `false` / Returns `true` on success, otherwise `false`
 */
