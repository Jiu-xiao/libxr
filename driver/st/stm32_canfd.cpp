#include "stm32_canfd.hpp"

#ifdef HAL_FDCAN_MODULE_ENABLED

using namespace LibXR;

STM32CANFD* STM32CANFD::map[STM32_FDCAN_NUMBER] = {nullptr};

stm32_fdcan_id_t STM32_FDCAN_GetID(FDCAN_GlobalTypeDef* addr)
{
  if (addr == nullptr)
  {  // NOLINT
    return stm32_fdcan_id_t::STM32_FDCAN_ID_ERROR;
  }
#ifdef FDCAN1
  else if (addr == FDCAN1)
  {  // NOLINT
    return stm32_fdcan_id_t::STM32_FDCAN1;
  }
#endif
#ifdef FDCAN2
  else if (addr == FDCAN2)
  {  // NOLINT
    return stm32_fdcan_id_t::STM32_FDCAN2;
  }
#endif
#ifdef FDCAN3
  else if (addr == FDCAN3)
  {  // NOLINT
    return stm32_fdcan_id_t::STM32_FDCAN3;
  }
#endif
  else
  {
    return stm32_fdcan_id_t::STM32_FDCAN_ID_ERROR;
  }
}

extern "C" void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef* hcan)
{
  hcan->ErrorCode = HAL_FDCAN_ERROR_NONE;
  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef* hfdcan,
                                              uint32_t ErrorStatusITs)
{
  if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != RESET)
  {
    FDCAN_ProtocolStatusTypeDef protocol_status = {};
    HAL_FDCAN_GetProtocolStatus(hfdcan, &protocol_status);
    if (protocol_status.BusOff)
    {
      CLEAR_BIT(hfdcan->Instance->CCCR, FDCAN_CCCR_INIT);
    }
  }

  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hfdcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef* hcan,
                                                   uint32_t BufferIndexes)
{
  UNUSED(BufferIndexes);
  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_FDCAN_TxFifoEmptyCallback(FDCAN_HandleTypeDef* hcan)
{
  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessTxInterrupt();
  }
}

extern "C" void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef* hcan, uint32_t RxFifo0ITs)
{
  UNUSED(RxFifo0ITs);
  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessRxInterrupt(FDCAN_RX_FIFO0);
  }
}

extern "C" void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef* hcan, uint32_t RxFifo1ITs)
{
  UNUSED(RxFifo1ITs);
  auto can = STM32CANFD::map[STM32_FDCAN_GetID(hcan->Instance)];
  if (can)
  {
    can->ProcessRxInterrupt(FDCAN_RX_FIFO1);
  }
}

#endif
