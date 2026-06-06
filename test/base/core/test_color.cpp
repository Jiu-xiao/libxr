/**
 * @file test_color.cpp
 * @brief `libxr_color.hpp` ANSI 颜色/样式/控制表测试。 ANSI color/style/control lookup-table tests for `libxr_color.hpp`.
 *
 * 测试项目 / Test items:
 * 1. 枚举与查表数组的数量对应关系。 Enum/table cardinality: verify every exported enum `COUNT` matches its corresponding string table length.
 * 2. 关键 ANSI 转义序列的取值。 Representative escape values: verify key style, terminal-control, foreground, background and preset entries map to the expected ANSI sequences.
 *
 * 测试原理 / Test principles:
 * 1. 先检查编译期表长，避免枚举和字符串表静默漂移。 Check the compile-time table sizes first, because a silent enum/table drift would invalidate every runtime user of these lookup tables.
 * 2. 再检查代表性运行时字符串，确认公开文本契约而不只是数值布局。 Check representative strings at runtime to confirm the published textual contract, not just the numeric enum layout.
 */
#include <cstddef>
#include <iterator>
#include <string_view>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

/**
 * @brief 测试入口函数 `test_color`。 Test entry function `test_color`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_color()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  static_assert(std::size(LibXR::LIBXR_TEXT_STYLE_STR) ==
                static_cast<size_t>(LibXR::TextStyle::COUNT));
  static_assert(std::size(LibXR::LIBXR_TERMINAL_CONTROL_STR) ==
                static_cast<size_t>(LibXR::TerminalControl::COUNT));
  static_assert(std::size(LibXR::LIBXR_FOREGROUND_STR) ==
                static_cast<size_t>(LibXR::Foreground::COUNT));
  static_assert(std::size(LibXR::LIBXR_BACKGROUND_STR) ==
                static_cast<size_t>(LibXR::Background::COUNT));
  static_assert(std::size(LibXR::LIBXR_PRESET_STR) ==
                static_cast<size_t>(LibXR::Preset::COUNT));

  ASSERT(std::string_view(
             LibXR::LIBXR_TEXT_STYLE_STR[static_cast<size_t>(LibXR::TextStyle::NONE)]) ==
         "");
  ASSERT(std::string_view(
             LibXR::LIBXR_TEXT_STYLE_STR[static_cast<size_t>(LibXR::TextStyle::BOLD)]) ==
         "\033[1m");
  ASSERT(std::string_view(LibXR::LIBXR_TEXT_STYLE_STR[static_cast<size_t>(
             LibXR::TextStyle::UNDERLINE)]) == "\033[4m");

  ASSERT(std::string_view(LibXR::LIBXR_TERMINAL_CONTROL_STR[static_cast<size_t>(
             LibXR::TerminalControl::RESET)]) == "\033[m");
  ASSERT(std::string_view(LibXR::LIBXR_TERMINAL_CONTROL_STR[static_cast<size_t>(
             LibXR::TerminalControl::ERASE_LINE)]) == "\033[K");

  ASSERT(std::string_view(LibXR::LIBXR_FOREGROUND_STR[static_cast<size_t>(
             LibXR::Foreground::GREEN)]) == "\033[32m");
  ASSERT(std::string_view(LibXR::LIBXR_BACKGROUND_STR[static_cast<size_t>(
             LibXR::Background::BLUE)]) == "\033[44m");

  ASSERT(std::string_view(
             LibXR::LIBXR_PRESET_STR[static_cast<size_t>(LibXR::Preset::YELLOW_BOLD)]) ==
         "\033[33m\033[1m");
  ASSERT(std::string_view(
             LibXR::LIBXR_PRESET_STR[static_cast<size_t>(LibXR::Preset::BOLD_ON_RED)]) ==
         "\033[1m\033[41m");
}
