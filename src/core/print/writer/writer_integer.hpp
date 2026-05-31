#pragma once

/**
 * @brief 运行期 writer 后端共享的整数文本辅助函数 / Shared integer-text helpers for the runtime writer backend
 */

/**
 * @brief 将一个无符号整数写入调用方提供的数字缓冲区 / Append one unsigned integer into a caller-provided digit buffer
 * @tparam UInt 无符号整数类型 / Unsigned integer type
 * @param out 目标数字缓冲区 / Destination digit buffer
 * @param value 待编码的无符号值 / Unsigned value to encode
 * @param base 整数进制 / Integer radix
 * @param upper_case 十六进制数字是否使用大写字母 / Whether hexadecimal digits should use uppercase letters
 * @return 返回输出的数字个数 / Returns the emitted digit count
 */
template <std::unsigned_integral UInt>
size_t Writer::AppendUnsigned(char* out, UInt value, uint8_t base, bool upper_case)
{
  constexpr char lower_digits[] = "0123456789abcdef";
  constexpr char upper_digits[] = "0123456789ABCDEF";
  const char* digits = upper_case ? upper_digits : lower_digits;
  char reverse[32];
  size_t count = 0;

  if (value == 0)
  {
    out[0] = '0';
    return 1;
  }

  while (value != 0)
  {
    reverse[count++] = digits[value % base];
    value /= base;
  }

  for (size_t i = 0; i < count; ++i)
  {
    out[i] = reverse[count - i - 1];
  }

  return count;
}

/**
 * @brief 追加一个较小的十进制无符号整数 / Append one small decimal unsigned integer
 * @param out 目标数字缓冲区 / Destination digit buffer
 * @param value 待编码的无符号值 / Unsigned value to encode
 * @return 返回输出的数字个数 / Returns the emitted digit count
 */
inline size_t Writer::AppendSmallUnsigned(char* out, uint8_t value)
{
  return AppendUnsigned(out, value, 10, false);
}

/**
 * @brief 返回某个载荷尺寸对应的字段宽度填充量 / Return the field-width padding needed for one payload size
 * @param width 请求的字段宽度 / Requested field width
 * @param payload_size 填充前的可见载荷长度 / Visible payload size before padding
 * @return 返回需要补上的填充个数 / Returns the required padding count
 */
constexpr size_t Writer::FieldPadding(uint8_t width, size_t payload_size)
{
  return (width > payload_size) ? static_cast<size_t>(width) - payload_size : 0;
}

/**
 * @brief 返回整数精度要求引入的额外前导零个数 / Return the extra leading zeros introduced by integer precision
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param digit_count 当前已有的数字个数 / Existing digit count
 * @return 返回额外前导零个数 / Returns the extra leading-zero count
 */
constexpr size_t Writer::IntegerPrecisionZeros(const Spec& spec, size_t digit_count)
{
  return (spec.HasPrecision() && spec.precision > digit_count)
             ? static_cast<size_t>(spec.precision) - digit_count
             : 0;
}

/**
 * @brief 返回某个运行期语义类型对应的整数进制 / Return the integer radix selected by one runtime semantic type
 * @param type 运行期字段类型 / Runtime field type
 * @return 返回选中的整数进制 / Returns the selected integer radix
 */
constexpr uint8_t Writer::IntegerBase(FormatType type)
{
  switch (type)
  {
    case FormatType::Unsigned32:
    case FormatType::Unsigned64:
      return 10;
    case FormatType::Binary32:
    case FormatType::Binary64:
      return 2;
    case FormatType::Octal32:
    case FormatType::Octal64:
      return 8;
    case FormatType::HexLower32:
    case FormatType::HexLower64:
    case FormatType::HexUpper32:
    case FormatType::HexUpper64:
      return 16;
    default:
      return 0;
  }
}

/**
 * @brief 判断某个运行期整数语义是否使用大写数字字符 / Return whether one runtime integer semantic uses uppercase digits
 * @param type 运行期字段类型 / Runtime field type
 * @return 需要输出大写数字字符时返回 `true`，否则返回 `false` / Returns `true` when uppercase digits are required, otherwise `false`
 */
constexpr bool Writer::IntegerUpperCase(FormatType type)
{
  return type == FormatType::HexUpper32 || type == FormatType::HexUpper64;
}

/**
 * @brief 返回备用格式下脱离数字主体输出的整数前缀 / Return the detached integer prefix emitted by alternate formatting
 * @tparam UInt 无符号整数类型 / Unsigned integer type
 * @param type 运行期字段类型 / Runtime field type
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param value 当前要输出的无符号值 / Unsigned value being emitted
 * @return 返回分离在数字主体之外的整数前缀 / Returns the detached integer prefix
 */
template <std::unsigned_integral UInt>
constexpr std::string_view Writer::IntegerPrefix(FormatType type, const Spec& spec,
                                                 UInt value)
{
  if (!spec.Alternate() || value == 0)
  {
    return {};
  }
  if (type == FormatType::HexLower32 || type == FormatType::HexLower64)
  {
    return "0x";
  }
  if (type == FormatType::HexUpper32 || type == FormatType::HexUpper64)
  {
    return "0X";
  }
  if (type == FormatType::Binary32 || type == FormatType::Binary64)
  {
    return spec.UpperCase() ? "0B" : "0b";
  }
  return {};
}

/**
 * @brief 直接在已生成数字上应用 `%#o` 的特殊规则 / Apply the special `%#o` payload rules directly onto the generated digits
 * @tparam UInt 无符号整数类型 / Unsigned integer type
 * @param digits 可修改的数字缓冲区 / Mutable digit buffer
 * @param digit_count 当前数字个数 / Current digit count
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param value 当前要输出的无符号值 / Unsigned value being emitted
 * @return 返回更新后的数字个数 / Returns the updated digit count
 */
template <std::unsigned_integral UInt>
size_t Writer::ApplyAlternateOctal(char* digits, size_t digit_count, const Spec& spec,
                                   UInt value)
{
  if (!spec.Alternate())
  {
    return (value == 0 && spec.precision == 0) ? 0 : digit_count;
  }

  if (value == 0 && spec.precision == 0)
  {
    return 1;
  }

  if (value == 0)
  {
    return digit_count;
  }

  if (spec.HasPrecision() && spec.precision > digit_count)
  {
    return digit_count;
  }

  digits[digit_count++] = '0';
  for (size_t i = digit_count - 1; i > 0; --i)
  {
    digits[i] = digits[i - 1];
  }
  digits[0] = '0';
  return digit_count;
}
