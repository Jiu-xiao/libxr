#pragma once

/**
 * @brief 运行期执行器中 `GenericField` 的分发桥接函数 / `GenericField` dispatch bridges
 * for the runtime executor
 */

/**
 * @brief 读取一个有符号载荷并转发给具体有符号写出路径 / Read one signed payload and
 * forward it to the concrete signed writer
 * @tparam Int 有符号打包存储类型 / Signed packed-storage type
 * @return 返回具体有符号字段写出结果 / Returns the concrete signed-field write result
 */
template <OutputSink Sink>
template <std::signed_integral Int>
ErrorCode Writer::Executor<Sink>::DispatchSignedField()
{
  return WriteSigned(codes_.ReadSpec(), args_.Read<Int>());
}

/**
 * @brief 读取一个无符号载荷并转发给选定的整数语义写出路径 / Read one unsigned payload and
 * forward it to the selected integer semantic writer
 * @tparam Type 运行期整数语义类型 / Runtime integer semantic type
 * @tparam UInt 无符号打包存储类型 / Unsigned packed-storage type
 * @return 返回选定整数语义路径的写出结果 / Returns the selected integer-field write
 * result
 */
template <OutputSink Sink>
template <FormatType Type, std::unsigned_integral UInt>
ErrorCode Writer::Executor<Sink>::DispatchUnsignedField()
{
  return DispatchUnsigned<Type>(codes_.ReadSpec(), args_.Read<UInt>());
}

#if LIBXR_PRINT_ENABLE_FLOAT
/**
 * @brief 读取一个浮点载荷并转发给选定的浮点语义写出路径 / Read one float payload and
 * forward it to the selected float semantic writer
 * @tparam Type 运行期浮点语义类型 / Runtime float semantic type
 * @tparam Float 打包浮点存储类型 / Packed float storage type
 * @return 返回选定浮点语义路径的写出结果 / Returns the selected float-field write result
 */
template <OutputSink Sink>
template <FormatType Type, typename Float>
ErrorCode Writer::Executor<Sink>::DispatchFloatField()
{
  return WriteFloat(Type, codes_.ReadSpec(), args_.Read<Float>());
}
#endif

/**
 * @brief 读取一个指针载荷并走指针字段写出路径 / Read one pointer payload and write it
 * through the pointer field path
 * @return 返回指针字段写出结果 / Returns the pointer-field write result
 */
template <OutputSink Sink>
ErrorCode Writer::Executor<Sink>::DispatchPointerField()
{
  return WritePointer(codes_.ReadSpec(), args_.Read<uintptr_t>());
}

/**
 * @brief 读取一个字符载荷并走字符字段写出路径 / Read one character payload and write it
 * through the character field path
 * @return 返回字符字段写出结果 / Returns the character-field write result
 */
template <OutputSink Sink>
ErrorCode Writer::Executor<Sink>::DispatchCharacterField()
{
  return WriteCharacter(codes_.ReadSpec(), args_.Read<char>());
}

/**
 * @brief 读取一个字符串载荷并走字符串字段写出路径 / Read one string payload and write it
 * through the string field path
 * @return 返回字符串字段写出结果 / Returns the string-field write result
 */
template <OutputSink Sink>
ErrorCode Writer::Executor<Sink>::DispatchStringField()
{
  return WriteString(codes_.ReadSpec(), args_.Read<std::string_view>());
}

/**
 * @brief 将一个 `GenericField` 语义类型分发到具体宽回退写出路径 / Dispatch one
 * `GenericField` semantic type to the concrete wide fallback writer
 * @param type 当前 `GenericField` 携带的运行期语义类型 / Runtime semantic type carried by
 * this `GenericField`
 * @return 返回具体宽回退路径的写出结果 / Returns the concrete wide-path write result
 */
template <OutputSink Sink>
template <FormatProfile Profile>
ErrorCode Writer::Executor<Sink>::DispatchGenericField(FormatType type)
{
  switch (type)
  {
    case FormatType::Signed32:
      if constexpr (HasProfile(Profile, FormatProfile::GenericSigned32) &&
                    Config::enable_integer)
      {
        return DispatchSignedField<int32_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::Signed64:
      if constexpr (HasProfile(Profile, FormatProfile::GenericSigned64) &&
                    Config::enable_integer && Config::enable_integer_64bit)
      {
        return DispatchSignedField<int64_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::Unsigned32:
      if constexpr (HasProfile(Profile, FormatProfile::GenericUnsigned32) &&
                    Config::enable_integer)
      {
        return DispatchUnsignedField<FormatType::Unsigned32, uint32_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::Unsigned64:
      if constexpr (HasProfile(Profile, FormatProfile::GenericUnsigned64) &&
                    Config::enable_integer && Config::enable_integer_64bit)
      {
        return DispatchUnsignedField<FormatType::Unsigned64, uint64_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::Binary32:
      if constexpr (HasProfile(Profile, FormatProfile::GenericBinary32) &&
                    Config::enable_integer && Config::enable_integer_base8_16)
      {
        return DispatchUnsignedField<FormatType::Binary32, uint32_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::Binary64:
      if constexpr (HasProfile(Profile, FormatProfile::GenericBinary64) &&
                    Config::enable_integer && Config::enable_integer_base8_16 &&
                    Config::enable_integer_64bit)
      {
        return DispatchUnsignedField<FormatType::Binary64, uint64_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::Octal32:
      if constexpr (HasProfile(Profile, FormatProfile::GenericOctal32) &&
                    Config::enable_integer && Config::enable_integer_base8_16)
      {
        return DispatchUnsignedField<FormatType::Octal32, uint32_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::Octal64:
      if constexpr (HasProfile(Profile, FormatProfile::GenericOctal64) &&
                    Config::enable_integer && Config::enable_integer_base8_16 &&
                    Config::enable_integer_64bit)
      {
        return DispatchUnsignedField<FormatType::Octal64, uint64_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::HexLower32:
      if constexpr (HasProfile(Profile, FormatProfile::GenericHexLower32) &&
                    Config::enable_integer && Config::enable_integer_base8_16)
      {
        return DispatchUnsignedField<FormatType::HexLower32, uint32_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::HexLower64:
      if constexpr (HasProfile(Profile, FormatProfile::GenericHexLower64) &&
                    Config::enable_integer && Config::enable_integer_base8_16 &&
                    Config::enable_integer_64bit)
      {
        return DispatchUnsignedField<FormatType::HexLower64, uint64_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::HexUpper32:
      if constexpr (HasProfile(Profile, FormatProfile::GenericHexUpper32) &&
                    Config::enable_integer && Config::enable_integer_base8_16)
      {
        return DispatchUnsignedField<FormatType::HexUpper32, uint32_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::HexUpper64:
      if constexpr (HasProfile(Profile, FormatProfile::GenericHexUpper64) &&
                    Config::enable_integer && Config::enable_integer_base8_16 &&
                    Config::enable_integer_64bit)
      {
        return DispatchUnsignedField<FormatType::HexUpper64, uint64_t>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::Pointer:
      if constexpr (HasProfile(Profile, FormatProfile::GenericPointer) &&
                    Config::enable_pointer)
      {
        return DispatchPointerField();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::Character:
      if constexpr (HasProfile(Profile, FormatProfile::GenericCharacter) &&
                    Config::enable_text)
      {
        return DispatchCharacterField();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::String:
      if constexpr (HasProfile(Profile, FormatProfile::GenericString) &&
                    Config::enable_text)
      {
        return DispatchStringField();
      }
      return ErrorCode::STATE_ERR;
#if LIBXR_PRINT_ENABLE_FLOAT
    case FormatType::FloatFixed:
      if constexpr (HasProfile(Profile, FormatProfile::GenericFloatFixed) &&
                    FloatEnabled(FormatType::FloatFixed))
      {
        return DispatchFloatField<FormatType::FloatFixed, float>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::DoubleFixed:
      if constexpr (HasProfile(Profile, FormatProfile::GenericDoubleFixed) &&
                    FloatEnabled(FormatType::DoubleFixed))
      {
        return DispatchFloatField<FormatType::DoubleFixed, double>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::FloatScientific:
      if constexpr (HasProfile(Profile, FormatProfile::GenericFloatScientific) &&
                    FloatEnabled(FormatType::FloatScientific))
      {
        return DispatchFloatField<FormatType::FloatScientific, float>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::DoubleScientific:
      if constexpr (HasProfile(Profile, FormatProfile::GenericDoubleScientific) &&
                    FloatEnabled(FormatType::DoubleScientific))
      {
        return DispatchFloatField<FormatType::DoubleScientific, double>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::FloatGeneral:
      if constexpr (HasProfile(Profile, FormatProfile::GenericFloatGeneral) &&
                    FloatEnabled(FormatType::FloatGeneral))
      {
        return DispatchFloatField<FormatType::FloatGeneral, float>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::DoubleGeneral:
      if constexpr (HasProfile(Profile, FormatProfile::GenericDoubleGeneral) &&
                    FloatEnabled(FormatType::DoubleGeneral))
      {
        return DispatchFloatField<FormatType::DoubleGeneral, double>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::LongDoubleFixed:
      if constexpr (HasProfile(Profile, FormatProfile::GenericLongDoubleFixed) &&
                    FloatEnabled(FormatType::LongDoubleFixed))
      {
        return DispatchFloatField<FormatType::LongDoubleFixed, long double>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::LongDoubleScientific:
      if constexpr (HasProfile(Profile, FormatProfile::GenericLongDoubleScientific) &&
                    FloatEnabled(FormatType::LongDoubleScientific))
      {
        return DispatchFloatField<FormatType::LongDoubleScientific, long double>();
      }
      return ErrorCode::STATE_ERR;
    case FormatType::LongDoubleGeneral:
      if constexpr (HasProfile(Profile, FormatProfile::GenericLongDoubleGeneral) &&
                    FloatEnabled(FormatType::LongDoubleGeneral))
      {
        return DispatchFloatField<FormatType::LongDoubleGeneral, long double>();
      }
      return ErrorCode::STATE_ERR;
#endif
    case FormatType::TextInline:
    case FormatType::TextRef:
    case FormatType::TextSpace:
    case FormatType::End:
    default:
      return ErrorCode::STATE_ERR;
  }
}
