#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "format_protocol.hpp"

namespace LibXR::Print
{
/**
 * @brief Shared compile-time backend that lowers frontend text/field events into
 *        one final compiled format.
 * @brief 共享编译期后端，将前端产生的文本/字段事件直接降为最终编译格式。
 *
 * Frontend contract:
 * The frontend must provide the members using ErrorType, static constexpr
 * const char* SourceData(), static constexpr size_t SourceSize(), and
 * static consteval ErrorType Walk(visitor).
 * 前端协议：
 * 前端必须提供如下成员：using ErrorType、static constexpr const char*
 * SourceData()、static constexpr size_t SourceSize()、以及
 * static consteval ErrorType Walk(visitor)。
 *
 * Walk(visitor) must emit source-ordered events through visitor.Text(offset,
 * text_size) and visitor.Field(const FormatField&).
 * Walk(visitor) 必须按源串顺序通过 visitor.Text(offset, text_size) 与
 * visitor.Field(const FormatField&) 发射事件。
 *
 * The shared backend now walks the frontend exactly once. During that walk it
 * accumulates code bytes, text-pool bytes, argument metadata, and packed-argument
 * layout into one scratch builder, then materializes the final tightly-sized
 * result in one local finalize step.
 * 共享后端现在只遍历前端一次：遍历时同步累积码流、文本池、参数元信息与参数打包布局，
 * 然后在本地收尾阶段生成最终精确尺寸结果。
 */
template <typename Frontend>
class FormatCompiler
{
 private:
  static constexpr bool source_contract =
      requires {
        typename Frontend::ErrorType;
        { Frontend::SourceData() } -> std::convertible_to<const char*>;
        { Frontend::SourceSize() } -> std::convertible_to<size_t>;
      };
  static_assert(source_contract,
                "LibXR::Print::FormatCompiler: frontend must expose ErrorType, "
                "SourceData(), and SourceSize()");

  using Error = typename Frontend::ErrorType;

  /**
   * @brief Final tightly-sized compiled format emitted by this backend instance.
   * @brief 当前后端实例发射出的最终精确尺寸编译格式。
   */
  template <size_t BlobBytes, size_t ArgCount>
  struct ResultData
  {
    using CodeList =
        std::array<uint8_t, BlobBytes>;  ///< final runtime byte block / 最终运行期字节块
    using ArgumentListData =
        std::array<FormatArgumentInfo,
                   ArgCount>;  ///< ordered compile-time argument metadata / 按顺序排列的编译期参数元信息

    CodeList codes{};                    ///< code stream first, text pool second / 前半记录流，后半文本池
    ArgumentListData arg_info{};         ///< ordered argument metadata / 按参数顺序排列的参数元信息
    FormatProfile profile = FormatProfile::None;  ///< compile-time executor profile / 编译期执行器配置
    Error compile_error = Error::None;   ///< first compile-time failure, if any / 首个编译期失败原因
  };

  template <size_t BlobBytes, size_t ArgCount>
  using Result = ResultData<BlobBytes, ArgCount>;

  /// Maximum inline literal size before the backend spills to TextRef. / 超过该长度后，字面文本会从内嵌模式切换为 TextRef
  static constexpr size_t inline_text_limit = 2 * sizeof(size_t) - 1;

  // Conservative single-pass scratch bounds derived from the original source:
  // - literal text can cost up to 3 code bytes per source byte (TextInline)
  // - value records cost at most 2 code bytes per source byte (%x -> 4 bytes)
  // - therefore 3 * SourceSize() + 1 safely covers the temporary record stream
  //   plus the final End marker.
  // 基于原始源串给单遍构建器预留的保守上界：
  // - 普通文本最坏可膨胀为每字节 3 个码流字节（TextInline）
  // - 值记录最坏约为每字节 2 个码流字节（如 %x -> 4 字节）
  // - 因此 3 * SourceSize() + 1 足以覆盖临时记录流与最终结束标记。
  static constexpr size_t max_code_bytes = 3 * Frontend::SourceSize() + 1;
  static constexpr size_t max_text_pool_bytes = Frontend::SourceSize();
  static constexpr size_t max_arg_count = Frontend::SourceSize();
  static constexpr uint8_t unspecified_precision =
      std::numeric_limits<uint8_t>::max();

  static consteval void EmitByte(auto& data, size_t& out, uint8_t value)
  {
    data[out++] = value;
  }

  template <typename T>
  static consteval void EmitNative(auto& data, size_t& out, T value)
  {
    auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(value);
    for (auto byte : bytes)
    {
      data[out++] = byte;
    }
  }

  template <typename T>
  [[nodiscard]] static consteval T ReadNative(const auto& data, size_t& pos)
  {
    std::array<uint8_t, sizeof(T)> bytes{};
    for (size_t i = 0; i < sizeof(T); ++i)
    {
      bytes[i] = data[pos++];
    }
    return std::bit_cast<T>(bytes);
  }

  [[nodiscard]] static consteval auto Failed(Error error)
  {
    Result<1, 0> result{};
    result.compile_error = error;
    return result;
  }

  /**
   * @brief Maps one opcode family to the runtime executor profile bit it requires.
   * @brief 将一个操作码族映射到它所需的运行期执行器 profile 位。
   */
  [[nodiscard]] static consteval FormatProfile ProfileForOp(FormatOp op)
  {
    switch (op)
    {
      case FormatOp::U32Dec:
      case FormatOp::U32ZeroPadWidth:
        return FormatProfile::U32;
      case FormatOp::StringRaw:
        return FormatProfile::TextArg;
      case FormatOp::F32FixedPrec:
        return FormatProfile::F32Fixed;
      case FormatOp::F64FixedPrec:
        return FormatProfile::F64Fixed;
      case FormatOp::GenericField:
        return FormatProfile::Generic;
      case FormatOp::TextInline:
      case FormatOp::TextRef:
      case FormatOp::TextSpace:
      case FormatOp::End:
        return FormatProfile::None;
    }

    return FormatProfile::None;
  }

  /**
   * @brief Chooses the narrowest opcode that preserves one normalized field's behavior.
   * @brief 为一个规范化字段选择在行为不变前提下最窄的操作码。
   */
  [[nodiscard]] static consteval FormatOp FastFieldOp(const FormatField& field)
  {
    if (field.type == FormatType::Unsigned32 && field.pack == FormatPackKind::U32 &&
        field.flags == 0 && field.fill == ' ' && field.width == 0 &&
        field.precision == unspecified_precision)
    {
      return FormatOp::U32Dec;
    }

    if (field.type == FormatType::Unsigned32 && field.pack == FormatPackKind::U32 &&
        field.flags == static_cast<uint8_t>(FormatFlag::ZeroPad) &&
        field.fill == ' ' && field.width != 0 &&
        field.precision == unspecified_precision)
    {
      return FormatOp::U32ZeroPadWidth;
    }

    if (field.type == FormatType::String &&
        field.pack == FormatPackKind::StringView && field.flags == 0 &&
        field.fill == ' ' &&
        field.width == 0 && field.precision == unspecified_precision)
    {
      return FormatOp::StringRaw;
    }

    if (field.type == FormatType::FloatFixed && field.pack == FormatPackKind::F32 &&
        field.flags == 0 && field.fill == ' ' && field.width == 0 &&
        field.precision != unspecified_precision)
    {
      return FormatOp::F32FixedPrec;
    }

    if (field.type == FormatType::DoubleFixed && field.pack == FormatPackKind::F64 &&
        field.flags == 0 && field.fill == ' ' && field.width == 0 &&
        field.precision != unspecified_precision)
    {
      return FormatOp::F64FixedPrec;
    }

    return FormatOp::GenericField;
  }

  /**
   * @brief Single-pass scratch builder driven directly by frontend events.
   * @brief 由前端事件直接驱动的单遍临时构建器。
   */
  struct ScratchBuilder
  {
    std::array<uint8_t, max_code_bytes> code_scratch{};            ///< temporary record stream without final End / 临时记录流，不含最终 End
    std::array<uint8_t, max_text_pool_bytes> text_scratch{};       ///< temporary trailing text pool / 临时尾部文本池
    std::array<FormatArgumentInfo, max_arg_count> arg_scratch{};   ///< temporary ordered argument metadata / 临时参数元信息表

    size_t code_bytes = 0;       ///< scratch record-stream bytes, excluding End / 临时记录流字节数，不含 End
    size_t text_pool_bytes = 0;  ///< scratch text-pool bytes / 临时文本池字节数
    size_t arg_count = 0;        ///< consumed runtime arguments / 运行期参数个数
    FormatProfile profile = FormatProfile::None;  ///< required runtime executor profile / 需要的运行期执行器配置
    Error frontend_error = Error::None;   ///< first frontend failure / 首个前端失败原因

    /**
     * @brief Appends one literal-text span into the scratch buffers.
     * @brief 将单个字面文本片段追加到临时缓冲区。
     */
    [[nodiscard]] consteval Error Text(size_t offset, size_t text_size)
    {
      if (text_size == 0)
      {
        return Error::None;
      }

      if (text_size == 1 && Frontend::SourceData()[offset] == ' ')
      {
        EmitByte(code_scratch, code_bytes, static_cast<uint8_t>(FormatOp::TextSpace));
        return Error::None;
      }

      if (text_size <= inline_text_limit)
      {
        EmitByte(code_scratch, code_bytes, static_cast<uint8_t>(FormatOp::TextInline));
        for (size_t i = 0; i < text_size; ++i)
        {
          EmitByte(code_scratch, code_bytes,
                   static_cast<uint8_t>(Frontend::SourceData()[offset + i]));
        }
        EmitByte(code_scratch, code_bytes, 0);
        return Error::None;
      }

      if (text_pool_bytes > std::numeric_limits<uint16_t>::max())
      {
        return Error::TextOffsetOverflow;
      }
      if (text_size > std::numeric_limits<uint16_t>::max())
      {
        return Error::TextSizeOverflow;
      }

      EmitByte(code_scratch, code_bytes, static_cast<uint8_t>(FormatOp::TextRef));
      EmitNative(code_scratch, code_bytes, static_cast<uint16_t>(text_pool_bytes));
      EmitNative(code_scratch, code_bytes, static_cast<uint16_t>(text_size));

      for (size_t i = 0; i < text_size; ++i)
      {
        text_scratch[text_pool_bytes++] =
            static_cast<uint8_t>(Frontend::SourceData()[offset + i]);
      }

      return Error::None;
    }

    /**
     * @brief Appends one normalized value field into the scratch record stream and
     *        argument metadata table.
     * @brief 将单个规范化值字段追加到临时记录流和参数元信息表。
     */
    [[nodiscard]] consteval Error Field(const FormatField& field)
    {
      auto op = FastFieldOp(field);
      EmitByte(code_scratch, code_bytes, static_cast<uint8_t>(op));

      switch (op)
      {
        case FormatOp::U32Dec:
        case FormatOp::StringRaw:
          break;
        case FormatOp::U32ZeroPadWidth:
          EmitByte(code_scratch, code_bytes, field.width);
          break;
        case FormatOp::F32FixedPrec:
        case FormatOp::F64FixedPrec:
          EmitByte(code_scratch, code_bytes, field.precision);
          break;
        case FormatOp::GenericField:
          EmitByte(code_scratch, code_bytes, static_cast<uint8_t>(field.type));
          EmitByte(code_scratch, code_bytes, field.flags);
          EmitByte(code_scratch, code_bytes, static_cast<uint8_t>(field.fill));
          EmitByte(code_scratch, code_bytes, field.width);
          EmitByte(code_scratch, code_bytes, field.precision);
          break;
        case FormatOp::TextInline:
        case FormatOp::TextRef:
        case FormatOp::TextSpace:
        case FormatOp::End:
          break;
      }

      profile |= ProfileForOp(op);

      arg_scratch[arg_count++] = FormatArgumentInfo{
          .pack = field.pack,
          .rule = field.rule,
      };
      return Error::None;
    }

    /// Final record-stream size including the End marker. / 含 End 结束标记在内的最终记录流字节数
    [[nodiscard]] constexpr size_t FinalCodeBytes() const { return code_bytes + 1; }
    /// Final retained byte-block size including the trailing text pool. / 含尾部文本池在内的最终保留字节块大小
    [[nodiscard]] constexpr size_t FinalBlobBytes() const
    {
      return FinalCodeBytes() + text_pool_bytes;
    }

    /**
     * @brief Materializes the exact final compiled format from the scratch buffers.
     * @brief 从临时缓冲区收敛出最终精确尺寸的编译格式。
     *
     * TextRef records store temporary offsets relative to the text scratch pool.
     * Finalization rebases those offsets so they point at the trailing text pool
     * inside the final codes byte block.
     * TextRef 记录在临时阶段保存的是相对文本池起点的偏移；收尾阶段会把它们重定位为
     * 指向最终 codes 字节块尾部文本池的绝对偏移。
     */
    template <size_t CodeBytes, size_t BlobBytes, size_t ArgCount>
    [[nodiscard]] consteval auto Finish() const
    {
      Result<BlobBytes, ArgCount> result{};
      result.profile = profile;
      size_t scratch_in = 0;
      size_t code_out = 0;

      while (scratch_in < code_bytes)
      {
        auto op = static_cast<FormatOp>(code_scratch[scratch_in++]);
        EmitByte(result.codes, code_out, static_cast<uint8_t>(op));

        if (op == FormatOp::TextInline)
        {
          while (true)
          {
            uint8_t byte = code_scratch[scratch_in++];
            EmitByte(result.codes, code_out, byte);
            if (byte == 0)
            {
              break;
            }
          }
          continue;
        }

        if (op == FormatOp::TextRef)
        {
          auto relative_offset = ReadNative<uint16_t>(code_scratch, scratch_in);
          auto text_size = ReadNative<uint16_t>(code_scratch, scratch_in);
          size_t absolute_offset = CodeBytes + relative_offset;
          if (absolute_offset > std::numeric_limits<uint16_t>::max())
          {
            result.compile_error = Error::TextOffsetOverflow;
            return result;
          }
          EmitNative(result.codes, code_out, static_cast<uint16_t>(absolute_offset));
          EmitNative(result.codes, code_out, text_size);
          continue;
        }

        if (op == FormatOp::U32ZeroPadWidth || op == FormatOp::F32FixedPrec ||
            op == FormatOp::F64FixedPrec)
        {
          EmitByte(result.codes, code_out, code_scratch[scratch_in++]);
          continue;
        }

        if (op == FormatOp::GenericField)
        {
          EmitByte(result.codes, code_out, code_scratch[scratch_in++]);
          EmitByte(result.codes, code_out, code_scratch[scratch_in++]);
          EmitByte(result.codes, code_out, code_scratch[scratch_in++]);
          EmitByte(result.codes, code_out, code_scratch[scratch_in++]);
          EmitByte(result.codes, code_out, code_scratch[scratch_in++]);
        }
      }

      EmitByte(result.codes, code_out, static_cast<uint8_t>(FormatOp::End));

      for (size_t i = 0; i < text_pool_bytes; ++i)
      {
        result.codes[CodeBytes + i] = text_scratch[i];
      }
      for (size_t i = 0; i < ArgCount; ++i)
      {
        result.arg_info[i] = arg_scratch[i];
      }

      return result;
    }
  };

  static constexpr bool walk_contract =
      requires(ScratchBuilder& visitor) {
        { Frontend::Walk(visitor) } -> std::same_as<Error>;
      };
  static_assert(walk_contract,
                "LibXR::Print::FormatCompiler: frontend Walk(visitor) must accept "
                "the shared builder and return ErrorType");

 public:
  /**
   * @brief Compiles the frontend into one final compiled format.
   * @brief 将前端编译为一份最终编译格式。
   */
  [[nodiscard]] static consteval auto Compile()
  {
    constexpr auto scratch = []() consteval {
      ScratchBuilder builder{};
      builder.frontend_error = Frontend::Walk(builder);
      return builder;
    }();

    if constexpr (scratch.frontend_error != Error::None)
    {
      return Failed(scratch.frontend_error);
    }
    else
    {
      return scratch.template Finish<scratch.FinalCodeBytes(),
                                     scratch.FinalBlobBytes(), scratch.arg_count>();
    }
  }
};
}  // namespace LibXR::Print
