#include "ch32_power.hpp"

using namespace LibXR;

void CH32PowerManager::Reset() { NVIC_SystemReset(); }

void CH32PowerManager::Shutdown() { PWR_EnterSTANDBYMode(); }
