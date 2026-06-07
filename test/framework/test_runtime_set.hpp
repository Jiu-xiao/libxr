/**
 * @file test_runtime_set.hpp
 * @brief 固定测试运行集合定义与解析 helper。 Fixed test runtime-set definitions and parsing helpers.
 * @details 作用：
 *          1. 只定义三类允许的测试运行集合：`bare_metal`、`rtos`、`full_os`。
 *          2. 提供环境变量 `XR_TEST_SET` 的解析与校验。
 *          Purpose:
 *          1. Define only the three allowed runtime sets: `bare_metal`, `rtos`, and `full_os`.
 *          2. Provide parsing and validation for the `XR_TEST_SET` environment variable.
 */
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "libxr_def.hpp"

enum class TestRuntimeSet
{
  BARE_METAL = 0,
  RTOS = 1,
  FULL_OS = 2,
};

/**
 * @brief 将测试框架错误码转换为进程退出码。 Convert a framework error code into a process exit code.
 * @param code 测试框架要返回的错误码。 Framework error code to expose to the process.
 * @return `OK` 映射为 `0`，其余错误码映射为对应正数，避免进程退出码被 8 位截断后失真。 `OK` maps to `0`, and every other error code maps to its positive magnitude so the process exit code is not distorted by 8-bit truncation.
 */
inline int ErrorCodeToExitStatus(LibXR::ErrorCode code)
{
  const int raw = static_cast<int>(code);
  return (raw <= 0) ? -raw : raw;
}

/**
 * @brief 判断错误码是否表示成功。 Check whether an error code represents success.
 * @param code 要判断的错误码。 Error code to examine.
 * @return `true` 表示成功；`false` 表示失败。 `true` on success; otherwise `false`.
 */
inline bool IsOk(LibXR::ErrorCode code)
{
  return code == LibXR::ErrorCode::OK;
}

/**
 * @brief 获取运行集合名字。 Get the textual name of a runtime set.
 * @param set 固定运行集合枚举。 Fixed runtime-set enumerator.
 * @return 对应的环境变量字符串。 Matching environment-variable string.
 */
inline const char* RuntimeSetName(TestRuntimeSet set)
{
  switch (set)
  {
    case TestRuntimeSet::BARE_METAL:
      return "bare_metal";
    case TestRuntimeSet::RTOS:
      return "rtos";
    case TestRuntimeSet::FULL_OS:
      return "full_os";
  }
  return "unknown";
}

/**
 * @brief 解析运行集合字符串。 Parse a runtime-set selector string.
 * @param text 输入字符串，通常来自 `XR_TEST_SET`。 Input string, usually from `XR_TEST_SET`.
 * @param set 输出运行集合。 Parsed runtime-set output.
 * @param err 错误输出流。 Error output stream.
 * @return `OK` 表示解析成功；`ARG_ERR` 表示字符串非法。 `OK` on success; `ARG_ERR` if the string is invalid.
 */
inline LibXR::ErrorCode ParseRuntimeSet(const char* text, TestRuntimeSet& set,
                                        FILE* err = stderr)
{
  if (text == nullptr || text[0] == '\0')
  {
    set = TestRuntimeSet::FULL_OS;
    return LibXR::ErrorCode::OK;
  }
  if (std::strcmp(text, "bare_metal") == 0)
  {
    set = TestRuntimeSet::BARE_METAL;
    return LibXR::ErrorCode::OK;
  }
  if (std::strcmp(text, "rtos") == 0)
  {
    set = TestRuntimeSet::RTOS;
    return LibXR::ErrorCode::OK;
  }
  if (std::strcmp(text, "full_os") == 0)
  {
    set = TestRuntimeSet::FULL_OS;
    return LibXR::ErrorCode::OK;
  }
  std::fprintf(err, "unknown XR_TEST_SET: %s\n", text);
  return LibXR::ErrorCode::ARG_ERR;
}

/**
 * @brief 从环境变量读取运行集合。 Load the runtime set from the process environment.
 * @param set 输出运行集合。 Parsed runtime-set output.
 * @param err 错误输出流。 Error output stream.
 * @return `OK` 表示读取成功；否则返回解析错误码。 `OK` on success; otherwise the parsing error code.
 */
inline LibXR::ErrorCode LoadRuntimeSetFromEnv(TestRuntimeSet& set, FILE* err = stderr)
{
  return ParseRuntimeSet(std::getenv("XR_TEST_SET"), set, err);
}

/**
 * @brief 检查二进制是否运行在允许的集合下。 Check whether a binary runs under the required runtime set.
 * @param actual 当前进程解析出的运行集合。 Runtime set parsed from the current process.
 * @param required 该二进制要求的固定集合。 Required fixed runtime set for the binary.
 * @param binary_name 报错时打印的二进制名字。 Binary name used in diagnostics.
 * @return `OK` 表示匹配；`NOT_SUPPORT` 表示该集合不支持该二进制。 `OK` if matched; `NOT_SUPPORT` if the runtime set is not supported by the binary.
 */
inline LibXR::ErrorCode RequireRuntimeSet(TestRuntimeSet actual,
                                          TestRuntimeSet required,
                                          const char* binary_name)
{
  if (actual != required)
  {
    std::fprintf(stderr, "%s requires XR_TEST_SET=%s, got %s\n", binary_name,
                 RuntimeSetName(required), RuntimeSetName(actual));
    return LibXR::ErrorCode::NOT_SUPPORT;
  }
  return LibXR::ErrorCode::OK;
}
