#pragma once

#include "libxr.hpp"

namespace LibXR
{

/**
 * @class PWM
 * @brief Abstract base class for PWM (Pulse Width Modulation) control.
 * PWM（脉冲宽度调制）控制的抽象基类。
 */
class PWM
{
 public:
  PWM() = default;

  /**
   * @struct Configuration
   * @brief Configuration parameters for PWM. PWM 配置参数。
   */
  typedef struct
  {
    uint32_t frequency;  ///< PWM signal frequency in Hz. PWM 信号的频率（Hz）。
  } Configuration;

  /**
   * @brief Sets the duty cycle of the PWM signal. 设置 PWM 信号的占空比。
   * @param value The duty cycle as a floating-point value (0.0 to 1.0).
   * 占空比，浮点值（0.0 到 1.0）。
   * @return ErrorCode indicating success or failure. 返回操作结果的错误码。
   */
  virtual ErrorCode SetDutyCycle(float value) = 0;

  /**
   * @brief Configures the PWM settings. 配置 PWM 参数。
   * @param config The configuration structure containing PWM settings. 配置结构体，包含
   * PWM 设置。
   * @return ErrorCode indicating success or failure. 返回操作结果的错误码。
   */
  virtual ErrorCode SetConfig(Configuration config) = 0;

  /**
   * @brief Enables the PWM output. 启用 PWM 输出。
   * @return ErrorCode indicating success or failure. 返回操作结果的错误码。
   */
  virtual ErrorCode Enable() = 0;

  /**
   * @brief Disables the PWM output. 禁用 PWM 输出。
   * @return ErrorCode indicating success or failure. 返回操作结果的错误码。
   */
  virtual ErrorCode Disable() = 0;
};

}  // namespace LibXR
