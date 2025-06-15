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

static uint32_t CH32GetGPIOPeriph(GPIO_TypeDef* port)
{
  if (false)
  {
  }
#if defined(GPIOA)
  else if (port == GPIOA)
    return RCC_APB2Periph_GPIOA;
#endif
#if defined(GPIOB)
  else if (port == GPIOB)
    return RCC_APB2Periph_GPIOB;
#endif
#if defined(GPIOC)
  else if (port == GPIOC)
    return RCC_APB2Periph_GPIOC;
#endif
#if defined(GPIOD)
  else if (port == GPIOD)
    return RCC_APB2Periph_GPIOD;
#endif
#if defined(GPIOE)
  else if (port == GPIOE)
    return RCC_APB2Periph_GPIOE;
#endif
#if defined(GPIOF)
  else if (port == GPIOF)
    return RCC_APB2Periph_GPIOF;
#endif
#if defined(GPIOG)
  else if (port == GPIOG)
    return RCC_APB2Periph_GPIOG;
#endif
#if defined(GPIOH)
  else if (port == GPIOH)
    return RCC_APB2Periph_GPIOH;
#endif
#if defined(GPIOI)
  else if (port == GPIOI)
    return RCC_APB2Periph_GPIOI;
#endif
  return 0;
}

class CH32GPIO : public GPIO
{
 public:
  CH32GPIO(GPIO_TypeDef* port, uint16_t pin,
           GPIO::Direction direction = GPIO::Direction::OUTPUT_PUSH_PULL,
           GPIO::Pull pull = GPIO::Pull::NONE, IRQn_Type irq = NonMaskableInt_IRQn)
      : port_(port), pin_(pin), irq_(irq)
  {
    if (irq_ != NonMaskableInt_IRQn)
    {
      NVIC_EnableIRQ(irq_);
      map[GetEXTIID(pin)] = this;
    }

    RCC_APB2PeriphClockCmd(CH32GetGPIOPeriph(port_), ENABLE);

    GPIO_InitTypeDef gpio_init = {};
    gpio_init.GPIO_Pin = pin_;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;

    switch (direction)
    {
      case Direction::RISING_INTERRUPT:
      case Direction::FALL_INTERRUPT:
      case Direction::FALL_RISING_INTERRUPT:
      case Direction::INPUT:
        gpio_init.GPIO_Mode = (pull == Pull::UP)     ? GPIO_Mode_IPU
                              : (pull == Pull::DOWN) ? GPIO_Mode_IPD
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

    switch (direction)
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
  }

  bool Read() override { return GPIO_ReadInputDataBit(port_, pin_) == Bit_SET; }

  ErrorCode Write(bool value) override
  {
    if (value)
      GPIO_SetBits(port_, pin_);
    else
      GPIO_ResetBits(port_, pin_);
    return ErrorCode::OK;
  }

  ErrorCode EnableInterrupt() override
  {
    EXTI->INTENR |= (1 << GetEXTIID(pin_));
    return ErrorCode::OK;
  }

  ErrorCode DisableInterrupt() override
  {
    EXTI->INTENR &= ~(1 << GetEXTIID(pin_));
    return ErrorCode::OK;
  }

  ErrorCode SetConfig(Configuration config) override
  {
    GPIO_InitTypeDef gpio_init = {};
    gpio_init.GPIO_Pin = pin_;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;

    switch (config.direction)
    {
      case Direction::INPUT:
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
      case Direction::RISING_INTERRUPT:
      case Direction::FALL_INTERRUPT:
      case Direction::FALL_RISING_INTERRUPT:
        gpio_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
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

  void OnInterrupt()
  {
    if (!callback_.Empty())
    {
      callback_.Run(true);
    }
  }

  static void CheckInterrupt(uint32_t line)
  {
    if (EXTI_GetITStatus(line) != RESET)
    {
      EXTI_ClearITPendingBit(line);
      map[GetEXTIID(line)]->OnInterrupt();
    }
  }

  static inline CH32GPIO* map[16] = {nullptr};

 private:
  GPIO_TypeDef* port_;
  uint16_t pin_;
  IRQn_Type irq_;

  void ConfigureEXTI(EXTITrigger_TypeDef trigger)
  {
    EXTI_InitTypeDef exti = {};
    uint8_t pin_source = __builtin_ctz(pin_);
    uint8_t port_source = 0xFF;

#if defined(GPIOA)
    if (port_ == GPIOA) port_source = GPIO_PortSourceGPIOA;
#endif
#if defined(GPIOB)
    else if (port_ == GPIOB)
    {
      port_source = GPIO_PortSourceGPIOB;
    }
#endif
#if defined(GPIOC)
    else if (port_ == GPIOC)
    {
      port_source = GPIO_PortSourceGPIOC;
    }
#endif
#if defined(GPIOD)
    else if (port_ == GPIOD)
    {
      port_source = GPIO_PortSourceGPIOD;
    }
#endif
#if defined(GPIOE)
    else if (port_ == GPIOE)
    {
      port_source = GPIO_PortSourceGPIOE;
    }
#endif
#if defined(GPIOF)
    else if (port_ == GPIOF)
    {
      port_source = GPIO_PortSourceGPIOF;
    }
#endif
#if defined(GPIOG)
    else if (port_ == GPIOG)
    {
      port_source = GPIO_PortSourceGPIOG;
    }
#endif
#if defined(GPIOH)
    else if (port_ == GPIOH)
    {
      port_source = GPIO_PortSourceGPIOH;
    }
#endif
#if defined(GPIOI)
    else if (port_ == GPIOI)
    {
      port_source = GPIO_PortSourceGPIOI;
    }
#endif

    ASSERT(port_source != 0xFF);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_EXTILineConfig(port_source, pin_source);

    exti.EXTI_Line = 1 << pin_source;
    exti.EXTI_Mode = EXTI_Mode_Interrupt;
    exti.EXTI_Trigger = trigger;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);

    NVIC_InitTypeDef nvic = {};
    nvic.NVIC_IRQChannel = irq_;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority = 1;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
  }

  static uint8_t GetEXTIID(uint16_t pin) { return __builtin_ctz(pin); }
};

}  // namespace LibXR
