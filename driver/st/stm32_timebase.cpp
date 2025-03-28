#include "stm32_timebase.hpp"

using namespace LibXR;

#ifdef HAL_TIM_MODULE_ENABLED

TIM_HandleTypeDef* STM32TimerTimebase::htim = nullptr;

#endif
