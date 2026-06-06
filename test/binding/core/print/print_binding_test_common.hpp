/**
 * @file print_binding_test_common.hpp
 * @brief binding `print` 测试共用 helper。 Shared helpers for binding `print` tests.
 *
 * 作用 / Purpose:
 * 1. 集中 STDIO 绑定作用域和失败辅助函数。
 *    Centralize STDIO binding scope and failure helpers.
 * 2. 让 split 后的 binding print 测试文件只保留各自场景。
 *    Keep each split binding-print test file focused on its own scenario.
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

namespace LibXRBindingPrintTest
{

struct StdioWriteScope
{
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
 * @brief 辅助函数 `Fail`。 Helper function `Fail`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline int Fail(const char* message)
{
  std::cerr << message << '\n';
  std::exit(1);
  return 0;
}

void TestStdioPrintWrappers();
void TestStdioTruncation();
}  // namespace LibXRBindingPrintTest
