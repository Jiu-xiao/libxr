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

uint32_t CH32GetGPIOPeriph(GPIO_TypeDef* port);

class CH32GPIO : public GPIO
{
 public:
  CH32GPIO(GPIO_TypeDef* port, uint16_t pin,
           GPIO::Direction direction = GPIO::Direction::OUTPUT_PUSH_PULL,
           GPIO::Pull pull = GPIO::Pull::NONE, IRQn_Type irq = NonMaskableInt_IRQn);

  bool Read() override;

  ErrorCode Write(bool value) override;

  ErrorCode EnableInterrupt() override;

  ErrorCode DisableInterrupt() override;

  ErrorCode SetConfig(Configuration config) override;

  void OnInterrupt();

  static void CheckInterrupt(uint32_t line);

  static inline CH32GPIO* map[16] = {nullptr};

 private:
  GPIO_TypeDef* port_;
  uint16_t pin_;
  IRQn_Type irq_;

  void ConfigureEXTI(EXTITrigger_TypeDef trigger);

  static uint8_t GetEXTIID(uint16_t pin);
};

}  // namespace LibXR
