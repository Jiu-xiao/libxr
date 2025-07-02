#include "stm32_power.hpp"

using namespace LibXR;

STM32PowerManager::STM32PowerManager() {}

void STM32PowerManager::Reset() { NVIC_SystemReset(); }

void STM32PowerManager::Shutdown() { HAL_PWR_EnterSTANDBYMode(); }
