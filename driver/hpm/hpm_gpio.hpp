#pragma once

#include "gpio.hpp"
#include "hpm_gpio_drv.h"
#include "hpm_plic_drv.h"
#include "hpm_soc.h"

namespace LibXR
{
/**
 * @class HPMGPIO
 * @brief HPM 平台 GPIO 驱动实现 / GPIO driver implementation for HPM platform.
 *
 * 该类将 LibXR `GPIO` 抽象接口适配到 HPM SDK GPIO API。
 * This class adapts the LibXR `GPIO` abstraction to HPM SDK GPIO APIs.
 *
 * 中断回调由 `CheckInterrupt()` 统一分发。
 * Interrupt callbacks are dispatched through `CheckInterrupt()`.
 */
class HPMGPIO final : public GPIO
{
 public:
  /**
   * @brief 构造 HPM GPIO 对象 / Construct an HPM GPIO object.
   * @param gpio GPIO 控制器基地址 / GPIO controller base address.
   * @param port GPIO 端口号 / GPIO port index.
   * @param pin GPIO 引脚号 / GPIO pin index.
   * @param irq 该引脚/端口对应的中断号（可选） / IRQ number for this pin/port (optional).
   * @param pad_index IOC PAD 编号，默认值表示自动由 `(gpio, port, pin)` 推导 /
   * IOC PAD index. Use default value to auto-resolve from `(gpio, port, pin)`.
   */
  HPMGPIO(GPIO_Type* gpio, uint32_t port, uint8_t pin, uint32_t irq = kInvalidIrq,
          uint16_t pad_index = kInvalidPadIndex);

  /**
   * @brief 读取引脚电平 / Read current pin level.
   * @return `true` 高电平，`false` 低电平 / `true` for high level, `false` for low level.
   */
  inline bool Read() override
  {
    const uint32_t level = gpio_read_pin(gpio_, port_, pin_);
    return level == 1u;
  }

  /**
   * @brief 写引脚电平 / Write output pin level.
   * @param value `true` 高电平，`false` 低电平 / `true` for high level, `false` for low
   * level.
   */
  inline void Write(bool value) override
  {
    gpio_write_pin(gpio_, port_, pin_, value ? 1u : 0u);
  }

  /**
   * @brief 使能当前引脚中断 / Enable GPIO interrupt for current pin.
   * @return 成功返回 `ErrorCode::OK`，IRQ 无效返回 `ErrorCode::ARG_ERR` /
   * Returns `ErrorCode::OK` on success, `ErrorCode::ARG_ERR` when IRQ is invalid.
   */
  ErrorCode EnableInterrupt() override;

  /**
   * @brief 失能当前引脚中断 / Disable GPIO interrupt for current pin.
   * @return 成功返回 `ErrorCode::OK`，IRQ 无效返回 `ErrorCode::ARG_ERR` /
   * Returns `ErrorCode::OK` on success, `ErrorCode::ARG_ERR` when IRQ is invalid.
   */
  ErrorCode DisableInterrupt() override;

  /**
   * @brief 配置当前引脚模式 / Configure GPIO mode for current pin.
   * @param config GPIO 配置参数 / GPIO configuration.
   * @return 成功返回 `ErrorCode::OK`，失败返回对应错误码 /
   * Returns `ErrorCode::OK` on success, error code otherwise.
   * @note 该接口会将 PAD 复用强制切换为 GPIO，不负责外设复用配置 /
   * This API forces pad mux to GPIO function (no peripheral alternate-function setup).
   * @note 在可解析到 PAD 时会自动开启 IOC loopback，用于输出模式下直接通过 `Read()` 读取实际引脚电平 /
   * When PAD can be resolved, IOC loopback is enabled so `Read()` can return the actual pad
   * level even in output mode.
   */
  ErrorCode SetConfig(Configuration config) override;

  /**
   * @brief 将当前 PAD 配置为模拟高阻 / Configure current pad to analog high-impedance.
   * @return 成功返回 `ErrorCode::OK`，PAD 不可解析返回 `ErrorCode::NOT_SUPPORT` /
   * Returns `ErrorCode::OK` on success, `ErrorCode::NOT_SUPPORT` when PAD is unavailable.
   */
  ErrorCode SetAnalogHighImpedance();

  /**
   * @brief 分发某一端口的 GPIO 中断回调 / Dispatch GPIO interrupt callbacks for one port.
   * @param port GPIO 端口号 / GPIO port index.
   *
   * 该函数应在板级 GPIO IRQ Handler 中调用。
   * This function should be called by board IRQ handlers.
   *
   * 同时会清除触发引脚的中断标志位。
   * It also clears interrupt flags for triggered pins.
   */
  static void CheckInterrupt(uint32_t port);

 private:
  static constexpr uint32_t kPortCount = 15;  ///< 支持的端口数量 / Supported port count.
  static constexpr uint32_t kPinCount = 32;   ///< 每个端口引脚数 / Pins per port.
  static constexpr uint32_t kInvalidIrq = 0xFFFFFFFFu;    ///< 无效 IRQ 标记 / Invalid IRQ marker.
  static constexpr uint16_t kInvalidPadIndex = 0xFFFFu;   ///< 无效 PAD 标记 / Invalid PAD marker.

  /**
   * @brief 根据控制器与端口引脚推导 IOC PAD 编号 /
   * Resolve IOC PAD index from controller/port/pin tuple.
   * @param gpio GPIO 控制器基地址 / GPIO controller base address.
   * @param port GPIO 端口号 / GPIO port index.
   * @param pin GPIO 引脚号 / GPIO pin index.
   * @return 有效 PAD 编号，无法推导时返回 `kInvalidPadIndex` /
   * Resolved PAD index, or `kInvalidPadIndex` if unavailable.
   */
  static uint16_t ResolvePadIndex(GPIO_Type* gpio, uint32_t port, uint8_t pin);

  ///< 端口-引脚到对象实例的静态映射，用于中断分发 /
  ///< Static port-pin to object map for interrupt dispatch.
  static HPMGPIO* map[kPortCount][kPinCount];

  GPIO_Type* gpio_;     ///< GPIO 控制器实例 / GPIO controller instance.
  uint32_t port_;       ///< GPIO 端口号 / GPIO port index.
  uint8_t pin_;         ///< GPIO 引脚号 / GPIO pin index.
  uint32_t irq_;        ///< 引脚对应 IRQ 号 / IRQ number for this pin.
  uint16_t pad_index_;  ///< IOC PAD 编号 / IOC PAD index.
};

}  // namespace LibXR

/**
 * @brief GPIO 中断分发的 C 接口 / C entry for GPIO interrupt dispatch.
 * @param port GPIO 端口号 / GPIO port index.
 */
extern "C" void libxr_hpm_gpio_check_interrupt(uint32_t port);
