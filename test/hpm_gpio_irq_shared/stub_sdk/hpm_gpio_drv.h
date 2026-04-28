#pragma once

#include <cstdint>

constexpr uint32_t GPIO_DI_GPIOA = 0u;
constexpr uint32_t GPIO_DI_GPIOB = 1u;
constexpr uint32_t GPIO_DI_GPIOX = 13u;
constexpr uint32_t GPIO_DI_GPIOY = 14u;

constexpr uint32_t HPM_TEST_GPIO_PORT_COUNT = 15u;

struct HpmTestGpioReg
{
  uint32_t VALUE = 0u;
  uint32_t SET = 0u;
  uint32_t CLEAR = 0u;
  uint32_t TOGGLE = 0u;
};

struct GPIO_Type
{
  HpmTestGpioReg DI[HPM_TEST_GPIO_PORT_COUNT] = {};
  HpmTestGpioReg DO[HPM_TEST_GPIO_PORT_COUNT] = {};
  HpmTestGpioReg OE[HPM_TEST_GPIO_PORT_COUNT] = {};
  HpmTestGpioReg IF[HPM_TEST_GPIO_PORT_COUNT] = {};
  HpmTestGpioReg IE[HPM_TEST_GPIO_PORT_COUNT] = {};
  HpmTestGpioReg AS[HPM_TEST_GPIO_PORT_COUNT] = {};
};

typedef enum gpio_interrupt_trigger {
  gpio_interrupt_trigger_level_high = 0,
  gpio_interrupt_trigger_level_low,
  gpio_interrupt_trigger_edge_rising,
  gpio_interrupt_trigger_edge_falling,
  gpio_interrupt_trigger_edge_both,
} gpio_interrupt_trigger_t;

static inline uint8_t gpio_read_pin(GPIO_Type* ptr, uint32_t port, uint8_t pin)
{
  return static_cast<uint8_t>((ptr->DI[port].VALUE >> pin) & 0x1u);
}

static inline void gpio_write_pin(GPIO_Type* ptr, uint32_t port, uint8_t pin, uint8_t high)
{
  const uint32_t mask = (1u << pin);
  if (high != 0u)
  {
    ptr->DO[port].SET = mask;
    ptr->DO[port].VALUE |= mask;
  }
  else
  {
    ptr->DO[port].CLEAR = mask;
    ptr->DO[port].VALUE &= ~mask;
  }
}

static inline void gpio_set_pin_input(GPIO_Type* ptr, uint32_t port, uint8_t pin)
{
  const uint32_t mask = (1u << pin);
  ptr->OE[port].CLEAR = mask;
  ptr->OE[port].VALUE &= ~mask;
}

static inline void gpio_set_pin_output(GPIO_Type* ptr, uint32_t port, uint8_t pin)
{
  const uint32_t mask = (1u << pin);
  ptr->OE[port].SET = mask;
  ptr->OE[port].VALUE |= mask;
}

static inline bool gpio_check_pin_interrupt_flag(GPIO_Type* ptr, uint32_t port, uint8_t pin)
{
  return (ptr->IF[port].VALUE & (1u << pin)) != 0u;
}

static inline void gpio_clear_pin_interrupt_flag(GPIO_Type* ptr, uint32_t port, uint8_t pin)
{
  const uint32_t mask = (1u << pin);
  ptr->IF[port].CLEAR = mask;
  ptr->IF[port].VALUE &= ~mask;
}

static inline void gpio_enable_pin_interrupt(GPIO_Type* ptr, uint32_t port, uint8_t pin)
{
  const uint32_t mask = (1u << pin);
  ptr->IE[port].SET = mask;
  ptr->IE[port].VALUE |= mask;
}

static inline void gpio_disable_pin_interrupt(GPIO_Type* ptr, uint32_t port, uint8_t pin)
{
  const uint32_t mask = (1u << pin);
  ptr->IE[port].CLEAR = mask;
  ptr->IE[port].VALUE &= ~mask;
}

static inline uint32_t gpio_get_port_interrupt_flags(GPIO_Type* ptr, uint32_t port)
{
  return ptr->IF[port].VALUE;
}

static inline void gpio_config_pin_interrupt(GPIO_Type* ptr, uint32_t port, uint8_t pin,
                                             gpio_interrupt_trigger_t trigger)
{
  (void) ptr;
  (void) port;
  (void) pin;
  (void) trigger;
}
