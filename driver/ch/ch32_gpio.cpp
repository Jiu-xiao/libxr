#include "ch32_gpio.hpp"

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