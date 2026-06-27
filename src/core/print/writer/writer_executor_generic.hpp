#pragma once

/**
 * @brief 运行期执行器中 `GenericField` 的分发桥接函数 / `GenericField` dispatch bridges for the runtime executor
 */

/**
 * @brief 读取一个有符号载荷并转发给具体有符号写出路径 / Read one signed payload and forward it to the concrete signed writer
 * @tparam Int 有符号打包存储类型 / Signed packed-storage type
 * @return 返回具体有符号字段写出结果 / Returns the concrete signed-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
template <std::signed_integral Int>
ErrorCode Writer::Executor<Sink, Profile>::DispatchSignedField()
{
  return WriteSigned(codes_.ReadSpec(), args_.Read<Int>());
}

/**
 * @brief 读取一个无符号载荷并转发给选定的整数语义写出路径 / Read one unsigned payload and forward it to the selected integer semantic writer
 * @tparam Type 运行期整数语义类型 / Runtime integer semantic type
 * @tparam UInt 无符号打包存储类型 / Unsigned packed-storage type
 * @return 返回选定整数语义路径的写出结果 / Returns the selected integer-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
template <FormatType Type, std::unsigned_integral UInt>
ErrorCode Writer::Executor<Sink, Profile>::DispatchUnsignedField()
{
  return WriteUnsigned<Type>(codes_.ReadSpec(), args_.Read<UInt>());
}

#if LIBXR_PRINT_ENABLE_FLOAT
/**
 * @brief 读取一个浮点载荷并转发给选定的浮点语义写出路径 / Read one float payload and forward it to the selected float semantic writer
 * @tparam Type 运行期浮点语义类型 / Runtime float semantic type
 * @tparam Float 打包浮点存储类型 / Packed float storage type
 * @return 返回选定浮点语义路径的写出结果 / Returns the selected float-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
template <FormatType Type, typename Float>
ErrorCode Writer::Executor<Sink, Profile>::DispatchFloatField()
{
  return WriteFloat(Type, codes_.ReadSpec(), args_.Read<Float>());
}
#endif

/**
 * @brief 读取一个指针载荷并走指针字段写出路径 / Read one pointer payload and write it through the pointer field path
 * @return 返回指针字段写出结果 / Returns the pointer-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::DispatchPointerField()
{
  return WritePointer(codes_.ReadSpec(), args_.Read<uintptr_t>());
}

/**
 * @brief 读取一个字符载荷并走字符字段写出路径 / Read one character payload and write it through the character field path
 * @return 返回字符字段写出结果 / Returns the character-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::DispatchCharacterField()
{
  return WriteCharacter(codes_.ReadSpec(), args_.Read<char>());
}

/**
 * @brief 读取一个字符串载荷并走字符串字段写出路径 / Read one string payload and write it through the string field path
 * @return 返回字符串字段写出结果 / Returns the string-field write result
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::DispatchStringField()
{
  return WriteString(codes_.ReadSpec(), args_.Read<std::string_view>());
}

/**
 * @brief 将一个 `GenericField` 语义类型分发到具体宽回退写出路径 / Dispatch one `GenericField` semantic type to the concrete wide fallback writer
 * @param type 当前 `GenericField` 携带的运行期语义类型 / Runtime semantic type carried by this `GenericField`
 * @return 返回具体宽回退路径的写出结果 / Returns the concrete wide-path write result
 */
template <OutputSink Sink, FormatProfile Profile>
ErrorCode Writer::Executor<Sink, Profile>::DispatchGenericField(FormatType type)
{
  switch (type)
  {
    case FormatType::Signed32:
      if constexpr (!Config::enable_integer)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchSignedField<int32_t>();
    case FormatType::Signed64:
      if constexpr (!Config::enable_integer || !Config::enable_integer_64bit)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchSignedField<int64_t>();
    case FormatType::Unsigned32:
      if constexpr (!Config::enable_integer)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchUnsignedField<FormatType::Unsigned32, uint32_t>();
    case FormatType::Unsigned64:
      if constexpr (!Config::enable_integer || !Config::enable_integer_64bit)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchUnsignedField<FormatType::Unsigned64, uint64_t>();
    case FormatType::Binary32:
      if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchUnsignedField<FormatType::Binary32, uint32_t>();
    case FormatType::Binary64:
      if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16 ||
                    !Config::enable_integer_64bit)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchUnsignedField<FormatType::Binary64, uint64_t>();
    case FormatType::Octal32:
      if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchUnsignedField<FormatType::Octal32, uint32_t>();
    case FormatType::Octal64:
      if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16 ||
                    !Config::enable_integer_64bit)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchUnsignedField<FormatType::Octal64, uint64_t>();
    case FormatType::HexLower32:
      if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchUnsignedField<FormatType::HexLower32, uint32_t>();
    case FormatType::HexLower64:
      if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16 ||
                    !Config::enable_integer_64bit)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchUnsignedField<FormatType::HexLower64, uint64_t>();
    case FormatType::HexUpper32:
      if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchUnsignedField<FormatType::HexUpper32, uint32_t>();
    case FormatType::HexUpper64:
      if constexpr (!Config::enable_integer || !Config::enable_integer_base8_16 ||
                    !Config::enable_integer_64bit)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchUnsignedField<FormatType::HexUpper64, uint64_t>();
    case FormatType::Pointer:
      if constexpr (!Config::enable_pointer)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchPointerField();
    case FormatType::Character:
      if constexpr (!Config::enable_text)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchCharacterField();
    case FormatType::String:
      if constexpr (!Config::enable_text)
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchStringField();
#if LIBXR_PRINT_ENABLE_FLOAT
    case FormatType::FloatFixed:
      if constexpr (!FloatEnabled(FormatType::FloatFixed))
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchFloatField<FormatType::FloatFixed, float>();
    case FormatType::DoubleFixed:
      if constexpr (!FloatEnabled(FormatType::DoubleFixed))
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchFloatField<FormatType::DoubleFixed, double>();
    case FormatType::FloatScientific:
      if constexpr (!FloatEnabled(FormatType::FloatScientific))
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchFloatField<FormatType::FloatScientific, float>();
    case FormatType::DoubleScientific:
      if constexpr (!FloatEnabled(FormatType::DoubleScientific))
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchFloatField<FormatType::DoubleScientific, double>();
    case FormatType::FloatGeneral:
      if constexpr (!FloatEnabled(FormatType::FloatGeneral))
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchFloatField<FormatType::FloatGeneral, float>();
    case FormatType::DoubleGeneral:
      if constexpr (!FloatEnabled(FormatType::DoubleGeneral))
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchFloatField<FormatType::DoubleGeneral, double>();
    case FormatType::LongDoubleFixed:
      if constexpr (!FloatEnabled(FormatType::LongDoubleFixed))
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchFloatField<FormatType::LongDoubleFixed, long double>();
    case FormatType::LongDoubleScientific:
      if constexpr (!FloatEnabled(FormatType::LongDoubleScientific))
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchFloatField<FormatType::LongDoubleScientific, long double>();
    case FormatType::LongDoubleGeneral:
      if constexpr (!FloatEnabled(FormatType::LongDoubleGeneral))
      {
        return ErrorCode::STATE_ERR;
      }
      return DispatchFloatField<FormatType::LongDoubleGeneral, long double>();
#endif
    case FormatType::TextInline:
    case FormatType::TextRef:
    case FormatType::TextSpace:
    case FormatType::End:
    default:
      return ErrorCode::STATE_ERR;
  }
}
