#include "stm32_can.hpp"

#ifdef HAL_CAN_MODULE_ENABLED

using namespace LibXR;

STM32CAN* STM32CAN::map[STM32_CAN_NUMBER] = {nullptr};

/**
 * @brief 获取 CAN ID Get CAN ID
 *
 * @param addr CAN外设地址 CAN device address
 * @return stm32_can_id_t
 */
stm32_can_id_t STM32_CAN_GetID(CAN_TypeDef* addr)
{
  if (addr == nullptr)
  {  // NOLINT
    return stm32_can_id_t::STM32_CAN_ID_ERROR;
  }
#ifdef CAN1
  else if (addr == CAN1)
  {  // NOLINT
    return stm32_can_id_t::STM32_CAN1;
  }
#endif
#ifdef CAN2
  else if (addr == CAN2)
  {  // NOLINT
    return stm32_can_id_t::STM32_CAN2;
  }
#endif
#ifdef CAN3
  else if (addr == CAN3)
  {  // NOLINT
    return stm32_can_id_t::STM32_CAN3;
  }
#endif
  else
  {
    return stm32_can_id_t::STM32_CAN_ID_ERROR;
  }
}

extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* hcan)
{
  STM32CAN* can = STM32CAN::map[STM32_CAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessRxInterrupt();
  }
}

extern "C" void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef* hcan)
{
  STM32CAN* can = STM32CAN::map[STM32_CAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessRxInterrupt();
  }
}

extern "C" void HAL_CAN_ErrorCallback(CAN_HandleTypeDef* hcan)
{
  HAL_CAN_ResetError(hcan);
}

extern "C" void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef* hcan)
{
  STM32CAN* can = STM32CAN::map[STM32_CAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef* hcan)
{
  STM32CAN* can = STM32CAN::map[STM32_CAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef* hcan)
{
  STM32CAN* can = STM32CAN::map[STM32_CAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

#endif
