#pragma once

/**
 * @brief 执行器的具体运行期数值写出函数 / Concrete runtime value writers for the executor
 */

/**
 * @brief 为一个有符号整数载荷确定最终可见的符号字符 / Resolve the visible sign character for one signed integer payload
 * @tparam Int 有符号整数类型 / Signed integer type
 * @param value 当前要输出的整数值 / Integer value being emitted
 * @param spec 解码后的字段规格 / Decoded field spec
 * @return 返回 `'-'`、`'+'`、`' '` 或 `'\0'` / Returns `'-'`, `'+'`, `' '`, or `'\0'`
 */
template <OutputSink Sink, FormatProfile Profile>
template <std::signed_integral Int>
char Writer::Executor<Sink, Profile>::ResolveSignChar(Int value, const Spec& spec)
{
  if (value < 0)
  {
    return '-';
  }
  if (spec.ForceSign())
  {
    return '+';
  }
  if (spec.SpaceSign())
  {
    return ' ';
  }
  return '\0';
}

#if LIBXR_PRINT_ENABLE_FLOAT
/**
 * @brief 为一个浮点载荷确定最终可见的符号字符 / Resolve the visible sign character for one float payload
 * @tparam T 浮点类型 / Float type
 * @param value 当前要输出的浮点值 / Float value being emitted
 * @param spec 解码后的字段规格 / Decoded field spec
 * @return 返回 `'-'`、`'+'`、`' '` 或 `'\0'` / Returns `'-'`, `'+'`, `' '`, or `'\0'`
 */
template <OutputSink Sink, FormatProfile Profile>
template <typename T>
char Writer::Executor<Sink, Profile>::ResolveFloatSignChar(T value, const Spec& spec)
{
  if (std::signbit(value))
  {
    return '-';
  }
  if (spec.ForceSign())
  {
    return '+';
  }
  if (spec.SpaceSign())
  {
    return ' ';
  }
  return '\0';
}
#endif

/**
 * @brief 通过共享整数字段路径写出一个有符号整数值 / Write one signed integer value through the shared integer-field path
 * @tparam Int 有符号整数类型 / Signed integer type
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param value 待写出的整数值 / Integer value to write
 * @return 返回共享整数字段路径的写出结果 / Returns the shared integer-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
template <std::signed_integral Int>
ErrorCode Writer::Executor<Sink, Profile>::WriteSigned(const Spec& spec, Int value)
{
  using UInt = std::make_unsigned_t<Int>;
  char digit_buffer[UnsignedDigitCapacity<UInt, 10>()];
  UInt bits = static_cast<UInt>(value);
  UInt magnitude = (value < 0) ? (UInt{0} - bits) : bits;
  size_t digit_count = AppendUnsigned<10>(digit_buffer, magnitude);

  std::string_view digits(digit_buffer, digit_count);
  if (value == 0 && spec.precision == 0)
  {
    digits = {};
  }

  return WriteIntegerField(ResolveSignChar(value, spec), {}, digits, spec);
}

/**
 * @brief 通过共享整数字段路径写出一个无符号整数语义值 / Write one unsigned integer semantic value through the shared integer-field path
 * @tparam Base 整数进制 / Integer radix
 * @tparam UpperCase 十六进制数字是否使用大写字符 / Whether hexadecimal digits should use uppercase characters
 * @tparam InlineAlternateOctal 是否把 `%#o` 的前导 `0` 直接并入数字载荷 / Whether `%#o` should inline its leading `0` into the digit payload
 * @tparam UInt 无符号整数类型 / Unsigned integer type
 * @param prefix 脱离数字载荷输出的前缀 / Prefix emitted outside the digit payload
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param value 待写出的整数值 / Integer value to write
 * @return 返回共享整数字段路径的写出结果 / Returns the shared integer-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
template <uint8_t Base, bool UpperCase, bool InlineAlternateOctal,
          std::unsigned_integral UInt>
ErrorCode Writer::Executor<Sink, Profile>::WriteUnsignedDigits(std::string_view prefix,
                                                               const Spec& spec,
                                                               UInt value)
{
  char digit_buffer[UnsignedDigitCapacity<UInt, Base>() +
                    (InlineAlternateOctal ? 1U : 0U)];
  size_t digit_count = AppendUnsigned<Base, UpperCase>(digit_buffer, value);

  if constexpr (InlineAlternateOctal)
  {
    digit_count = ApplyAlternateOctal(digit_buffer, digit_count, spec, value);
  }
  else if (value == 0 && spec.precision == 0)
  {
    digit_count = 0;
  }

  return WriteIntegerField('\0', prefix, std::string_view(digit_buffer, digit_count),
                           spec);
}

/**
 * @brief 通过共享整数字段路径写出一个无符号整数语义值 / Write one unsigned integer semantic value through the shared integer-field path
 * @tparam Type 运行期整数语义类型 / Runtime integer semantic type
 * @tparam UInt 无符号整数类型 / Unsigned integer type
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param value 待写出的整数值 / Integer value to write
 * @return 返回共享整数字段路径的写出结果 / Returns the shared integer-field write result
 *
 * This bridge does not format digits itself; it only maps one runtime integer
 * semantic type onto the shared compile-time radix/case helper above.
 * 这个桥接函数本身不直接格式化数字；它只负责把运行期整数语义类型映射到上面的
 * 编译期进制/大小写共享辅助路径。
 */
template <OutputSink Sink, FormatProfile Profile>
template <FormatType Type, std::unsigned_integral UInt>
ErrorCode Writer::Executor<Sink, Profile>::WriteUnsigned(const Spec& spec, UInt value)
{
  auto prefix = IntegerPrefix(Type, spec, value);

  if constexpr (Type == FormatType::Unsigned32 || Type == FormatType::Unsigned64)
  {
    return WriteUnsignedDigits<10>(prefix, spec, value);
  }
  if constexpr (Type == FormatType::Binary32 || Type == FormatType::Binary64)
  {
    return WriteUnsignedDigits<2>(prefix, spec, value);
  }
  if constexpr (Type == FormatType::Octal32 || Type == FormatType::Octal64)
  {
    return WriteUnsignedDigits<8, false, true>(prefix, spec, value);
  }
  if constexpr (Type == FormatType::HexLower32 || Type == FormatType::HexLower64)
  {
    return WriteUnsignedDigits<16>(prefix, spec, value);
  }
  if constexpr (Type == FormatType::HexUpper32 || Type == FormatType::HexUpper64)
  {
    return WriteUnsignedDigits<16, true>(prefix, spec, value);
  }

  return ErrorCode::ARG_ERR;
}

/**
 * @brief 按规范指针字段策略写出一个指针值 / Write one pointer value using the canonical pointer field policy
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param value 以 `uintptr_t` 编码的指针值 / Pointer value encoded as `uintptr_t`
 * @return 返回指针字段写出结果 / Returns the pointer-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::WritePointer(const Spec& spec,
                                                        uintptr_t value)
{
  char digit_buffer[UnsignedDigitCapacity<uintptr_t, 16>()];
  size_t digit_count = AppendUnsigned<16>(digit_buffer, value);
  Spec actual = spec;

  if (!actual.HasPrecision() || actual.precision == 0)
  {
    actual.precision = 1;
  }

  return WriteIntegerField('\0', "0x", std::string_view(digit_buffer, digit_count),
                           actual);
}

/**
 * @brief 写出一个字符字段值 / Write one character field value
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param ch 待写出的字符值 / Character value to write
 * @return 返回字符字段写出结果 / Returns the character-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::WriteCharacter(const Spec& spec, char ch)
{
  return WriteTextField(std::string_view(&ch, 1), spec);
}

/**
 * @brief 写出一个字符串字段值，并在需要时应用精度截断 / Write one string field value, including precision truncation when present
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param text 待写出的字符串载荷 / String payload to write
 * @return 返回字符串字段写出结果 / Returns the string-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::WriteString(const Spec& spec,
                                                       std::string_view text)
{
  auto view = text;
  if (spec.HasPrecision() && spec.precision < view.size())
  {
    view = view.substr(0, spec.precision);
  }

  return WriteTextField(view, spec);
}

/**
 * @brief 通过共享浮点文本后端写出一个浮点语义值 / Write one float semantic value through the shared float-text backend
 * @tparam T 浮点类型 / Float type
 * @param type 运行期浮点语义类型 / Runtime float semantic type
 * @param spec 解码后的字段规格 / Decoded field spec
 * @param value 待写出的浮点值 / Float value to write
 * @return 返回浮点字段写出结果 / Returns the float-field write result
 */
#if LIBXR_PRINT_ENABLE_FLOAT
template <OutputSink Sink, FormatProfile Profile>
template <typename T>
ErrorCode Writer::Executor<Sink, Profile>::WriteFloat(FormatType type,
                                                      const Spec& spec, T value)
{
  if (!UsesFloatTextBackend(type))
  {
    return ErrorCode::ARG_ERR;
  }

  Spec actual = spec;
  if (!std::isfinite(value))
  {
    actual.flags &= static_cast<uint8_t>(~static_cast<uint8_t>(FormatFlag::ZeroPad));
  }

  char sign_char = ResolveFloatSignChar(value, actual);
  if (std::isnan(value))
  {
    sign_char = '\0';
  }
  T magnitude = std::signbit(value) ? -static_cast<T>(value) : static_cast<T>(value);
  uint8_t precision = actual.HasPrecision() ? actual.precision : DefaultFloatPrecision();
  if (type == FormatType::FloatFixed || type == FormatType::DoubleFixed ||
      type == FormatType::LongDoubleFixed)
  {
    if (ExceedsFixedIntegerDigits(magnitude, precision))
    {
      return ErrorCode::OUT_OF_RANGE;
    }
  }
  else if (type == FormatType::FloatGeneral || type == FormatType::DoubleGeneral ||
           type == FormatType::LongDoubleGeneral)
  {
    uint8_t significant = precision == 0 ? 1 : precision;
    int exponent =
        RoundScientificDigits(magnitude, static_cast<uint8_t>(significant - 1)).exponent;
    if (!(exponent < -4 || exponent >= significant))
    {
      int fractional_precision = static_cast<int>(significant) - (exponent + 1);
      if (fractional_precision < 0)
      {
        fractional_precision = 0;
      }
      if (ExceedsFixedIntegerDigits(magnitude,
                                    static_cast<uint8_t>(fractional_precision)))
      {
        return ErrorCode::OUT_OF_RANGE;
      }
    }
  }
  char output_buffer[float_buffer_capacity];
  size_t output_size = 0;
  if (!FormatFloatText(type, actual, magnitude, output_buffer, output_size))
  {
    return ErrorCode::NO_BUFF;
  }

  return WriteFloatField(sign_char, std::string_view(output_buffer, output_size), actual);
}
#endif

/**
 * @brief 写出一个原始 uint32 十进制快路径字段 / Writes one raw uint32 decimal fast-path field.
 * @param value Unsigned value to write. / 待写出的无符号值。
 * @return Returns the sink write result. / 返回 sink 写出结果。
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::WriteU32Dec(uint32_t value)
{
  char digit_buffer[UnsignedDigitCapacity<uint32_t, 10>()];
  size_t digit_count = AppendUnsigned<10>(digit_buffer, value);
  return WriteRaw(std::string_view(digit_buffer, digit_count));
}

/**
 * @brief 写出一个原始 int32 十进制快路径字段 / Writes one raw int32 decimal fast-path field.
 * @param value Signed value to write. / 待写出的有符号值。
 * @return Returns the sink write result. / 返回 sink 写出结果。
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::WriteI32Dec(int32_t value)
{
  using UInt = std::make_unsigned_t<int32_t>;
  char digit_buffer[UnsignedDigitCapacity<UInt, 10>()];
  UInt bits = static_cast<UInt>(value);
  UInt magnitude = (value < 0) ? (UInt{0} - bits) : bits;
  size_t digit_count = AppendUnsigned<10>(digit_buffer, magnitude);

  if (value < 0)
  {
    if (auto ec = WriteRaw("-"); ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  return WriteRaw(std::string_view(digit_buffer, digit_count));
}

/**
 * @brief 写出一个原始 uint32 多进制快路径字段 / Writes one raw uint32 base-specific fast-path field.
 * @tparam Base Integer radix. / 整数进制。
 * @tparam UpperCase Whether hexadecimal digits are uppercase. / 十六进制数字是否大写。
 * @param value Unsigned value to write. / 待写出的无符号值。
 * @return Returns the sink write result. / 返回 sink 写出结果。
 */
template <OutputSink Sink, FormatProfile Profile>
template <uint8_t Base, bool UpperCase>
ErrorCode Writer::Executor<Sink, Profile>::WriteU32Base(uint32_t value)
{
  char digit_buffer[UnsignedDigitCapacity<uint32_t, Base>()];
  size_t digit_count = AppendUnsigned<Base, UpperCase>(digit_buffer, value);
  return WriteRaw(std::string_view(digit_buffer, digit_count));
}

/**
 * @brief 写出一个带零填充宽度的 `uint32_t` 十进制快路径字段 / Write one zero-padded `uint32_t` decimal fast-path field
 * @param width 目标零填充宽度 / Target zero-padded width
 * @param value 待写出的无符号值 / Unsigned value to write
 * @return 返回该快路径的写出结果 / Returns the fast-path write result
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::WriteU32ZeroPadWidth(uint8_t width,
                                                                uint32_t value)
{
  char digit_buffer[UnsignedDigitCapacity<uint32_t, 10>()];
  size_t digit_count = AppendUnsigned<10>(digit_buffer, value);
  size_t zeros = FieldPadding(width, digit_count);
  if (auto ec = WritePadding('0', zeros); ec != ErrorCode::OK)
  {
    return ec;
  }
  return WriteRaw(std::string_view(digit_buffer, digit_count));
}

/**
 * @brief 写出一个原始字符串快路径字段 / Writes one raw string fast-path field.
 * @param text String payload to write. / 待写出的字符串载荷。
 * @return Returns the sink write result. / 返回 sink 写出结果。
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::WriteStringRaw(std::string_view text)
{
  return WriteRaw(text);
}

/**
 * @brief 写出一个原始字符快路径字段 / Writes one raw character fast-path field.
 * @param ch Character payload to write. / 待写出的字符载荷。
 * @return Returns the sink write result. / 返回 sink 写出结果。
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::WriteCharacterRaw(char ch)
{
  return WriteRaw(std::string_view(&ch, 1));
}

/**
 * @brief 写出一个 float32 定点精度快路径字段 / Write one float32 fixed-precision fast-path field
 * @param precision 显式定点精度 / Explicit fixed precision
 * @param value 待写出的浮点值 / Float value to write
 * @return 返回该快路径的写出结果 / Returns the fast-path write result
 */
#if LIBXR_PRINT_ENABLE_FLOAT
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::WriteF32FixedPrec(uint8_t precision,
                                                             float value)
{
  char sign_char = std::signbit(value) ? '-' : '\0';
  float magnitude = std::signbit(value) ? -value : value;
  if (ExceedsFixedIntegerDigits(magnitude, precision))
  {
    return ErrorCode::OUT_OF_RANGE;
  }
  char output_buffer[float_buffer_capacity];
  size_t output_size = 0;
  if (!FormatF32FixedPrecText(magnitude, precision, output_buffer, output_size))
  {
    return ErrorCode::NO_BUFF;
  }

  if (sign_char != '\0')
  {
    if (auto ec = WriteRaw(std::string_view(&sign_char, 1)); ec != ErrorCode::OK)
    {
      return ec;
    }
  }

  return WriteRaw(std::string_view(output_buffer, output_size));
}

/**
 * @brief 通过通用浮点写出路径写出一个 double 定点精度字段 / Write one double fixed-precision field through the generic float writer
 * @param precision 显式定点精度 / Explicit fixed precision
 * @param value 待写出的 double 值 / Double value to write
 * @return 返回通用浮点写出路径的结果 / Returns the generic float-writer result
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::WriteF64FixedPrec(uint8_t precision,
                                                             double value)
{
  return WriteFloat(FormatType::DoubleFixed, Spec{.precision = precision}, value);
}
#endif
