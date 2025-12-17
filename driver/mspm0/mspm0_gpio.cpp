#include "mspm0_gpio.hpp"

using namespace LibXR;

MSPM0GPIO* MSPM0GPIO::instance_map_[MAX_PORTS][32] = {{nullptr}};

/**
 * @brief 获取 GPIO 端口索引
 *
 * @param port_addr GPIO 端口基地址
 * @return int 端口索引 (0 = GPIOA, 1 = GPIOB, 2 = GPIOC, -1 = 无效地址)
 */
static inline int MSPM0_GPIO_GetPortIndex(uint32_t port_addr)  // NOLINT
{
  if (port_addr == GPIOA_BASE)
  {
    return 0;
  }
#ifdef GPIOB_BASE
  if (port_addr == GPIOB_BASE)
  {
    return 1;
  }
#endif
#ifdef GPIOC_BASE
  if (port_addr == GPIOC_BASE) return 2;
#endif
  return -1;
}

/**
 * @brief 计算 GPIO 中断极性掩码
 *
 * @param pin 引脚号 (0-31)
 * @param direction 中断触发方向
 * @return uint32_t 极性掩码，用于配置中断触发边沿
 */
static inline uint32_t MSPM0_GPIO_GetPolarityMask(uint8_t pin,  // NOLINT
                                                  LibXR::GPIO::Direction direction)
{
  if (pin < 16)
  {
    switch (direction)
    {
      case LibXR::GPIO::Direction::RISING_INTERRUPT:
        return GPIO_POLARITY15_0_DIO0_RISE << (pin * 2);
      case LibXR::GPIO::Direction::FALL_INTERRUPT:
        return GPIO_POLARITY15_0_DIO0_FALL << (pin * 2);
      case LibXR::GPIO::Direction::FALL_RISING_INTERRUPT:
        return GPIO_POLARITY15_0_DIO0_RISE_FALL << (pin * 2);
      default:
        return 0;
    }
  }
  else
  {
    uint8_t pin_offset = pin - 16;
    switch (direction)
    {
      case LibXR::GPIO::Direction::RISING_INTERRUPT:
        return GPIO_POLARITY31_16_DIO16_RISE << (pin_offset * 2);
      case LibXR::GPIO::Direction::FALL_INTERRUPT:
        return GPIO_POLARITY31_16_DIO16_FALL << (pin_offset * 2);
      case LibXR::GPIO::Direction::FALL_RISING_INTERRUPT:
        return GPIO_POLARITY31_16_DIO16_RISE_FALL << (pin_offset * 2);
      default:
        return 0;
    }
  }
}

MSPM0GPIO::MSPM0GPIO(uint32_t port_addr, uint32_t pin_mask, uint32_t pincm)
    : port_addr_(port_addr), pin_mask_(pin_mask), pincm_(pincm)
{
  int pin_idx = __builtin_ctz(pin_mask_);
  int port_idx = MSPM0_GPIO_GetPortIndex(port_addr_);

  ASSERT(port_idx >= 0 && port_idx < LibXR::MAX_PORTS);
  ASSERT(instance_map_[port_idx][pin_idx] == nullptr);

  instance_map_[port_idx][pin_idx] = this;
}

bool MSPM0GPIO::Read()
{
  GPIO_Regs* port = reinterpret_cast<GPIO_Regs*>(port_addr_);  // NOLINT
  return DL_GPIO_readPins(port, pin_mask_) == pin_mask_;
}

ErrorCode MSPM0GPIO::Write(bool value)
{
  GPIO_Regs* port = reinterpret_cast<GPIO_Regs*>(port_addr_);  // NOLINT

  if (current_direction_ == LibXR::GPIO::Direction::OUTPUT_OPEN_DRAIN)
  {
    if (value)
    {
      // 开漏高阻 / Open-drain high-Z
      DL_GPIO_disableOutput(port, pin_mask_);
    }
    else
    {
      // 开漏拉低 / Open-drain pull low
      DL_GPIO_clearPins(port, pin_mask_);
      DL_GPIO_enableOutput(port, pin_mask_);
    }
  }
  else
  {
    if (value)
    {
      DL_GPIO_setPins(port, pin_mask_);
    }
    else
    {
      DL_GPIO_clearPins(port, pin_mask_);
    }
  }
  return ErrorCode::OK;
}

ErrorCode MSPM0GPIO::EnableInterrupt()
{
  GPIO_Regs* port = reinterpret_cast<GPIO_Regs*>(port_addr_);  // NOLINT
  DL_GPIO_enableInterrupt(port, pin_mask_);
  return ErrorCode::OK;
}

ErrorCode MSPM0GPIO::DisableInterrupt()
{
  GPIO_Regs* port = reinterpret_cast<GPIO_Regs*>(port_addr_);  // NOLINT
  DL_GPIO_disableInterrupt(port, pin_mask_);
  return ErrorCode::OK;
}

ErrorCode MSPM0GPIO::SetConfig(Configuration config)
{
  current_direction_ = config.direction;

  GPIO_Regs* port = reinterpret_cast<GPIO_Regs*>(port_addr_);  // NOLINT

  DL_GPIO_disableOutput(port, pin_mask_);
  DL_GPIO_disableInterrupt(port, pin_mask_);
  DL_GPIO_clearInterruptStatus(port, pin_mask_);

  switch (config.direction)
  {
    case Direction::INPUT:
    {
      DL_GPIO_RESISTOR res = DL_GPIO_RESISTOR_NONE;
      if (config.pull == Pull::UP)
      {
        res = DL_GPIO_RESISTOR_PULL_UP;
      }
      else if (config.pull == Pull::DOWN)
      {
        res = DL_GPIO_RESISTOR_PULL_DOWN;
      }
      DL_GPIO_initDigitalInputFeatures(pincm_, DL_GPIO_INVERSION_DISABLE, res,
                                       DL_GPIO_HYSTERESIS_DISABLE,
                                       DL_GPIO_WAKEUP_DISABLE);
      break;
    }

    case Direction::OUTPUT_PUSH_PULL:
    {
      DL_GPIO_RESISTOR res = DL_GPIO_RESISTOR_NONE;
      if (config.pull == Pull::UP)
      {
        res = DL_GPIO_RESISTOR_PULL_UP;
      }
      else if (config.pull == Pull::DOWN)
      {
        res = DL_GPIO_RESISTOR_PULL_DOWN;
      }

      DL_GPIO_initDigitalOutputFeatures(pincm_, DL_GPIO_INVERSION_DISABLE, res,
                                        DL_GPIO_DRIVE_STRENGTH_LOW, DL_GPIO_HIZ_DISABLE);

      volatile uint32_t* pincm_reg = &IOMUX->SECCFG.PINCM[pincm_];
      *pincm_reg |= IOMUX_PINCM_INENA_ENABLE;

      DL_GPIO_clearPins(port, pin_mask_);

      DL_GPIO_enableOutput(port, pin_mask_);
      break;
    }

    case Direction::OUTPUT_OPEN_DRAIN:
    {
      /* 伪开漏：输入模式(高阻) + 软件控制拉低 / Pseudo open-drain: Input mode(HiZ) +
       * software pull-low */
      DL_GPIO_RESISTOR res = DL_GPIO_RESISTOR_NONE;
      if (config.pull == Pull::UP)
      {
        res = DL_GPIO_RESISTOR_PULL_UP;
      }

      DL_GPIO_initDigitalInputFeatures(pincm_, DL_GPIO_INVERSION_DISABLE, res,
                                       DL_GPIO_HYSTERESIS_DISABLE,
                                       DL_GPIO_WAKEUP_DISABLE);
      break;
    }

    case Direction::FALL_INTERRUPT:
    case Direction::RISING_INTERRUPT:
    case Direction::FALL_RISING_INTERRUPT:
    {
      DL_GPIO_RESISTOR res = DL_GPIO_RESISTOR_NONE;
      if (config.pull == Pull::UP)
      {
        res = DL_GPIO_RESISTOR_PULL_UP;
      }
      else if (config.pull == Pull::DOWN)
      {
        res = DL_GPIO_RESISTOR_PULL_DOWN;
      }

      DL_GPIO_initDigitalInputFeatures(pincm_, DL_GPIO_INVERSION_DISABLE, res,
                                       DL_GPIO_HYSTERESIS_DISABLE,
                                       DL_GPIO_WAKEUP_DISABLE);

      uint32_t pol_mask =
          MSPM0_GPIO_GetPolarityMask(__builtin_ctz(pin_mask_), config.direction);
      if (pol_mask)
      {
        uint32_t pin_idx = __builtin_ctz(pin_mask_);

        auto update_polarity = [&](auto getter, auto setter, uint32_t offset)
        {
          constexpr uint32_t BITS_PER_PIN = 2;
          constexpr uint32_t CLEAR_PATTERN = 0x3U;

          uint32_t shift = (pin_idx - offset) * BITS_PER_PIN;
          uint32_t clear_mask = CLEAR_PATTERN << shift;

          uint32_t current_polarity = getter(port);
          current_polarity &= ~clear_mask;
          current_polarity |= pol_mask;
          setter(port, current_polarity);
        };

        if (pin_idx < 16)
        {
          update_polarity(DL_GPIO_getLowerPinsPolarity, DL_GPIO_setLowerPinsPolarity, 0);
        }
        else
        {
          update_polarity(DL_GPIO_getUpperPinsPolarity, DL_GPIO_setUpperPinsPolarity, 16);
        }
      }

      DL_GPIO_clearInterruptStatus(port, pin_mask_);
      DL_GPIO_enableInterrupt(port, pin_mask_);
    }
    break;

    default:
      return ErrorCode::FAILED;
  }

  return ErrorCode::OK;
}

void MSPM0GPIO::OnInterrupt(GPIO_Regs* port, int port_idx)
{
  uint32_t pending_pins = DL_GPIO_getEnabledInterruptStatus(port, 0xFFFFFFFF);

  if (pending_pins != 0)
  {
    DL_GPIO_clearInterruptStatus(port, pending_pins);
  }

  while (pending_pins)
  {
    uint32_t pin_idx = __builtin_ctz(pending_pins);
    uint32_t pin_mask = (1U << pin_idx);

    pending_pins &= ~pin_mask;

    auto* instance = instance_map_[port_idx][pin_idx];
    if (instance)
    {
      instance->callback_.Run(true);
    }
  }
}