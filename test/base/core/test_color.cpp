/**
 * @file test_color.cpp
 * @brief ANSI color/style/control lookup-table tests for `libxr_color.hpp`.
 *
 * Test items:
 * 1. Enum/table cardinality: verify every exported enum `COUNT` matches its corresponding string table length.
 * 2. Representative escape values: verify key style, terminal-control, foreground, background and preset entries map to the expected ANSI sequences.
 *
 * Test principle:
 * 1. Check the compile-time table sizes first, because a silent enum/table drift would invalidate every runtime user of these lookup tables.
 * 2. Check representative strings at runtime to confirm the published textual contract, not just the numeric enum layout.
 */
#include <cstddef>
#include <iterator>
#include <string_view>

#include "libxr.hpp"
#include "libxr_def.hpp"
#include "test.hpp"

void test_color()
{
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
