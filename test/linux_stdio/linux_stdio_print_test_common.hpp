/**
 * @file linux_stdio_print_test_common.hpp
 * @brief Linux STDIO `print` 适配测试共用 helper。 Shared helpers for Linux STDIO
 * `print` adapter tests.
 *
 * 作用 / Purpose:
 * 1. 用 `StdioWriteScope` 临时绑定并恢复 `LibXR::STDIO` 的全局写端。
 *    Temporarily bind and restore the global `LibXR::STDIO` writer.
 * 2. 提供失败退出 helper，让测试主体只保留适配层场景。
 *    Provide the failure-exit helper so test bodies stay focused on adapter scenarios.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "libxr.hpp"
#include "test.hpp"

namespace LibXRLinuxStdioPrintTest
{

struct StdioWriteScope
{
  // 构造时替换 STDIO 全局写端，析构时恢复为空，避免影响后续测试。
  // Replace the STDIO global writer during this scope and clear it on destruction.
  explicit StdioWriteScope(LibXR::WritePort& write, LibXR::Mutex& mutex,
                           LibXR::WritePort::Stream* stream = nullptr)
  {
    LibXR::STDIO::write_ = &write;
    LibXR::STDIO::write_mutex_ = &mutex;
    LibXR::STDIO::write_stream_ = stream;
  }

  StdioWriteScope(const StdioWriteScope&) = delete;
  StdioWriteScope& operator=(const StdioWriteScope&) = delete;

  ~StdioWriteScope()
  {
    LibXR::STDIO::write_ = nullptr;
    LibXR::STDIO::write_mutex_ = nullptr;
    LibXR::STDIO::write_stream_ = nullptr;
  }
};

/**
 * @brief 打印失败原因并终止当前测试进程。 Print the failure reason and terminate the
 * current test process.
 */
inline int Fail(const char* message)
{
  std::cerr << message << '\n';
  std::exit(1);
  return 0;
}

void TestStdioPrintWrappers();
void TestStdioTruncation();
}  // namespace LibXRLinuxStdioPrintTest
