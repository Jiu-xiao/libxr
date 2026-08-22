#include "stm32_uart.hpp"

#ifdef HAL_UART_MODULE_ENABLED

using namespace LibXR;

STM32UART* STM32UART::map[STM32_UART_NUMBER] = {nullptr};

bool STM32UART::InIsr()
{
#if defined(__CORTEX_M)
  return __get_IPSR() != 0U;
#else
  return false;
#endif
}

namespace
{
STM32UART* FindRegisteredUart(UART_HandleTypeDef* uart_handle)
{
  if (uart_handle == nullptr)
  {
    return nullptr;
  }

  const stm32_uart_id_t id = stm32_uart_get_id(uart_handle->Instance);
  if ((id == STM32_UART_ID_ERROR) ||
      (static_cast<size_t>(id) >= static_cast<size_t>(STM32_UART_NUMBER)))
  {
    return nullptr;
  }

  STM32UART* const uart = STM32UART::map[id];
  return ((uart != nullptr) && (uart->uart_handle_ == uart_handle)) ? uart : nullptr;
}
}  // namespace

stm32_uart_id_t stm32_uart_get_id(USART_TypeDef* addr)
{
  if (addr == nullptr)
  {
    return STM32_UART_ID_ERROR;
  }
#ifdef USART1
  if (addr == USART1) return STM32_USART1;
#endif
#ifdef USART2
  if (addr == USART2) return STM32_USART2;
#endif
#ifdef USART3
  if (addr == USART3) return STM32_USART3;
#endif
#ifdef USART4
  if (addr == USART4) return STM32_USART4;
#endif
#ifdef USART5
  if (addr == USART5) return STM32_USART5;
#endif
#ifdef USART6
  if (addr == USART6) return STM32_USART6;
#endif
#ifdef USART7
  if (addr == USART7) return STM32_USART7;
#endif
#ifdef USART8
  if (addr == USART8) return STM32_USART8;
#endif
#ifdef USART9
  if (addr == USART9) return STM32_USART9;
#endif
#ifdef USART10
  if (addr == USART10) return STM32_USART10;
#endif
#ifdef USART11
  if (addr == USART11) return STM32_USART11;
#endif
#ifdef USART12
  if (addr == USART12) return STM32_USART12;
#endif
#ifdef USART13
  if (addr == USART13) return STM32_USART13;
#endif
#ifdef UART1
  if (addr == UART1) return STM32_UART1;
#endif
#ifdef UART2
  if (addr == UART2) return STM32_UART2;
#endif
#ifdef UART3
  if (addr == UART3) return STM32_UART3;
#endif
#ifdef UART4
  if (addr == UART4) return STM32_UART4;
#endif
#ifdef UART5
  if (addr == UART5) return STM32_UART5;
#endif
#ifdef UART6
  if (addr == UART6) return STM32_UART6;
#endif
#ifdef UART7
  if (addr == UART7) return STM32_UART7;
#endif
#ifdef UART8
  if (addr == UART8) return STM32_UART8;
#endif
#ifdef UART9
  if (addr == UART9) return STM32_UART9;
#endif
#ifdef UART10
  if (addr == UART10) return STM32_UART10;
#endif
#ifdef UART11
  if (addr == UART11) return STM32_UART11;
#endif
#ifdef UART12
  if (addr == UART12) return STM32_UART12;
#endif
#ifdef UART13
  if (addr == UART13) return STM32_UART13;
#endif
#ifdef LPUART1
  if (addr == LPUART1) return STM32_LPUART1;
#endif
#ifdef LPUART2
  if (addr == LPUART2) return STM32_LPUART2;
#endif
#ifdef LPUART3
  if (addr == LPUART3) return STM32_LPUART3;
#endif
  return STM32_UART_ID_ERROR;
}

void STM32UART::DmaAbortCallback(DMA_HandleTypeDef* dma_handle)
{
  if (dma_handle == nullptr)
  {
    return;
  }
  auto* uart = FindRegisteredUart(static_cast<UART_HandleTypeDef*>(dma_handle->Parent));
  if (uart != nullptr)
  {
    uart->dma_model_.OnStopDone(InIsr());
  }
}

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t)
{
  if (auto* uart = FindRegisteredUart(huart); uart != nullptr)
  {
    uart->OnRxDataAvailable(STM32UART::InIsr());
  }
}

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
  if (auto* uart = FindRegisteredUart(huart); uart != nullptr)
  {
    uart->OnTxComplete(STM32UART::InIsr());
  }
}

extern "C" __attribute__((used)) void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{
  if (auto* uart = FindRegisteredUart(huart); uart != nullptr)
  {
    uart->dma_model_.OnTransferError(STM32UART::InIsr());
  }
}

#endif
