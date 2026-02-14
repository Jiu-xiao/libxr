#include "hpm_gpio.hpp"

#include "hpm_ioc_regs.h"

using namespace LibXR;

HPMGPIO* HPMGPIO::map[HPMGPIO::kPortCount][HPMGPIO::kPinCount] = {};

/**
 * @brief 将 PAD 复用切换为 GPIO 功能 / Route PAD mux to GPIO function.
 * @param gpio GPIO 控制器基地址 / GPIO controller base address.
 * @param port GPIO 端口号 / GPIO port index.
 * @param pad_index IOC PAD 编号 / IOC PAD index.
 * @param enable_loopback 是否使能 IOC loopback / Whether to enable IOC loopback.
 *
 * 仅处理 GPIO 复用，不涉及外设复用配置。
 * Only GPIO muxing is handled here; peripheral alternate-function setup is not included.
 */
static inline void HPM_GPIO_ConfigMuxToGPIO(GPIO_Type* gpio, uint32_t port, uint16_t pad_index,
                                            bool enable_loopback = true)
{
  uint32_t func_ctl = IOC_PAD_FUNC_CTL_ALT_SELECT_SET(0);
  if (enable_loopback)
  {
    func_ctl |= IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
  }
  HPM_IOC->PAD[pad_index].FUNC_CTL = func_ctl;

  // GPIOY 端口需要额外通过 PIOC 选择 SOC GPIO 信号 / GPIOY needs extra PIOC routing.
  if (port == GPIO_DI_GPIOY && (gpio == HPM_GPIO0 || gpio == HPM_FGPIO))
  {
    HPM_PIOC->PAD[pad_index].FUNC_CTL = IOC_PAD_FUNC_CTL_ALT_SELECT_SET(3);
  }
}

/**
 * @brief 构造 HPM GPIO 对象并注册到中断映射表 /
 * Construct HPM GPIO object and register into interrupt map.
 */
HPMGPIO::HPMGPIO(GPIO_Type* gpio, uint32_t port, uint8_t pin, uint32_t irq,
                 uint16_t pad_index)
    : gpio_(gpio),
      port_(port),
      pin_(pin),
      irq_(irq),
      pad_index_(pad_index == kInvalidPadIndex ? ResolvePadIndex(gpio, port, pin)
                                               : pad_index)
{
  if (port_ < kPortCount && pin_ < kPinCount)
  {
    map[port_][pin_] = this;
  }
}

/**
 * @brief 使能当前引脚中断 / Enable interrupt for current pin.
 */
ErrorCode HPMGPIO::EnableInterrupt()
{
  if (irq_ == kInvalidIrq)
  {
    return ErrorCode::ARG_ERR;
  }
  gpio_enable_pin_interrupt(gpio_, port_, pin_);
  __plic_enable_irq(HPM_PLIC_BASE, HPM_PLIC_TARGET_M_MODE, irq_);
  return ErrorCode::OK;
}

/**
 * @brief 失能当前引脚中断 / Disable interrupt for current pin.
 */
ErrorCode HPMGPIO::DisableInterrupt()
{
  if (irq_ == kInvalidIrq)
  {
    return ErrorCode::ARG_ERR;
  }
  gpio_disable_pin_interrupt(gpio_, port_, pin_);
  __plic_disable_irq(HPM_PLIC_BASE, HPM_PLIC_TARGET_M_MODE, irq_);
  return ErrorCode::OK;
}

/**
 * @brief 配置 GPIO 方向、中断模式与上下拉 / Configure GPIO direction, interrupt mode, and pull.
 */
ErrorCode HPMGPIO::SetConfig(Configuration config)
{
  if (port_ >= kPortCount || pin_ >= kPinCount)
  {
    return ErrorCode::ARG_ERR;
  }

  if (pad_index_ != kInvalidPadIndex)
  {
    // 保持接口自洽：先确保 PAD 复用到 GPIO / Keep API self-contained: force GPIO mux first.
    HPM_GPIO_ConfigMuxToGPIO(gpio_, port_, pad_index_);
  }

  switch (config.direction)
  {
    case Direction::INPUT:
      gpio_set_pin_input(gpio_, port_, pin_);
      break;
    case Direction::OUTPUT_PUSH_PULL:
    case Direction::OUTPUT_OPEN_DRAIN:
      gpio_set_pin_output(gpio_, port_, pin_);
      break;
    case Direction::FALL_INTERRUPT:
      gpio_set_pin_input(gpio_, port_, pin_);
      gpio_config_pin_interrupt(gpio_, port_, pin_, gpio_interrupt_trigger_edge_falling);
      break;
    case Direction::RISING_INTERRUPT:
      gpio_set_pin_input(gpio_, port_, pin_);
      gpio_config_pin_interrupt(gpio_, port_, pin_, gpio_interrupt_trigger_edge_rising);
      break;
    case Direction::FALL_RISING_INTERRUPT:
      gpio_set_pin_input(gpio_, port_, pin_);
      gpio_config_pin_interrupt(gpio_, port_, pin_, gpio_interrupt_trigger_edge_both);
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  if (pad_index_ != kInvalidPadIndex)
  {
    uint32_t pad_ctl = HPM_IOC->PAD[pad_index_].PAD_CTL;

    // 仅更新上下拉与开漏位，保留其他电气属性 / Update pull + open-drain bits only.
    pad_ctl &=
        ~(IOC_PAD_PAD_CTL_PE_MASK | IOC_PAD_PAD_CTL_PS_MASK | IOC_PAD_PAD_CTL_OD_MASK);

    switch (config.pull)
    {
      case Pull::NONE:
        break;
      case Pull::UP:
        pad_ctl |= IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
        break;
      case Pull::DOWN:
        pad_ctl |= IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(0);
        break;
      default:
        return ErrorCode::ARG_ERR;
    }

    if (config.direction == Direction::OUTPUT_OPEN_DRAIN)
    {
      pad_ctl |= IOC_PAD_PAD_CTL_OD_SET(1);
    }

    HPM_IOC->PAD[pad_index_].PAD_CTL = pad_ctl;
  }

  return ErrorCode::OK;
}

/**
 * @brief 分发指定端口的 GPIO 中断回调 / Dispatch GPIO interrupt callbacks for a port.
 * @param port GPIO 端口号 / GPIO port index.
 */
void HPMGPIO::CheckInterrupt(uint32_t port)
{
  if (port >= kPortCount)
  {
    return;
  }

  GPIO_Type* controller = nullptr;
  for (uint8_t pin = 0; pin < kPinCount; ++pin)
  {
    if (map[port][pin] != nullptr)
    {
      controller = map[port][pin]->gpio_;
      break;
    }
  }

  if (controller == nullptr)
  {
    return;
  }

  const uint32_t flags = gpio_get_port_interrupt_flags(controller, port);
  if (flags == 0u)
  {
    return;
  }

  for (uint8_t pin = 0; pin < kPinCount; ++pin)
  {
    if ((flags & (1u << pin)) == 0u)
    {
      continue;
    }

    gpio_clear_pin_interrupt_flag(controller, port, pin);
    if (auto* gpio = map[port][pin])
    {
      gpio->callback_.Run(true);
    }
  }
}

/**
 * @brief 推导指定 GPIO 对应的 IOC PAD 编号 / Resolve IOC PAD index for a GPIO pin.
 */
uint16_t HPMGPIO::ResolvePadIndex(GPIO_Type* gpio, uint32_t port, uint8_t pin)
{
  if (gpio != HPM_GPIO0 && gpio != HPM_FGPIO)
  {
    return kInvalidPadIndex;
  }

  switch (port)
  {
    case GPIO_DI_GPIOA:
      return static_cast<uint16_t>(IOC_PAD_PA00 + pin);
    case GPIO_DI_GPIOB:
      return static_cast<uint16_t>(IOC_PAD_PB00 + pin);
    case GPIO_DI_GPIOX:
      return pin < 8u ? static_cast<uint16_t>(IOC_PAD_PX00 + pin) : kInvalidPadIndex;
    case GPIO_DI_GPIOY:
      return pin < 8u ? static_cast<uint16_t>(IOC_PAD_PY00 + pin) : kInvalidPadIndex;
    default:
      return kInvalidPadIndex;
  }
}

/**
 * @brief GPIO 中断分发的 C 封装入口 / C wrapper entry for GPIO interrupt dispatch.
 * @param port GPIO 端口号 / GPIO port index.
 */
extern "C" void libxr_hpm_gpio_check_interrupt(uint32_t port)
{
  HPMGPIO::CheckInterrupt(port);
}
