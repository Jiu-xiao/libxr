#include "ch32_gpio.hpp"

using namespace LibXR;

uint32_t LibXR::CH32GetGPIOPeriph(GPIO_TypeDef* port)
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
  return 0;
}

extern "C" void EXTI0_IRQHandler(void) __attribute__((interrupt));
extern "C" void EXTI0_IRQHandler(void) { LibXR::CH32GPIO::CheckInterrupt(EXTI_Line0); }

extern "C" void EXTI1_IRQHandler(void) __attribute__((interrupt));
extern "C" void EXTI1_IRQHandler(void) { LibXR::CH32GPIO::CheckInterrupt(EXTI_Line1); }

extern "C" void EXTI2_IRQHandler(void) __attribute__((interrupt));
extern "C" void EXTI2_IRQHandler(void) { LibXR::CH32GPIO::CheckInterrupt(EXTI_Line2); }

extern "C" void EXTI3_IRQHandler(void) __attribute__((interrupt));
extern "C" void EXTI3_IRQHandler(void) { LibXR::CH32GPIO::CheckInterrupt(EXTI_Line3); }

extern "C" void EXTI4_IRQHandler(void) __attribute__((interrupt));
extern "C" void EXTI4_IRQHandler(void) { LibXR::CH32GPIO::CheckInterrupt(EXTI_Line4); }

extern "C" void EXTI9_5_IRQHandler(void) __attribute__((interrupt));
extern "C" void EXTI9_5_IRQHandler(void)
{
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line5);
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line6);
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line7);
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line8);
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line9);
}

extern "C" void EXTI15_10_IRQHandler(void) __attribute__((interrupt));
extern "C" void EXTI15_10_IRQHandler(void)
{
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line10);
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line11);
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line12);
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line13);
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line14);
  LibXR::CH32GPIO::CheckInterrupt(EXTI_Line15);
}

CH32GPIO::CH32GPIO(GPIO_TypeDef* port, uint16_t pin, GPIO::Direction direction,
                   GPIO::Pull pull, IRQn_Type irq)
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

bool CH32GPIO::Read() { return (port_->INDR & pin_) != (uint32_t)Bit_RESET; }

ErrorCode CH32GPIO::Write(bool value)
{
  if (value)
    port_->BSHR = pin_;
  else
    port_->BCR = pin_;
  return ErrorCode::OK;
}

ErrorCode CH32GPIO::EnableInterrupt()
{
  EXTI->INTENR |= (1 << GetEXTIID(pin_));
  return ErrorCode::OK;
}

ErrorCode CH32GPIO::DisableInterrupt()
{
  EXTI->INTENR &= ~(1 << GetEXTIID(pin_));
  return ErrorCode::OK;
}

ErrorCode CH32GPIO::SetConfig(Configuration config)
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

void CH32GPIO::OnInterrupt()
{
  if (!callback_.Empty())
  {
    callback_.Run(true);
  }
}

void CH32GPIO::CheckInterrupt(uint32_t line)
{
  if (EXTI_GetITStatus(line) != RESET)
  {
    EXTI_ClearITPendingBit(line);
    map[GetEXTIID(line)]->OnInterrupt();
  }
}

void CH32GPIO::ConfigureEXTI(EXTITrigger_TypeDef trigger)
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

  NVIC_EnableIRQ(irq_);
}

uint8_t CH32GPIO::GetEXTIID(uint16_t pin) { return __builtin_ctz(pin); }
