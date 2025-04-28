#ifndef LIBXR_COLOR_HPP
#define LIBXR_COLOR_HPP

#include <cstdint>

/**
 * @brief LibXR Color Control Library / LibXR终端颜色控制库
 * @details Provides ANSI escape sequences for setting terminal text styles and colors.
 *          提供基于ANSI转义序列的终端文本样式和颜色设置。
 */
namespace LibXR
{

/**
 * @brief Terminal text format (Format) / 终端文本格式 (Format)
 * @details Defines text styles like NONE, RESET, BOLD, DARK, UNDERLINE, etc.
 *          定义文本样式，如NONE（无格式）、RESET（重置）、BOLD（加粗）、DARK（暗色）、UNDERLINE（下划线）等。
 */
enum class Format : uint8_t
{
  NONE = 0,
  RESET,
  BOLD,
  DARK,
  UNDERLINE,
  BLINK,
  REVERSE,
  CONCEALED,
  CLEAR_LINE,
  COUNT
};

/**
 * @brief Terminal font color (Font) / 终端字体颜色 (Font)
 */
enum class Font : uint8_t
{
  NONE = 0,
  BLACK,
  RED,
  GREEN,
  YELLOW,
  BLUE,
  MAGENTA,
  CYAN,
  WHITE,
  COUNT
};

/**
 * @brief Terminal background color (Background) / 终端背景颜色 (Background)
 */
enum class Background : uint8_t
{
  NONE = 0,
  BLACK,
  RED,
  GREEN,
  YELLOW,
  BLUE,
  MAGENTA,
  CYAN,
  WHITE,
  COUNT
};

/**
 * @brief Terminal bold style (Bold) / 终端粗体样式 (Bold)
 * @details Optional bold styles like yellow bold, red bold, red background bold.
 *          可选粗体样式，包括黄色粗体、红色粗体、红色背景粗体等。
 */
enum class Bold : uint8_t
{
  NONE = 0,
  YELLOW,
  RED,
  ON_RED,
  COUNT
};

/**
 * @brief ANSI escape sequences for text format / ANSI转义序列 - 文本格式
 */
inline constexpr const char* LIBXR_FORMAT_STR[] = {"",        "\033[m",  "\033[1m",
                                                   "\033[2m", "\033[4m", "\033[5m",
                                                   "\033[7m", "\033[8m", "\033[K"};

/**
 * @brief ANSI escape sequences for font color / ANSI转义序列 - 字体颜色
 */
inline constexpr const char* LIBXR_FONT_STR[] = {"",         "\033[30m", "\033[31m",
                                                 "\033[32m", "\033[33m", "\033[34m",
                                                 "\033[35m", "\033[36m", "\033[37m"};

/**
 * @brief ANSI escape sequences for background color / ANSI转义序列 - 背景颜色
 */
inline constexpr const char* LIBXR_BACKGROUND_STR[] = {
    "",         "\033[40m", "\033[41m", "\033[42m", "\033[43m",
    "\033[44m", "\033[45m", "\033[46m", "\033[47m"};

/**
 * @brief ANSI escape sequences for bold styles / ANSI转义序列 - 粗体样式
 */
inline constexpr const char* LIBXR_BOLD_STR[] = {"", "\033[33m\033[1m", "\033[31m\033[1m",
                                                 "\033[1m\033[41m"};

}  // namespace LibXR

#endif  // LIBXR_COLOR_HPP