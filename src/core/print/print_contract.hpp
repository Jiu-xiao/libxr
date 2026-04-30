#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "format_protocol.hpp"
#include "libxr_def.hpp"

namespace LibXR::Print
{

/**
 * @brief 接收编译格式输出的写入端。
 * @brief Output endpoint accepted by compiled format writers.
 */
template <typename Sink>
concept OutputSink = requires(Sink& output, std::string_view text)
{
  { output.Write(text) } -> std::convertible_to<ErrorCode>;
};

/**
 * @brief 可由共享后端读取源码信息的格式前端。
 * @brief Format frontend exposing source metadata to the shared backend.
 */
template <typename Frontend>
concept SourceFrontend = requires
{
  typename Frontend::ErrorType;
  { Frontend::SourceData() } -> std::convertible_to<const char*>;
  { Frontend::SourceSize() } -> std::convertible_to<size_t>;
};

/**
 * @brief 可向指定 visitor 发射格式事件的前端。
 * @brief Frontend that can emit format events into the supplied visitor.
 */
template <typename Frontend, typename Visitor>
concept WalkableFrontend =
    SourceFrontend<Frontend> && requires(Visitor& visitor)
{
  { Frontend::Walk(visitor) } -> std::same_as<typename Frontend::ErrorType>;
};

/**
 * @brief Writer 可执行的编译后格式对象。
 * @brief Compiled format object executable by Writer.
 */
template <typename Format>
concept CompiledFormat = requires
{
  typename std::remove_cvref_t<decltype(Format::Codes())>::value_type;
  typename std::remove_cvref_t<decltype(Format::ArgumentList())>::value_type;
  { Format::Codes().data() } -> std::convertible_to<const uint8_t*>;
  { Format::Codes().size() } -> std::convertible_to<size_t>;
  { Format::ArgumentList().size() } -> std::convertible_to<size_t>;
  { Format::ArgumentOrder().size() } -> std::convertible_to<size_t>;
  { Format::Profile() } -> std::convertible_to<FormatProfile>;
} && std::same_as<typename std::remove_cvref_t<decltype(Format::Codes())>::value_type,
                  uint8_t> &&
    std::same_as<
        typename std::remove_cvref_t<decltype(Format::ArgumentList())>::value_type,
        FormatArgumentInfo>;

}  // namespace LibXR::Print
