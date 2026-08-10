#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "mspm0_uart.hpp"

#if defined(MSPM0_CASE_POSITIVE_TX_BUFFER_MAIN_G3507)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH3, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_POSITIVE_TX_BUFFER_MAIN_G3519)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH4, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_POSITIVE_EXTEND_G3507)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::array<unsigned char, 8U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 17U));

#elif defined(MSPM0_CASE_POSITIVE_EXTEND_RX_C_ARRAY)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 17U));

#elif defined(MSPM0_CASE_POSITIVE_EXTEND_RX_STD_ARRAY)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::array<std::byte, 8U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 17U));

#elif defined(MSPM0_CASE_POSITIVE_EXTEND_UART7)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::array<unsigned char, 8U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_7, DMA_CH2, DMA_CH3, tx_storage, 4U,
                                             rx_storage, 17U));

#elif defined(MSPM0_CASE_POSITIVE_LOGICAL_NAME_PHYSICAL_UART7)

#define DEBUG_UART_INST UART7
#define DEBUG_UART_INST_INT_IRQN UART7_INT_IRQn
#define DEBUG_UART_INST_FREQUENCY 32000000U
#define DEBUG_UART_BAUD_RATE 115200U
#define DEBUG_UART_LIBXR_EXTEND_CAPABLE 1
static_assert(LIBXR_MSPM0_UART_DMA_TRIGGER(DEBUG_UART_INST, TX) == DMA_UART7_TX_TRIG);
static_assert(LIBXR_MSPM0_UART_DMA_TRIGGER(DEBUG_UART_INST, RX) == DMA_UART7_RX_TRIG);
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::array<unsigned char, 8U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(DEBUG_UART, DMA_CH2, DMA_CH3, tx_storage, 4U,
                                             rx_storage, 17U));

#elif defined(MSPM0_CASE_NEGATIVE_TX_PLAIN_C_ARRAY)

alignas(size_t) unsigned char tx_storage[32U]{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH3, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_TX_PLAIN_STD_ARRAY)

alignas(size_t) std::array<unsigned char, 32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH3, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_TX_POINTER)

LibXR::MSPM0UARTTxBuffer<32U> tx_array{};
auto* tx_storage = &tx_array;
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH3, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_TX_CONST)

const LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH3, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_TX_STRUCT)

struct TxByteWrapper
{
  unsigned char value;
};
TxByteWrapper tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH3, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_TX_ZERO)

LibXR::MSPM0UARTTxBuffer<0U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH3, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_TX_ODD)

LibXR::MSPM0UARTTxBuffer<17U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH3, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_TX_OVERSIZE)

LibXR::MSPM0UARTTxBuffer<131072U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH3, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_POINTER)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_array[8U]{};
unsigned char* rx_storage = rx_array;
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_CONST)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
const unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_VOLATILE)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
volatile unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_CONST_STD_ARRAY)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
const std::array<unsigned char, 8U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_VOLATILE_STD_ARRAY)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
volatile std::array<unsigned char, 8U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_MULTIDIMENSIONAL)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_storage[2U][4U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_WIDE_ELEMENT)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::array<uint16_t, 8U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_ZERO)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::array<unsigned char, 0U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_ODD)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::array<unsigned char, 7U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_OVERSIZE)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::array<unsigned char, 65536U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_SPAN)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_array[8U]{};
std::span<unsigned char> rx_storage{rx_array};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_VECTOR)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::vector<unsigned char> rx_storage(8U);
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_STRING)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::string rx_storage(8U, '\0');
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_ENUM)

enum class RxByte : uint8_t
{
  ZERO,
};
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::array<RxByte, 8U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_RX_STRUCT)

struct RxByteWrapper
{
  unsigned char value;
};
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
std::array<RxByteWrapper, 8U> rx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_BINDING_WRONG_DIRECTION)

#define BAD_TX_CHAN_ID 2U
#define BAD_TX_LIBXR_UART_IRQN UART1_INT_IRQn
#define BAD_TX_LIBXR_UART_TX 0
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, BAD_TX, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_BINDING_WRONG_TX_OWNER)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH5, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_BINDING_WRONG_RX_OWNER)

#define BAD_RX_CHAN_ID 1U
#define BAD_RX_LIBXR_UART_IRQN UART7_INT_IRQn
#define BAD_RX_LIBXR_UART_RX 1
#define BAD_RX_LIBXR_FULL_CHANNEL 1
#define BAD_RX_LIBXR_HALF_INTERRUPT 0
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, BAD_RX, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_BINDING_SAME_CHANNEL)

#define SAME_RX_CHAN_ID DMA_CH0_CHAN_ID
#define SAME_RX_LIBXR_UART_IRQN UART0_INT_IRQn
#define SAME_RX_LIBXR_UART_RX 1
#define SAME_RX_LIBXR_FULL_CHANNEL 1
#define SAME_RX_LIBXR_HALF_INTERRUPT 0
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, SAME_RX, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_BINDING_G3519_UART1_EXTEND)

#define UART1_RX_CHAN_ID 0U
#define UART1_RX_LIBXR_UART_IRQN UART1_INT_IRQn
#define UART1_RX_LIBXR_UART_RX 1
#define UART1_RX_LIBXR_FULL_CHANNEL 1
#define UART1_RX_LIBXR_HALF_INTERRUPT 0
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_1, DMA_CH4, UART1_RX, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_BINDING_FULL_MARKER_FALSE)

#define MARKER_RX_CHAN_ID DMA_CH1_CHAN_ID
#define MARKER_RX_LIBXR_UART_IRQN UART0_INT_IRQn
#define MARKER_RX_LIBXR_UART_RX 1
#define MARKER_RX_LIBXR_FULL_CHANNEL 0
#define MARKER_RX_LIBXR_HALF_INTERRUPT 0
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, MARKER_RX, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_BINDING_BASIC_RX_CHANNEL)

#define BASIC_RX_CHAN_ID DMA_CH6_CHAN_ID
#define BASIC_RX_LIBXR_UART_IRQN UART0_INT_IRQn
#define BASIC_RX_LIBXR_UART_RX 1
#define BASIC_RX_LIBXR_FULL_CHANNEL 1
#define BASIC_RX_LIBXR_HALF_INTERRUPT 0
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, BASIC_RX, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_BINDING_HALF_UNSUPPORTED)

#define HALF_RX_CHAN_ID DMA_CH1_CHAN_ID
#define HALF_RX_LIBXR_UART_IRQN UART0_INT_IRQn
#define HALF_RX_LIBXR_UART_RX 1
#define HALF_RX_LIBXR_FULL_CHANNEL 1
#define HALF_RX_LIBXR_HALF_INTERRUPT 1
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, HALF_RX, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_BINDING_MISSING_DISPATCHER)

#undef LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE
#define LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE 0
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 4U,
                                             rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_MAIN_BINDING_MISSING_DISPATCHER)

#undef LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE
#define LIBXR_MSPM0_DMA_DISPATCHER_AVAILABLE 0
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH4, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_BINDING_MISSING_DMA)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_MISSING, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_MAIN_G3507_UART0)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_0, DMA_CH0, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_EXTEND_G3507_UART1)

#define G3507_UART1_RX_CHAN_ID 2U
#define G3507_UART1_RX_LIBXR_UART_IRQN UART1_INT_IRQn
#define G3507_UART1_RX_LIBXR_UART_RX 1
#define G3507_UART1_RX_LIBXR_FULL_CHANNEL 1
#define G3507_UART1_RX_LIBXR_HALF_INTERRUPT 0
LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
unsigned char rx_storage[8U]{};
LibXR::MSPM0UART uart(MSPM0_UART_EXTEND_INIT(UART_1, DMA_CH3, G3507_UART1_RX, tx_storage,
                                             4U, rx_storage, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_MAIN_G3519_UART0)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_0, DMA_CH0, tx_storage, 4U, 8U));

#elif defined(MSPM0_CASE_NEGATIVE_MAIN_G3519_UART7)

LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
LibXR::MSPM0UART uart(MSPM0_UART_MAIN_INIT(UART_7, DMA_CH2, tx_storage, 4U, 8U));

#else
#error "Select exactly one MSPM0 compile-contract case"
#endif
