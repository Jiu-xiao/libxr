#pragma once

#include "gpio.hpp"
#include "libxr.hpp"

#include DEF2STR(LIBXR_CH32_CONFIG_FILE)

namespace LibXR
{

typedef enum
{
#if defined(GPIOA)
  CH32_GPIOA,
#endif
#if defined(GPIOB)
  CH32_GPIOB,
#endif
#if defined(GPIOC)
  CH32_GPIOC,
#endif
#if defined(GPIOD)
  CH32_GPIOD,
#endif
#if defined(GPIOE)
  CH32_GPIOE,
#endif
#if defined(GPIOF)
  CH32_GPIOF,
#endif
#if defined(GPIOG)
  CH32_GPIOG,
#endif
#if defined(GPIOH)
  CH32_GPIOH,
#endif
#if defined(GPIOI)
  CH32_GPIOI,
#endif
  CH32_GPIO_NUMBER
} ch32_gpio_group_t;

uint32_t ch32_get_gpio_periph(GPIO_TypeDef* port);

class CH32GPIO final : public GPIO
{
 public:
  CH32GPIO(GPIO_TypeDef* port, uint16_t pin,
           GPIO::Direction direction = GPIO::Direction::OUTPUT_PUSH_PULL,
           GPIO::Pull pull = GPIO::Pull::NONE, IRQn_Type irq = NonMaskableInt_IRQn);

  inline bool Read() override { return (port_->INDR & pin_) != (uint32_t)Bit_RESET; }

  inline void Write(bool value) override
  {
    if (value)
    {
      port_->BSHR = pin_;
    }
    else
    {
      port_->BCR = pin_;
    }
  }

  ErrorCode EnableInterrupt() override;

  ErrorCode DisableInterrupt() override;

  inline ErrorCode SetConfig(Configuration config) override
  {
    GPIO_InitTypeDef gpio_init = {};
    gpio_init.GPIO_Pin = pin_;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;

    switch (config.direction)
    {
      case Direction::INPUT:
      case Direction::RISING_INTERRUPT:
      case Direction::FALL_INTERRUPT:
      case Direction::FALL_RISING_INTERRUPT:
        gpio_init.GPIO_Mode = (config.pull == Pull::UP)     ? GPIO_Mode_IPU
                              : (config.pull == Pull::DOWN) ? GPIO_Mode_IPD
                                                            : GPIO_Mode_IN_FLOATING;
        break;

      case Direction::OUTPUT_PUSH_PULL:
        gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
        break;

      case Direction::OUTPUT_OPEN_DRAIN:
        gpio_init.GPIO_Mode = GPIO_Mode_Out_OD;
        break;
    }

    GPIO_Init(port_, &gpio_init);

    switch (config.direction)
    {
      case Direction::RISING_INTERRUPT:
        ConfigureEXTI(EXTI_Trigger_Rising);
        break;
      case Direction::FALL_INTERRUPT:
        ConfigureEXTI(EXTI_Trigger_Falling);
        break;
      case Direction::FALL_RISING_INTERRUPT:
        ConfigureEXTI(EXTI_Trigger_Rising_Falling);
        break;
      default:
        break;
    }

    return ErrorCode::OK;
  }

  void OnInterrupt();

  static void CheckInterrupt(uint32_t line);

  static inline CH32GPIO* map_[16] = {nullptr};

 private:
  GPIO_TypeDef* port_;
  uint16_t pin_;
  IRQn_Type irq_;

  void ConfigureEXTI(EXTITrigger_TypeDef trigger);

  static uint8_t GetEXTIID(uint16_t pin);
};

}  // namespace LibXR
