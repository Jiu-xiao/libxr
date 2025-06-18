#include "ch32_uart_def.hpp"

ch32_uart_id_t CH32_UART_GetID(USART_TypeDef *addr)
{
  if (addr == nullptr)
  {  // NOLINT
    return ch32_uart_id_t::CH32_UART_ID_ERROR;
  }
#if defined(USART1)
  else if (addr == USART1)
  {  // NOLINT
    return ch32_uart_id_t::CH32_USART1;
  }
#endif
#if defined(USART2)
  else if (addr == USART2)
  {  // NOLINT
    return ch32_uart_id_t::CH32_USART2;
  }
#endif
#if defined(USART3)
  else if (addr == USART3)
  {  // NOLINT
    return ch32_uart_id_t::CH32_USART3;
  }
#endif
#if defined(USART4)
  else if (addr == USART4)
  {  // NOLINT
    return ch32_uart_id_t::CH32_USART4;
  }
#endif
#if defined(USART5)
  else if (addr == USART5)
  {  // NOLINT
    return ch32_uart_id_t::CH32_USART5;
  }
#endif
#if defined(USART6)
  else if (addr == USART6)
  {  // NOLINT
    return ch32_uart_id_t::CH32_USART6;
  }
#endif
#if defined(USART7)
  else if (addr == USART7)
  {  // NOLINT
    return ch32_uart_id_t::CH32_USART7;
  }
#endif
#if defined(USART8)
  else if (addr == USART8)
  {  // NOLINT
    return ch32_uart_id_t::CH32_USART8;
  }
#endif
#if defined(UART1)
  else if (addr == UART1)
  {  // NOLINT
    return ch32_uart_id_t::CH32_UART1;
  }
#endif
#if defined(UART2)
  else if (addr == UART2)
  {  // NOLINT
    return ch32_uart_id_t::CH32_UART2;
  }
#endif
#if defined(UART3)
  else if (addr == UART3)
  {  // NOLINT
    return ch32_uart_id_t::CH32_UART3;
  }
#endif
#if defined(UART4)
  else if (addr == UART4)
  {  // NOLINT
    return ch32_uart_id_t::CH32_UART4;
  }
#endif
#if defined(UART5)
  else if (addr == UART5)
  {  // NOLINT
    return ch32_uart_id_t::CH32_UART5;
  }
#endif
#if defined(UART6)
  else if (addr == UART6)
  {  // NOLINT
    return ch32_uart_id_t::CH32_UART6;
  }
#endif
#if defined(UART7)
  else if (addr == UART7)
  {  // NOLINT
    return ch32_uart_id_t::CH32_UART7;
  }
#endif
#if defined(UART8)
  else if (addr == UART8)
  {  // NOLINT
    return ch32_uart_id_t::CH32_UART8;
  }
#endif
  else
  {
    return ch32_uart_id_t::CH32_UART_ID_ERROR;
  }
}

USART_TypeDef *CH32_UART_GetInstanceID(ch32_uart_id_t id)
{
  switch (id)
  {
#if defined(USART1)
    case CH32_USART1:
      return USART1;
#endif
#if defined(USART2)
    case CH32_USART2:
      return USART2;
#endif
#if defined(USART3)
    case CH32_USART3:
      return USART3;
#endif
#if defined(USART4)
    case CH32_USART4:
      return USART4;
#endif
#if defined(USART5)
    case CH32_USART5:
      return USART5;
#endif
#if defined(USART6)
    case CH32_USART6:
      return USART6;
#endif
#if defined(USART7)
    case CH32_USART7:
      return USART7;
#endif
#if defined(USART8)
    case CH32_USART8:
      return USART8;
#endif
#if defined(UART1)
    case CH32_UART1:
      return UART1;
#endif
#if defined(UART2)
    case CH32_UART2:
      return UART2;
#endif
#if defined(UART3)
    case CH32_UART3:
      return UART3;
#endif
#if defined(UART4)
    case CH32_UART4:
      return UART4;
#endif
#if defined(UART5)
    case CH32_UART5:
      return UART5;
#endif
#if defined(UART6)
    case CH32_UART6:
      return UART6;
#endif
#if defined(UART7)
    case CH32_UART7:
      return UART7;
#endif
#if defined(UART8)
    case CH32_UART8:
      return UART8;
#endif
    default:
      return nullptr;
  }
}