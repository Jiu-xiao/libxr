#pragma once

#include "timebase.hpp"

namespace LibXR
{

/**
 * @brief WebAssembly 时间基准实现 / WebAssembly timebase implementation
 */
class WebAsmTimebase : public Timebase
{
 public:
  /**
   * @brief 构造函数 / Constructor
   *
   * 记录 WebAssembly 运行时的时间参考点。
   * Captures the WebAssembly runtime reference timestamp.
   */
  WebAsmTimebase();
};

}  // namespace LibXR
