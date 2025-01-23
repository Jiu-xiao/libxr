#include "main.h"

typedef enum {
#ifdef USART1
  STM32_USART1,
#endif
#ifdef USART2
  STM32_USART2,
#endif
#ifdef USART3
  STM32_USART3,
#endif
#ifdef USART4
  STM32_USART4,
#endif
#ifdef USART5
  STM32_USART5,
#endif
#ifdef USART6
  STM32_USART6,
#endif
#ifdef USART7
  STM32_USART7,
#endif
#ifdef USART8
  STM32_USART8,
#endif
#ifdef USART9
  STM32_USART9,
#endif
#ifdef USART10
  STM32_USART10,
#endif
#ifdef USART11
  STM32_USART11,
#endif
#ifdef USART12
  STM32_USART12,
#endif
#ifdef USART13
  STM32_USART13,
#endif
#ifdef UART1
  STM32_UART1,
#endif
#ifdef UART2
  STM32_UART2,
#endif
#ifdef UART3
  STM32_UART3,
#endif
#ifdef UART4
  STM32_UART4,
#endif
#ifdef UART5
  STM32_UART5,
#endif
#ifdef UART6
  STM32_UART6,
#endif
#ifdef UART7
  STM32_UART7,
#endif
#ifdef UART8
  STM32_UART8,
#endif
#ifdef UART9
  STM32_UART9,
#endif
#ifdef UART10
  STM32_UART10,
#endif
#ifdef UART11
  STM32_UART11,
#endif
#ifdef UART12
  STM32_UART12,
#endif
#ifdef UART13
  STM32_UART13,
#endif
#ifdef LPUART1
  STM32_LPUART1,
#endif
#ifdef LPUART2
  STM32_LPUART2,
#endif
#ifdef LPUART3
  STM32_LPUART3,
#endif
  STM32_UART_NUMBER,
  STM32_UART_ID_ERROR
} stm32_uart_id_t;

void STM32_UART_ISR_Handler_IDLE(void *uart_handle);

stm32_uart_id_t STM32_UART_GetID(USART_TypeDef *addr);
