#pragma once

/**
 * @brief 运行期 writer 后端共享的整数文本辅助函数 / Shared integer-text helpers for the runtime writer backend
 */

template <std::unsigned_integral UInt, uint8_t Base>
consteval size_t Writer::UnsignedDigitCapacity()
{
  static_assert(Base == 2 || Base == 8 || Base == 10 || Base == 16,
                "LibXR::Print::Writer only supports base 2, 8, 10, and 16");

  UInt value = std::numeric_limits<UInt>::max();
  size_t digits = 1;
  while (value >= static_cast<UInt>(Base))
  {
    value /= static_cast<UInt>(Base);
    ++digits;
  }
  return digits;
}

/**
 * @brief 将一个无符号整数写入调用方提供的定长数字缓冲区 / Append one unsigned integer into a caller-provided fixed-size digit buffer
 * @tparam Base 整数进制 / Integer radix
 * @tparam UpperCase 十六进制数字是否使用大写字母 / Whether hexadecimal digits should use uppercase letters
 * @tparam N 目标缓冲区长度 / Destination buffer size
 * @tparam UInt 无符号整数类型 / Unsigned integer type
 * @param out 目标数字缓冲区 / Destination digit buffer
 * @param value 待编码的无符号值 / Unsigned value to encode
 * @return 返回输出的数字个数 / Returns the emitted digit count
 */
template <uint8_t Base, bool UpperCase, size_t N, std::unsigned_integral UInt>
size_t Writer::AppendUnsigned(char (&out)[N], UInt value)
{
  constexpr char lower_digits[] = "0123456789abcdef";
  constexpr char upper_digits[] = "0123456789ABCDEF";
  static_assert(N >= UnsignedDigitCapacity<UInt, Base>(),
                "LibXR::Print::Writer digit buffer is too small for the selected integer type");

  const char* digits = UpperCase ? upper_digits : lower_digits;
  char reverse[UnsignedDigitCapacity<UInt, Base>()];
  size_t count = 0;

  if (value == 0)
  {
    out[0] = '0';
    return 1;
  }

  while (value != 0)
  {
    reverse[count++] = digits[static_cast<size_t>(value % static_cast<UInt>(Base))];
    value /= static_cast<UInt>(Base);
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
template <size_t N>
inline size_t Writer::AppendSmallUnsigned(char (&out)[N], uint8_t value)
{
  return AppendUnsigned<10>(out, value);
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

  for (size_t i = digit_count; i > 0; --i)
  {
    digits[i] = digits[i - 1];
  }
  digits[0] = '0';
  digit_count++;
  return digit_count;
}
