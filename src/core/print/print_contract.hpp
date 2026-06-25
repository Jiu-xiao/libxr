#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "format_protocol.hpp"
#include "../libxr_def.hpp"

namespace LibXR::Print
{

/**
 * @brief 接收编译格式输出的写入端 / Output endpoint accepted by compiled format writers
 * @tparam Sink 候选输出端类型 / Candidate sink type
 *
 * @note 必须提供 `Write(std::string_view)`，并返回可转换为 `ErrorCode` 的结果 /
 *       Must provide `Write(std::string_view)` and return a value convertible to
 *       `ErrorCode`
 */
template <typename Sink>
concept OutputSink = requires(Sink& output, std::string_view text)
{
  { output.Write(text) } -> std::convertible_to<ErrorCode>;
};

/**
 * @brief 可由共享后端读取源码信息的格式前端 / Format frontend exposing source metadata to the shared backend
 * @tparam Frontend 候选前端类型 / Candidate frontend type
 *
 * @note 至少需要暴露 `ErrorType`、`SourceData()` 与 `SourceSize()` /
 *       Must expose at least `ErrorType`, `SourceData()`, and `SourceSize()`
 */
template <typename Frontend>
concept SourceFrontend = requires
{
  typename Frontend::ErrorType;
  { Frontend::SourceData() } -> std::convertible_to<const char*>;
  { Frontend::SourceSize() } -> std::convertible_to<size_t>;
};

/**
 * @brief 可向指定 visitor 发射格式事件的前端 / Frontend that can emit format events into the supplied visitor
 * @tparam Frontend 候选前端类型 / Candidate frontend type
 * @tparam Visitor 候选 visitor 类型 / Candidate visitor type
 *
 * @note 必须支持 `Walk(visitor)`，并返回 `ErrorType` /
 *       Must support `Walk(visitor)` and return `ErrorType`
 */
template <typename Frontend, typename Visitor>
concept WalkableFrontend =
    SourceFrontend<Frontend> && requires(Visitor& visitor)
{
  { Frontend::Walk(visitor) } -> std::same_as<typename Frontend::ErrorType>;
};

/**
 * @brief Writer 可执行的编译后格式对象 / Compiled format object executable by Writer
 * @tparam Format 候选编译格式类型 / Candidate compiled-format type
 *
 * @note 必须提供 `Codes()`、`ArgumentList()`、`ArgumentOrder()` 与 `Profile()`
 *       这四组运行期协议信息 / Must provide `Codes()`, `ArgumentList()`,
 *       `ArgumentOrder()`, and `Profile()` as its runtime protocol surface
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
