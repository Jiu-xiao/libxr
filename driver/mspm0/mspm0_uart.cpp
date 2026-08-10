#include "mspm0_uart.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

using namespace LibXR;

namespace
{

constexpr uint32_t MSPM0_UART_RX_ERROR_INTERRUPT_MASK =
    DL_UART_INTERRUPT_OVERRUN_ERROR | DL_UART_INTERRUPT_BREAK_ERROR |
    DL_UART_INTERRUPT_PARITY_ERROR | DL_UART_INTERRUPT_FRAMING_ERROR |
    DL_UART_INTERRUPT_NOISE_ERROR;

constexpr uint32_t MSPM0_UART_RX_INTERRUPT_MASK = DL_UART_INTERRUPT_RX;
constexpr uint32_t MSPM0_UART_RX_GAP_INTERRUPT_MASK = DL_UART_INTERRUPT_LINC0_MATCH;
constexpr uint32_t MSPM0_UART_CONTROL_INTERRUPT_MASK = DL_UART_INTERRUPT_EOT_DONE;
constexpr uint32_t MSPM0_UART_TX_INTERRUPT_MASK = DL_UART_INTERRUPT_DMA_DONE_TX;
constexpr size_t MSPM0_UART_DMA_MAX_TRANSFER_SIZE = 0xFFFFU;
constexpr uint64_t MSPM0_UART_BAUD_DIVISOR_MIN = 1ULL << 6U;
constexpr uint64_t MSPM0_UART_BAUD_DIVISOR_MAX = 0xFFFFULL << 6U;
constexpr uint64_t MSPM0_UART_UINT32_MAX = 0xFFFFFFFFULL;

constexpr DL_DMA_Config MSPM0_UART_DMA_TX_CONFIG = {
    .trigger = 0U,
    .triggerType = DL_DMA_TRIGGER_TYPE_EXTERNAL,
    .transferMode = DL_DMA_SINGLE_TRANSFER_MODE,
    .extendedMode = DL_DMA_NORMAL_MODE,
    .srcWidth = DL_DMA_WIDTH_BYTE,
    .destWidth = DL_DMA_WIDTH_BYTE,
    .srcIncrement = DL_DMA_ADDR_INCREMENT,
    .destIncrement = DL_DMA_ADDR_UNCHANGED,
};

constexpr DL_DMA_Config MSPM0_UART_DMA_RX_CONFIG = {
    .trigger = 0U,
    .triggerType = DL_DMA_TRIGGER_TYPE_EXTERNAL,
    .transferMode = DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE,
    .extendedMode = DL_DMA_NORMAL_MODE,
    .srcWidth = DL_DMA_WIDTH_BYTE,
    .destWidth = DL_DMA_WIDTH_BYTE,
    .srcIncrement = DL_DMA_ADDR_UNCHANGED,
    .destIncrement = DL_DMA_ADDR_INCREMENT,
};

bool MSPM0UARTBaudRateIsRepresentable(uint32_t clock_freq, uint32_t baudrate)
{
  if (baudrate == 0U || baudrate > (MSPM0_UART_UINT32_MAX / 16U))
  {
    return false;
  }

  const uint64_t clock = clock_freq;
  const uint64_t baud = baudrate;
  uint64_t divisor = 0U;
  if ((baud * 8U) > clock)
  {
    if (clock > (MSPM0_UART_UINT32_MAX / 64U))
    {
      return false;
    }
    divisor = (clock * 64U) / (baud * 3U);
  }
  else if ((baud * 16U) > clock)
  {
    if (clock > (MSPM0_UART_UINT32_MAX / 8U))
    {
      return false;
    }
    const uint64_t halved_baud = baud / 2U;
    if (halved_baud == 0U)
    {
      return false;
    }
    divisor = (((clock * 8U) / halved_baud) + 1U) / 2U;
  }
  else
  {
    if (clock > (MSPM0_UART_UINT32_MAX / 8U))
    {
      return false;
    }
    divisor = (((clock * 8U) / baud) + 1U) / 2U;
  }

  return divisor >= MSPM0_UART_BAUD_DIVISOR_MIN && divisor <= MSPM0_UART_BAUD_DIVISOR_MAX;
}

constexpr uint64_t CalculateMSPM0UartGapCompare(uint32_t clock_freq,
                                                UART::Configuration config)
{
  if (clock_freq == 0U || config.baudrate == 0U)
  {
    return 0U;
  }

  // LINC0 resets on RXD falling edges. Cover the worst trailing-high run plus one frame;
  // this is a data-dependent conservative flush threshold, not a UART IDLE detector.
  const uint64_t parity_bits = config.parity == UART::Parity::NO_PARITY ? 0U : 1U;
  const uint64_t max_high_bits = config.data_bits + parity_bits + config.stop_bits;
  const uint64_t gap_bits = max_high_bits + (1U + max_high_bits);
  const uint64_t numerator = static_cast<uint64_t>(clock_freq) * gap_bits;
  const uint64_t baudrate = config.baudrate;
  return (numerator / baudrate) + ((numerator % baudrate) != 0U ? 1U : 0U);
}

constexpr uint32_t DispatcherEvent(MSPM0DmaDispatcher::Event event)
{
  return MSPM0DmaDispatcher::EventMask(event);
}

}  // namespace

MSPM0UART* MSPM0UART::instance_map_[MAX_UART_INSTANCES] = {nullptr};

MSPM0UART::MSPM0UART(Resources res, RawData tx_dma_storage, RawData rx_dma_storage,
                     uint32_t tx_queue_size, uint32_t rx_queue_capacity,
                     UART::Configuration config)
    : UART(&_read_port, &_write_port),
      _read_port(rx_queue_capacity),
      _write_port(tx_queue_size, tx_dma_storage.size_ / 2U),
      res_(res),
      rx_dma_storage_(rx_dma_storage),
      execution_policy_(res.irqn),
      dma_model_(*this, execution_policy_, _write_port, tx_dma_storage),
      active_config_(config)
{
  REQUIRE(res_.instance != nullptr);
  REQUIRE(res_.clock_freq > 0U);
  REQUIRE(res_.index < MAX_UART_INSTANCES);
  REQUIRE(res_.index == ResolveIndex(res_.irqn));
  REQUIRE(instance_map_[res_.index] == nullptr);
  REQUIRE(tx_dma_storage.addr_ != nullptr);
  REQUIRE(tx_dma_storage.size_ > 1U);
  REQUIRE((tx_dma_storage.size_ % 2U) == 0U);
  REQUIRE((reinterpret_cast<uintptr_t>(tx_dma_storage.addr_) % alignof(size_t)) == 0U);
  REQUIRE((tx_dma_storage.size_ % (2U * alignof(size_t))) == 0U);
  REQUIRE((tx_dma_storage.size_ / 2U) <= MSPM0_UART_DMA_MAX_TRANSFER_SIZE);
  REQUIRE(tx_queue_size > 0U);
  REQUIRE(rx_queue_capacity > 0U);

  if (res_.rx_mode == RxMode::EXTEND_DMA)
  {
    REQUIRE(rx_dma_storage_.addr_ != nullptr);
    REQUIRE(rx_dma_storage_.size_ > 1U);
    REQUIRE((rx_dma_storage_.size_ % 2U) == 0U);
    REQUIRE(rx_dma_storage_.size_ <= MSPM0_UART_DMA_MAX_TRANSFER_SIZE);
    rx_dma_half_size_ = rx_dma_storage_.size_ / 2U;
  }
  else
  {
    REQUIRE(rx_dma_storage_.addr_ == nullptr);
    REQUIRE(rx_dma_storage_.size_ == 0U);
  }

  ValidateResources();
  REQUIRE(ValidateConfig(config) == ErrorCode::OK);
  _write_port = WriteFun;

  NVIC_DisableIRQ(res_.irqn);
  NVIC_ClearPendingIRQ(res_.irqn);

  const uint32_t dma_error_event = DispatcherEvent(MSPM0DmaDispatcher::Event::ERROR);
  REQUIRE(MSPM0DmaDispatcher::Register(res_.dma_tx_channel, dma_error_event,
                                       TxDmaCallback, this,
                                       tx_dma_registration_) == ErrorCode::OK);
  registered_tx_dma_ = true;

  if (res_.rx_mode == RxMode::EXTEND_DMA)
  {
    const uint32_t rx_events = RxDmaEventMask();
    REQUIRE(MSPM0DmaDispatcher::Register(res_.dma_rx_channel, rx_events, RxDmaCallback,
                                         this, rx_dma_registration_) == ErrorCode::OK);
    registered_rx_dma_ = true;
  }

  ApplyInitialConfig(config);
  ConfigureTxDma();
  instance_map_[res_.index] = this;
  StartDataPath();

  NVIC_EnableIRQ(res_.irqn);
}

MSPM0UART::~MSPM0UART()
{
  if (res_.index >= MAX_UART_INSTANCES || instance_map_[res_.index] != this)
  {
    return;
  }

  NVIC_DisableIRQ(res_.irqn);
  NVIC_ClearPendingIRQ(res_.irqn);
  DL_UART_disableInterrupt(res_.instance, 0xFFFFFFFFU);
  DL_UART_disableDMAReceiveEvent(res_.instance, DL_UART_DMA_INTERRUPT_RX);
  DL_UART_disableDMATransmitEvent(res_.instance);
  DL_DMA_disableChannel(DMA, res_.dma_tx_channel);

  if (registered_rx_dma_)
  {
    const uint32_t rx_events = RxDmaEventMask();
    MSPM0DmaDispatcher::SetEnabled(rx_dma_registration_, rx_events, false);
    DL_DMA_disableChannel(DMA, res_.dma_rx_channel);
    (void)MSPM0DmaDispatcher::Unregister(rx_dma_registration_);
    registered_rx_dma_ = false;
  }

  if (registered_tx_dma_)
  {
    MSPM0DmaDispatcher::SetEnabled(
        tx_dma_registration_, DispatcherEvent(MSPM0DmaDispatcher::Event::ERROR), false);
    (void)MSPM0DmaDispatcher::Unregister(tx_dma_registration_);
    registered_tx_dma_ = false;
  }

  DL_DMA_clearInterruptStatus(DMA, MSPM0DmaDispatcher::CompleteMask(res_.dma_tx_channel));
  DL_UART_clearDMATransmitEventStatus(res_.instance);
  DL_UART_clearDMAReceiveEventStatus(res_.instance, DL_UART_DMA_INTERRUPT_RX);
  DL_UART_clearInterruptStatus(res_.instance, 0xFFFFFFFFU);
  DL_UART_disable(res_.instance);
  __DMB();
  instance_map_[res_.index] = nullptr;
}

ErrorCode MSPM0UART::SetConfig(UART::Configuration config)
{
  return dma_model_.SetConfig(config, InIsr());
}

ErrorCode MSPM0UART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &MSPM0UART::_write_port);
  return uart->dma_model_.Submit(in_isr);
}

bool MSPM0UART::InIsr() { return __get_IPSR() != 0U; }

UART::Configuration MSPM0UART::BuildConfigFromSysCfg(UART_Regs* instance,
                                                     uint32_t baudrate)
{
  ASSERT(instance != nullptr);
  ASSERT(baudrate > 0U);

  UART::Configuration config = {baudrate, UART::Parity::NO_PARITY, 8U, 1U};
  switch (DL_UART_getWordLength(instance))
  {
    case DL_UART_WORD_LENGTH_5_BITS:
      config.data_bits = 5U;
      break;
    case DL_UART_WORD_LENGTH_6_BITS:
      config.data_bits = 6U;
      break;
    case DL_UART_WORD_LENGTH_7_BITS:
      config.data_bits = 7U;
      break;
    case DL_UART_WORD_LENGTH_8_BITS:
    default:
      config.data_bits = 8U;
      break;
  }

  switch (DL_UART_getParityMode(instance))
  {
    case DL_UART_PARITY_NONE:
      config.parity = UART::Parity::NO_PARITY;
      break;
    case DL_UART_PARITY_EVEN:
      config.parity = UART::Parity::EVEN;
      break;
    case DL_UART_PARITY_ODD:
      config.parity = UART::Parity::ODD;
      break;
    default:
      ASSERT(false);
      break;
  }
  config.stop_bits = DL_UART_getStopBits(instance) == DL_UART_STOP_BITS_TWO ? 2U : 1U;
  return config;
}

ErrorCode MSPM0UART::ValidateConfig(UART::Configuration config) const
{
  if (!MSPM0UARTBaudRateIsRepresentable(res_.clock_freq, config.baudrate) ||
      config.data_bits < 5U || config.data_bits > 8U ||
      (config.stop_bits != 1U && config.stop_bits != 2U))
  {
    return ErrorCode::ARG_ERR;
  }
  if (config.parity != UART::Parity::NO_PARITY && config.parity != UART::Parity::EVEN &&
      config.parity != UART::Parity::ODD)
  {
    return ErrorCode::ARG_ERR;
  }
  if (res_.rx_mode == RxMode::EXTEND_DMA)
  {
    const uint64_t gap_compare = CalculateMSPM0UartGapCompare(res_.clock_freq, config);
    if (gap_compare == 0U || gap_compare > std::numeric_limits<uint16_t>::max())
    {
      return ErrorCode::ARG_ERR;
    }
  }
  return ErrorCode::OK;
}

void MSPM0UART::ValidateResources() const
{
  REQUIRE(res_.dma_tx_channel < DMA_SYS_N_DMA_CHANNEL);
  REQUIRE(res_.dma_tx_trigger != 0U);

  if (res_.rx_mode == RxMode::EXTEND_DMA)
  {
    REQUIRE(res_.dma_rx_channel < DMA_SYS_N_DMA_FULL_CHANNEL);
    REQUIRE(res_.dma_rx_channel != res_.dma_tx_channel);
    REQUIRE(res_.dma_rx_trigger != 0U);
    REQUIRE(res_.dma_rx_trigger != res_.dma_tx_trigger);
    REQUIRE(!res_.rx_half_interrupt ||
            MSPM0DmaDispatcher::EarlyInterruptSupported(res_.dma_rx_channel));
  }
  else
  {
    REQUIRE(!res_.rx_half_interrupt);
  }

  for (const auto* other : instance_map_)
  {
    if (other == nullptr)
    {
      continue;
    }
    REQUIRE(res_.dma_tx_channel != other->res_.dma_tx_channel);
    if (other->res_.rx_mode == RxMode::EXTEND_DMA)
    {
      REQUIRE(res_.dma_tx_channel != other->res_.dma_rx_channel);
    }
    if (res_.rx_mode == RxMode::EXTEND_DMA)
    {
      REQUIRE(res_.dma_rx_channel != other->res_.dma_tx_channel);
      if (other->res_.rx_mode == RxMode::EXTEND_DMA)
      {
        REQUIRE(res_.dma_rx_channel != other->res_.dma_rx_channel);
      }
    }
  }
}

void MSPM0UART::ApplyInitialConfig(UART::Configuration config)
{
  DL_UART_changeConfig(res_.instance);
  ApplyDisabledConfig(config);
}

void MSPM0UART::ApplyDisabledConfig(UART::Configuration config)
{
  DL_UART_WORD_LENGTH word_length = DL_UART_WORD_LENGTH_8_BITS;
  switch (config.data_bits)
  {
    case 5U:
      word_length = DL_UART_WORD_LENGTH_5_BITS;
      break;
    case 6U:
      word_length = DL_UART_WORD_LENGTH_6_BITS;
      break;
    case 7U:
      word_length = DL_UART_WORD_LENGTH_7_BITS;
      break;
    case 8U:
      break;
    default:
      ASSERT(false);
      break;
  }

  DL_UART_PARITY parity = DL_UART_PARITY_NONE;
  if (config.parity == UART::Parity::EVEN)
  {
    parity = DL_UART_PARITY_EVEN;
  }
  else if (config.parity == UART::Parity::ODD)
  {
    parity = DL_UART_PARITY_ODD;
  }

  DL_UART_setWordLength(res_.instance, word_length);
  DL_UART_setParityMode(res_.instance, parity);
  DL_UART_setStopBits(res_.instance, config.stop_bits == 2U ? DL_UART_STOP_BITS_TWO
                                                            : DL_UART_STOP_BITS_ONE);
  DL_UART_setCommunicationMode(res_.instance, DL_UART_MODE_NORMAL);
  DL_UART_setDirection(res_.instance, DL_UART_DIRECTION_TX_RX);
  DL_UART_enableFIFOs(res_.instance);
  DL_UART_setTXFIFOThreshold(res_.instance, DL_UART_TX_FIFO_LEVEL_ONE_ENTRY);
  DL_UART_setRXFIFOThreshold(res_.instance, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
  DL_UART_setRXInterruptTimeout(res_.instance, 0U);
  DL_UART_configBaudRate(res_.instance, res_.clock_freq, config.baudrate);
  if (res_.rx_mode == RxMode::EXTEND_DMA)
  {
    const uint64_t gap_compare = CalculateMSPM0UartGapCompare(res_.clock_freq, config);
    ASSERT(gap_compare > 0U && gap_compare <= std::numeric_limits<uint16_t>::max());
    DL_UART_setLINCounterValue(res_.instance, 0U);
    DL_UART_setLINCounterCompareValue(res_.instance, static_cast<uint16_t>(gap_compare));
    DL_UART_enableLINCounterCompareMatch(res_.instance);
    DL_UART_disableLINCountWhileLow(res_.instance);
    DL_UART_enableLINCounterClearOnFallingEdge(res_.instance);
    DL_UART_enableLINCounter(res_.instance);
  }
  active_config_ = config;
}

void MSPM0UART::ConfigureTxDma()
{
  DL_DMA_Config config = MSPM0_UART_DMA_TX_CONFIG;
  config.trigger = res_.dma_tx_trigger;
  DL_DMA_disableChannel(DMA, res_.dma_tx_channel);
  DL_DMA_clearInterruptStatus(DMA, MSPM0DmaDispatcher::CompleteMask(res_.dma_tx_channel));
  DL_DMA_initChannel(DMA, res_.dma_tx_channel, &config);
}

void MSPM0UART::ConfigureRxDma()
{
  ASSERT(res_.rx_mode == RxMode::EXTEND_DMA);
  DL_DMA_Config config = MSPM0_UART_DMA_RX_CONFIG;
  config.trigger = res_.dma_rx_trigger;
  DL_DMA_disableChannel(DMA, res_.dma_rx_channel);
  DL_DMA_initChannel(DMA, res_.dma_rx_channel, &config);
  DL_DMA_setSrcAddr(
      DMA, res_.dma_rx_channel,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&res_.instance->RXDATA)));
  DL_DMA_setDestAddr(
      DMA, res_.dma_rx_channel,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rx_dma_storage_.addr_)));
  DL_DMA_setTransferSize(DMA, res_.dma_rx_channel,
                         static_cast<uint16_t>(rx_dma_storage_.size_));
  if (MSPM0DmaDispatcher::EarlyInterruptSupported(res_.dma_rx_channel))
  {
    DL_DMA_Full_Ch_setEarlyInterruptThreshold(
        DMA, res_.dma_rx_channel,
        res_.rx_half_interrupt ? DL_DMA_EARLY_INTERRUPT_THRESHOLD_HALF
                               : DL_DMA_EARLY_INTERRUPT_THRESHOLD_DISABLED);
  }
}

uint32_t MSPM0UART::RxDmaEventMask() const
{
  uint32_t events = DispatcherEvent(MSPM0DmaDispatcher::Event::COMPLETE);
  if (res_.rx_half_interrupt)
  {
    events |= DispatcherEvent(MSPM0DmaDispatcher::Event::EARLY);
  }
  return events;
}

uint32_t MSPM0UART::RxDmaBoundaryRawMask() const
{
  uint32_t mask = MSPM0DmaDispatcher::CompleteMask(res_.dma_rx_channel);
  if (res_.rx_half_interrupt)
  {
    mask |= MSPM0DmaDispatcher::EarlyMask(res_.dma_rx_channel);
  }
  return mask;
}

void MSPM0UART::StartDataPath()
{
  DL_UART_disableInterrupt(res_.instance, 0xFFFFFFFFU);
  DL_UART_clearInterruptStatus(res_.instance, 0xFFFFFFFFU);
  DL_UART_clearDMATransmitEventStatus(res_.instance);
  DL_UART_clearDMAReceiveEventStatus(res_.instance, DL_UART_DMA_INTERRUPT_RX);
  DL_UART_disableDMAReceiveEvent(res_.instance, DL_UART_DMA_INTERRUPT_RX);
  DL_UART_enableDMATransmitEvent(res_.instance);

  MSPM0DmaDispatcher::SetEnabled(tx_dma_registration_,
                                 DispatcherEvent(MSPM0DmaDispatcher::Event::ERROR), true);
  DL_UART_enableInterrupt(
      res_.instance, MSPM0_UART_TX_INTERRUPT_MASK | MSPM0_UART_RX_ERROR_INTERRUPT_MASK);

  if (res_.rx_mode == RxMode::MAIN_BYTE_IRQ)
  {
    DL_UART_enableInterrupt(res_.instance, MSPM0_UART_RX_INTERRUPT_MASK);
    __DMB();
    DL_UART_enable(res_.instance);
    return;
  }

  DL_UART_disableInterrupt(res_.instance, DL_UART_INTERRUPT_DMA_DONE_RX);
  DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_DMA_DONE_RX);
  DL_UART_setLINCounterValue(res_.instance, 0U);
  DL_UART_clearInterruptStatus(res_.instance, MSPM0_UART_RX_GAP_INTERRUPT_MASK);
  DL_UART_enableInterrupt(res_.instance, MSPM0_UART_RX_GAP_INTERRUPT_MASK);
  StartRxEpoch();
  ConfigureRxDma();
  const uint32_t rx_events = RxDmaEventMask();
  MSPM0DmaDispatcher::SetEnabled(rx_dma_registration_, rx_events, true);
  __DMB();
  DL_DMA_enableChannel(DMA, res_.dma_rx_channel);
  DL_UART_enableDMAReceiveEvent(res_.instance, DL_UART_DMA_INTERRUPT_RX);
  __DMB();
  DL_UART_enable(res_.instance);
}

void MSPM0UART::StopDataPathInterrupts()
{
  DL_UART_disableInterrupt(
      res_.instance,
      MSPM0_UART_RX_INTERRUPT_MASK | MSPM0_UART_RX_ERROR_INTERRUPT_MASK |
          MSPM0_UART_TX_INTERRUPT_MASK | MSPM0_UART_CONTROL_INTERRUPT_MASK |
          MSPM0_UART_RX_GAP_INTERRUPT_MASK | DL_UART_INTERRUPT_DMA_DONE_RX);
  DL_UART_disableDMAReceiveEvent(res_.instance, DL_UART_DMA_INTERRUPT_RX);
  DL_UART_disableDMATransmitEvent(res_.instance);
  MSPM0DmaDispatcher::SetEnabled(
      tx_dma_registration_, DispatcherEvent(MSPM0DmaDispatcher::Event::ERROR), false);
  if (registered_rx_dma_)
  {
    const uint32_t rx_events = RxDmaEventMask();
    MSPM0DmaDispatcher::SetEnabled(rx_dma_registration_, rx_events, false);
  }
}

UartDmaTxStartResult MSPM0UART::StartDmaTx(uint8_t* data, size_t size, int, bool in_isr)
{
  REQUIRE_FROM_CALLBACK(
      data != nullptr && size > 0U && size <= MSPM0_UART_DMA_MAX_TRANSFER_SIZE, in_isr);
  DL_DMA_disableChannel(DMA, res_.dma_tx_channel);
  DL_DMA_clearInterruptStatus(DMA, MSPM0DmaDispatcher::CompleteMask(res_.dma_tx_channel));
  DL_DMA_setSrcAddr(DMA, res_.dma_tx_channel,
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data)));
  DL_DMA_setDestAddr(
      DMA, res_.dma_tx_channel,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&res_.instance->TXDATA)));
  DL_DMA_setTransferSize(DMA, res_.dma_tx_channel, static_cast<uint16_t>(size));
  DL_UART_clearInterruptStatus(
      res_.instance, DL_UART_INTERRUPT_DMA_DONE_TX | DL_UART_INTERRUPT_EOT_DONE);
  DL_UART_enableInterrupt(res_.instance, DL_UART_INTERRUPT_DMA_DONE_TX);
  DL_UART_enableDMATransmitEvent(res_.instance);
  tx_line_active_ = true;
  __DMB();
  DL_DMA_enableChannel(DMA, res_.dma_tx_channel);
  return UartDmaTxStartResult::STARTED;
}

void MSPM0UART::OnInterrupt(uint8_t index)
{
  if (index >= MAX_UART_INSTANCES || instance_map_[index] == nullptr)
  {
    return;
  }
  instance_map_[index]->HandleInterrupt();
}

void MSPM0UART::HandleInterrupt()
{
  rx_pushed_in_owner_ = false;
  (void)dma_model_.InvokeIrq([this]() { return CaptureIrqEvents(); }, true);
  __DMB();
  if (rx_pushed_in_owner_)
  {
    // InvokeIrq has released the service owner; read callbacks may reenter public APIs.
    _read_port.ProcessPendingReads(true);
  }
}

uint32_t MSPM0UART::CaptureIrqEvents()
{
  using Model = UartDmaModel<MSPM0UART, MSPM0UartIrqPolicy>;
  uint32_t events = 0U;
  const uint32_t owned_uart_mask =
      MSPM0_UART_RX_INTERRUPT_MASK | MSPM0_UART_RX_ERROR_INTERRUPT_MASK |
      MSPM0_UART_TX_INTERRUPT_MASK | MSPM0_UART_CONTROL_INTERRUPT_MASK |
      MSPM0_UART_RX_GAP_INTERRUPT_MASK | DL_UART_INTERRUPT_DMA_DONE_RX;
  const uint32_t pending =
      DL_UART_getEnabledInterruptStatus(res_.instance, owned_uart_mask);

  if ((pending & MSPM0_UART_TX_INTERRUPT_MASK) != 0U)
  {
    DL_UART_clearInterruptStatus(res_.instance, MSPM0_UART_TX_INTERRUPT_MASK);
    events |= Model::EventMask(UartDmaEvent::COMPLETE);
  }

  if ((pending & MSPM0_UART_CONTROL_INTERRUPT_MASK) != 0U)
  {
    DL_UART_disableInterrupt(res_.instance, MSPM0_UART_CONTROL_INTERRUPT_MASK);
    events |= Model::EventMask(UartDmaEvent::CONTROL_READY);
  }

  const bool dma_error = dma_error_pending_.exchange(0U, std::memory_order_acquire) != 0U;
  const bool uart_error = (pending & MSPM0_UART_RX_ERROR_INTERRUPT_MASK) != 0U;
  const bool rx_gap = res_.rx_mode == RxMode::EXTEND_DMA &&
                      (pending & MSPM0_UART_RX_GAP_INTERRUPT_MASK) != 0U;
  if (rx_gap)
  {
    DL_UART_clearInterruptStatus(res_.instance, MSPM0_UART_RX_GAP_INTERRUPT_MASK);
    rx_partial_flush_pending_ = true;
  }
  if (dma_error)
  {
    counters_.dma_error_.fetch_add(1U, std::memory_order_relaxed);
    if (res_.rx_mode == RxMode::EXTEND_DMA)
    {
      InvalidateRxEpoch();
    }
    events |= Model::EventMask(UartDmaEvent::ERROR);
  }

  if (res_.rx_mode == RxMode::EXTEND_DMA && uart_error)
  {
    CountUartInterruptErrors(pending);
    DL_UART_clearInterruptStatus(res_.instance,
                                 pending & MSPM0_UART_RX_ERROR_INTERRUPT_MASK);
    if (!dma_error)
    {
      InvalidateRxEpoch();
    }
    events |= Model::EventMask(UartDmaEvent::ERROR);
  }

  if (control_phase_ == ControlPhase::WAIT_UART_IDLE)
  {
    if ((pending & MSPM0_UART_RX_INTERRUPT_MASK) != 0U)
    {
      DL_UART_disableInterrupt(res_.instance, MSPM0_UART_RX_INTERRUPT_MASK);
      DL_UART_clearInterruptStatus(res_.instance, MSPM0_UART_RX_INTERRUPT_MASK);
    }
    events |= Model::EventMask(UartDmaEvent::CONTROL_READY);
  }
  else if (!dma_error && res_.rx_mode == RxMode::MAIN_BYTE_IRQ &&
           (pending &
            (MSPM0_UART_RX_INTERRUPT_MASK | MSPM0_UART_RX_ERROR_INTERRUPT_MASK)) != 0U)
  {
    CaptureMainRx(pending, events);
  }

  if (!dma_error && !uart_error && res_.rx_mode == RxMode::EXTEND_DMA)
  {
    CaptureExtendRx(events);
  }
  else if (res_.rx_mode == RxMode::EXTEND_DMA)
  {
    const uint32_t discarded = rx_dma_facts_.exchange(0U, std::memory_order_acquire);
    if (discarded != 0U)
    {
      counters_.rx_stale_event_.fetch_add(1U, std::memory_order_relaxed);
    }
    rx_partial_flush_pending_ = false;
  }

  return events;
}

void MSPM0UART::CaptureMainRx(uint32_t pending, uint32_t& events)
{
  static_cast<void>(pending);
  DL_UART_disableInterrupt(
      res_.instance, MSPM0_UART_RX_INTERRUPT_MASK | MSPM0_UART_RX_ERROR_INTERRUPT_MASK);

  const bool admitted =
      dma_model_.ProcessRxInIrqSource(events, [this]() { DrainMainRx(); });
  if (admitted && control_phase_ == ControlPhase::IDLE)
  {
    DL_UART_enableInterrupt(
        res_.instance, MSPM0_UART_RX_INTERRUPT_MASK | MSPM0_UART_RX_ERROR_INTERRUPT_MASK);
  }
}

void MSPM0UART::CaptureExtendRx(uint32_t& events)
{
  using Model = UartDmaModel<MSPM0UART, MSPM0UartIrqPolicy>;
  const uint32_t tagged = rx_dma_facts_.exchange(0U, std::memory_order_acquire);
  if (tagged == 0U && !rx_partial_flush_pending_)
  {
    return;
  }

  uint32_t facts = 0U;
  if (tagged != 0U)
  {
    const uint32_t event_epoch = tagged >> RX_DMA_EPOCH_SHIFT;
    facts = tagged & RX_DMA_FACT_MASK;
    const uint32_t current_epoch = rx_epoch_.load(std::memory_order_acquire);
    if (event_epoch != current_epoch || rx_epoch_invalid_)
    {
      counters_.rx_stale_event_.fetch_add(1U, std::memory_order_relaxed);
      rx_partial_flush_pending_ = false;
      return;
    }
  }

  bool invalid = false;
  const bool admitted =
      dma_model_.ProcessRxInIrqSource(events,
                                      [this, facts, &invalid]()
                                      {
                                        invalid = facts != 0U && ConsumeRxDmaFacts(facts);
                                        if (!invalid && rx_partial_flush_pending_)
                                        {
                                          invalid = FlushPartialRx();
                                        }
                                      });
  if (!admitted)
  {
    counters_.rx_stale_event_.fetch_add(1U, std::memory_order_relaxed);
    rx_partial_flush_pending_ = false;
  }
  else if (invalid)
  {
    events |= Model::EventMask(UartDmaEvent::ERROR);
  }
}

void MSPM0UART::DrainMainRx()
{
  uint32_t dropped = 0U;
  bool loss = false;
  bool discard_uncertain_words = false;

  while (true)
  {
    uint32_t word_error_categories = 0U;
    while (!DL_UART_isRXFIFOEmpty(res_.instance))
    {
      const uint32_t word = res_.instance->RXDATA;
      const uint32_t word_errors = RxWordInterruptErrors(word);
      word_error_categories |= word_errors;
      CountUartInterruptErrors(word_errors);
      const uint32_t rejecting_errors =
          DL_UART_INTERRUPT_FRAMING_ERROR | DL_UART_INTERRUPT_PARITY_ERROR |
          DL_UART_INTERRUPT_BREAK_ERROR | DL_UART_INTERRUPT_NOISE_ERROR;
      if ((word_errors & DL_UART_INTERRUPT_OVERRUN_ERROR) != 0U)
      {
        loss = true;
      }
      if (discard_uncertain_words || (word_errors & rejecting_errors) != 0U)
      {
        ++dropped;
        loss = true;
        continue;
      }

      const uint8_t byte = static_cast<uint8_t>(word & UART_RXDATA_DATA_MASK);
      if (_read_port.queue_data_->Push(byte) == ErrorCode::OK)
      {
        rx_pushed_in_owner_ = true;
      }
      else
      {
        ++dropped;
        loss = true;
      }
    }

    const uint32_t raw_errors =
        DL_UART_getRawInterruptStatus(res_.instance, MSPM0_UART_RX_ERROR_INTERRUPT_MASK);
    CountUartInterruptErrors(raw_errors & ~word_error_categories);
    if (raw_errors != 0U)
    {
      loss = true;
    }
    DL_UART_clearInterruptStatus(res_.instance,
                                 MSPM0_UART_RX_INTERRUPT_MASK | raw_errors);
    __DMB();
    if (DL_UART_isRXFIFOEmpty(res_.instance))
    {
      break;
    }

    // An error ICLR can erase matching status on a byte that arrived between the raw
    // snapshot and acknowledgement. Preserve integrity by dropping that whole window.
    discard_uncertain_words = raw_errors != 0U;
  }

  if (dropped != 0U)
  {
    counters_.rx_drop_.fetch_add(dropped, std::memory_order_relaxed);
  }
  if (loss)
  {
    counters_.rx_loss_generation_.fetch_add(1U, std::memory_order_relaxed);
  }
}

bool MSPM0UART::ConsumeRxDmaFacts(uint32_t facts)
{
  const bool half_phase = rx_dma_phase_ == RxDmaPhase::HALF;
  const uint32_t expected =
      half_phase ? FactMask(RxDmaFact::HALF) : FactMask(RxDmaFact::FULL);
  // DMASZ, raw boundary state, and newly queued broker facts must all agree before a
  // repeated-DMA half-ring is treated as stable and copied.
  const uint16_t remaining = DL_DMA_getTransferSize(DMA, res_.dma_rx_channel);
  __DMB();
  const uint32_t raw_boundaries =
      DL_DMA_getRawInterruptStatus(DMA, RxDmaBoundaryRawMask());
  const uint32_t queued_boundaries = rx_dma_facts_.load(std::memory_order_acquire);
  const bool remaining_valid = remaining > 0U && remaining <= rx_dma_storage_.size_;
  const size_t position =
      remaining_valid ? rx_dma_storage_.size_ - remaining : rx_dma_storage_.size_;
  const bool stable_phase =
      half_phase
          ? res_.rx_half_interrupt && remaining_valid && position >= rx_dma_half_size_
          : (res_.rx_half_interrupt ? remaining_valid && position < rx_dma_half_size_
                                    : remaining_valid && position <= rx_dma_cursor_);
  if (facts != expected || !stable_phase || raw_boundaries != 0U ||
      queued_boundaries != 0U)
  {
    counters_.rx_deadline_violation_.fetch_add(1U, std::memory_order_relaxed);
    InvalidateRxEpoch();
    return true;
  }

  __DMB();
  if (half_phase)
  {
    PublishRxRange(rx_dma_cursor_, rx_dma_half_size_);
    rx_dma_cursor_ = rx_dma_half_size_;
    rx_dma_phase_ = RxDmaPhase::FULL;
  }
  else
  {
    PublishRxRange(rx_dma_cursor_, rx_dma_storage_.size_);
    rx_dma_cursor_ = 0U;
    rx_dma_phase_ = res_.rx_half_interrupt ? RxDmaPhase::HALF : RxDmaPhase::FULL;
  }
  return false;
}

MSPM0UART::RxDmaSample MSPM0UART::SampleRxDmaPosition(size_t& position)
{
  const uint16_t first = DL_DMA_getTransferSize(DMA, res_.dma_rx_channel);
  __DMB();
  const uint32_t first_raw = DL_DMA_getRawInterruptStatus(DMA, RxDmaBoundaryRawMask());
  const uint32_t first_queued = rx_dma_facts_.load(std::memory_order_acquire);
  __DMB();
  const uint16_t second = DL_DMA_getTransferSize(DMA, res_.dma_rx_channel);
  __DMB();
  const uint32_t second_raw = DL_DMA_getRawInterruptStatus(DMA, RxDmaBoundaryRawMask());
  const uint32_t second_queued = rx_dma_facts_.load(std::memory_order_acquire);

  if (first_raw != 0U || first_queued != 0U || second_raw != 0U || second_queued != 0U)
  {
    return RxDmaSample::WAIT_BOUNDARY;
  }
  if (first != second)
  {
    return RxDmaSample::RETRY;
  }
  if (first == 0U || first > rx_dma_storage_.size_)
  {
    return RxDmaSample::INVALID;
  }

  position = rx_dma_storage_.size_ - first;
  return RxDmaSample::STABLE;
}

bool MSPM0UART::FlushPartialRx()
{
  size_t position = 0U;
  const RxDmaSample sample = SampleRxDmaPosition(position);
  if (sample == RxDmaSample::WAIT_BOUNDARY)
  {
    return false;
  }
  if (sample == RxDmaSample::RETRY)
  {
    __DMB();
    NVIC_SetPendingIRQ(res_.irqn);
    return false;
  }

  bool valid = sample == RxDmaSample::STABLE && position >= rx_dma_cursor_;
  if (valid && res_.rx_half_interrupt)
  {
    valid = rx_dma_phase_ == RxDmaPhase::HALF ? position < rx_dma_half_size_
                                              : position >= rx_dma_half_size_;
  }
  if (!valid)
  {
    counters_.rx_deadline_violation_.fetch_add(1U, std::memory_order_relaxed);
    InvalidateRxEpoch();
    return true;
  }

  __DMB();
  PublishRxRange(rx_dma_cursor_, position);
  rx_dma_cursor_ = position;
  rx_partial_flush_pending_ = false;
  return false;
}

void MSPM0UART::PublishRxRange(size_t begin, size_t end)
{
  ASSERT(begin <= end);
  ASSERT(end <= rx_dma_storage_.size_);
  const size_t size = end - begin;
  if (size == 0U)
  {
    return;
  }

  const size_t accepted = std::min(size, _read_port.queue_data_->EmptySize());
  size_t pushed = 0U;
  if (accepted != 0U)
  {
    auto* data = static_cast<uint8_t*>(rx_dma_storage_.addr_) + begin;
    if (_read_port.queue_data_->PushBatch(data, accepted) == ErrorCode::OK)
    {
      pushed = accepted;
      rx_pushed_in_owner_ = true;
    }
  }

  const size_t dropped = size - pushed;
  if (dropped != 0U)
  {
    counters_.rx_drop_.fetch_add(static_cast<uint32_t>(dropped),
                                 std::memory_order_relaxed);
    counters_.rx_loss_generation_.fetch_add(1U, std::memory_order_relaxed);
  }
}

void MSPM0UART::DiscardRxFifo()
{
  uint32_t dropped = 0U;
  bool loss = false;
  while (true)
  {
    uint32_t word_error_categories = 0U;
    while (!DL_UART_isRXFIFOEmpty(res_.instance))
    {
      const uint32_t word = res_.instance->RXDATA;
      const uint32_t word_errors = RxWordInterruptErrors(word);
      word_error_categories |= word_errors;
      CountUartInterruptErrors(word_errors);
      ++dropped;
      loss = true;
    }

    const uint32_t raw_errors =
        DL_UART_getRawInterruptStatus(res_.instance, MSPM0_UART_RX_ERROR_INTERRUPT_MASK);
    CountUartInterruptErrors(raw_errors & ~word_error_categories);
    loss = loss || raw_errors != 0U;
    DL_UART_clearInterruptStatus(res_.instance,
                                 MSPM0_UART_RX_INTERRUPT_MASK | raw_errors);
    __DMB();
    if (DL_UART_isRXFIFOEmpty(res_.instance))
    {
      break;
    }
  }
  if (dropped != 0U)
  {
    counters_.rx_drop_.fetch_add(dropped, std::memory_order_relaxed);
  }
  if (loss)
  {
    counters_.rx_loss_generation_.fetch_add(1U, std::memory_order_relaxed);
  }
}

void MSPM0UART::CountUartInterruptErrors(uint32_t pending)
{
  if ((pending & DL_UART_INTERRUPT_OVERRUN_ERROR) != 0U)
  {
    counters_.rx_overrun_.fetch_add(1U, std::memory_order_relaxed);
  }
  if ((pending & DL_UART_INTERRUPT_FRAMING_ERROR) != 0U)
  {
    counters_.rx_framing_.fetch_add(1U, std::memory_order_relaxed);
  }
  if ((pending & DL_UART_INTERRUPT_PARITY_ERROR) != 0U)
  {
    counters_.rx_parity_.fetch_add(1U, std::memory_order_relaxed);
  }
  if ((pending & DL_UART_INTERRUPT_BREAK_ERROR) != 0U)
  {
    counters_.rx_break_.fetch_add(1U, std::memory_order_relaxed);
  }
  if ((pending & DL_UART_INTERRUPT_NOISE_ERROR) != 0U)
  {
    counters_.rx_noise_.fetch_add(1U, std::memory_order_relaxed);
  }
}

uint32_t MSPM0UART::RxWordInterruptErrors(uint32_t word)
{
  uint32_t errors = 0U;
  if ((word & UART_RXDATA_OVRERR_MASK) != 0U)
  {
    errors |= DL_UART_INTERRUPT_OVERRUN_ERROR;
  }
  if ((word & UART_RXDATA_FRMERR_MASK) != 0U)
  {
    errors |= DL_UART_INTERRUPT_FRAMING_ERROR;
  }
  if ((word & UART_RXDATA_PARERR_MASK) != 0U)
  {
    errors |= DL_UART_INTERRUPT_PARITY_ERROR;
  }
  if ((word & UART_RXDATA_BRKERR_MASK) != 0U)
  {
    errors |= DL_UART_INTERRUPT_BREAK_ERROR;
  }
  if ((word & UART_RXDATA_NERR_MASK) != 0U)
  {
    errors |= DL_UART_INTERRUPT_NOISE_ERROR;
  }
  return errors;
}

void MSPM0UART::InvalidateRxEpoch()
{
  if (!rx_epoch_invalid_)
  {
    rx_epoch_invalid_ = true;
    counters_.rx_loss_generation_.fetch_add(1U, std::memory_order_relaxed);
  }
  const uint32_t next = NextEpoch(rx_epoch_.load(std::memory_order_relaxed));
  rx_epoch_.store(next, std::memory_order_release);
  const uint32_t discarded = rx_dma_facts_.exchange(0U, std::memory_order_acq_rel);
  if (discarded != 0U)
  {
    counters_.rx_stale_event_.fetch_add(1U, std::memory_order_relaxed);
  }
  rx_partial_flush_pending_ = false;
}

void MSPM0UART::StartRxEpoch()
{
  const uint32_t next = NextEpoch(rx_epoch_.load(std::memory_order_relaxed));
  rx_epoch_.store(next, std::memory_order_release);
  rx_dma_facts_.store(0U, std::memory_order_release);
  rx_dma_phase_ = res_.rx_half_interrupt ? RxDmaPhase::HALF : RxDmaPhase::FULL;
  rx_dma_cursor_ = 0U;
  rx_epoch_invalid_ = false;
  rx_partial_flush_pending_ = false;
}

UartDmaControlResult MSPM0UART::AdvanceConfig(UART::Configuration config, bool active_tx,
                                              bool in_isr)
{
  UartDmaControlResult stop = AdvanceControlStop(active_tx, false, in_isr);
  if (!stop.IsCompleted())
  {
    return stop;
  }
  if (!control_config_applied_)
  {
    DL_UART_disableFIFOs(res_.instance);
    ApplyDisabledConfig(config);
    control_config_applied_ = true;
  }
  return UartDmaControlResult::Completed(stopped_tx_terminal_);
}

UartDmaControlProgress MSPM0UART::CompleteConfig(bool)
{
  StartDataPath();
  FinishControl();
  return UartDmaControlProgress::COMPLETED;
}

UartDmaControlResult MSPM0UART::AdvanceRecovery(bool active_tx, bool in_isr)
{
  return AdvanceControlStop(active_tx, true, in_isr);
}

UartDmaControlProgress MSPM0UART::CompleteRecovery(bool)
{
  StartDataPath();
  FinishControl();
  return UartDmaControlProgress::COMPLETED;
}

UartDmaControlResult MSPM0UART::AdvanceControlStop(bool active_tx, bool error_stop, bool)
{
  if (control_phase_ == ControlPhase::IDLE)
  {
    BeginControlStop(active_tx, error_stop);
  }
  if (!AdvanceToQuiescence())
  {
    return UartDmaControlResult::Pending();
  }
  return UartDmaControlResult::Completed(stopped_tx_terminal_);
}

void MSPM0UART::BeginControlStop(bool active_tx, bool error_stop)
{
  StopDataPathInterrupts();
  control_config_applied_ = false;
  control_active_tx_ = active_tx;
  control_error_stop_ = error_stop;
  stopped_tx_terminal_ = UartOldTxTerminal::NONE;
  if (res_.rx_mode == RxMode::EXTEND_DMA)
  {
    InvalidateRxEpoch();
  }

  // Sample completion on both sides of DMA disable so a terminal transition in that
  // interval is retained for the common TX model.
  tx_complete_observed_ =
      (DL_UART_getRawInterruptStatus(res_.instance, DL_UART_INTERRUPT_DMA_DONE_TX) &
       DL_UART_INTERRUPT_DMA_DONE_TX) != 0U;
  DL_DMA_disableChannel(DMA, res_.dma_tx_channel);
  __DMB();
  tx_complete_observed_ =
      tx_complete_observed_ ||
      ((DL_UART_getRawInterruptStatus(res_.instance, DL_UART_INTERRUPT_DMA_DONE_TX) &
        DL_UART_INTERRUPT_DMA_DONE_TX) != 0U);
  if (active_tx && error_stop)
  {
    stopped_tx_terminal_ = UartOldTxTerminal::ERROR;
  }

  DL_DMA_clearInterruptStatus(DMA, MSPM0DmaDispatcher::CompleteMask(res_.dma_tx_channel));
  if (error_stop)
  {
    counters_.recovery_.fetch_add(1U, std::memory_order_relaxed);
  }

  if (tx_line_active_ && !DL_UART_isTXFIFOEmpty(res_.instance))
  {
    DL_UART_enableInterrupt(res_.instance, DL_UART_INTERRUPT_EOT_DONE);
    control_phase_ = ControlPhase::WAIT_EOT;
  }
  else
  {
    control_phase_ = ControlPhase::WAIT_UART_IDLE;
  }
}

bool MSPM0UART::AdvanceToQuiescence()
{
  if (control_phase_ == ControlPhase::WAIT_EOT)
  {
    const bool eot =
        (DL_UART_getRawInterruptStatus(res_.instance, DL_UART_INTERRUPT_EOT_DONE) &
         DL_UART_INTERRUPT_EOT_DONE) != 0U;
    if (!eot)
    {
      DL_UART_enableInterrupt(res_.instance, DL_UART_INTERRUPT_EOT_DONE);
      return false;
    }
    DL_UART_disableInterrupt(res_.instance, DL_UART_INTERRUPT_EOT_DONE);
    DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_EOT_DONE);
    tx_line_active_ = false;
    control_phase_ = ControlPhase::WAIT_UART_IDLE;
  }

  if (control_phase_ == ControlPhase::WAIT_UART_IDLE)
  {
    if (!uart_disabled_for_control_)
    {
      DL_UART_disable(res_.instance);
      DiscardRxFifo();
      DL_UART_clearInterruptStatus(res_.instance, MSPM0_UART_RX_INTERRUPT_MASK);
      uart_disabled_for_control_ = true;
    }
    if (DL_UART_isBusy(res_.instance))
    {
      __DMB();
      NVIC_SetPendingIRQ(res_.irqn);
      return false;
    }

    tx_complete_observed_ =
        tx_complete_observed_ ||
        ((DL_UART_getRawInterruptStatus(res_.instance, DL_UART_INTERRUPT_DMA_DONE_TX) &
          DL_UART_INTERRUPT_DMA_DONE_TX) != 0U);
    if (control_active_tx_ && !control_error_stop_ && tx_complete_observed_)
    {
      stopped_tx_terminal_ = UartOldTxTerminal::COMPLETE;
    }
    DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_DMA_DONE_TX);
    tx_line_active_ = false;

    DL_UART_disableInterrupt(res_.instance, MSPM0_UART_RX_INTERRUPT_MASK);
    DL_UART_clearInterruptStatus(res_.instance, MSPM0_UART_RX_INTERRUPT_MASK);
    DiscardRxFifo();
    if (res_.rx_mode == RxMode::EXTEND_DMA)
    {
      DL_DMA_disableChannel(DMA, res_.dma_rx_channel);
      __DMB();
      DL_DMA_clearInterruptStatus(DMA, RxDmaBoundaryRawMask());
    }
    DL_UART_clearInterruptStatus(res_.instance, MSPM0_UART_RX_GAP_INTERRUPT_MASK);
    DL_UART_clearDMAReceiveEventStatus(res_.instance, DL_UART_DMA_INTERRUPT_RX);
    DL_UART_clearDMATransmitEventStatus(res_.instance);
    control_phase_ = ControlPhase::QUIESCENT;
  }
  return control_phase_ == ControlPhase::QUIESCENT;
}

void MSPM0UART::FinishControl()
{
  control_phase_ = ControlPhase::IDLE;
  stopped_tx_terminal_ = UartOldTxTerminal::NONE;
  control_config_applied_ = false;
  uart_disabled_for_control_ = false;
  control_active_tx_ = false;
  control_error_stop_ = false;
  tx_complete_observed_ = false;
}

void MSPM0UART::TxDmaCallback(void* context, uint32_t events)
{
  auto* uart = static_cast<MSPM0UART*>(context);
  if ((events & DispatcherEvent(MSPM0DmaDispatcher::Event::ERROR)) == 0U)
  {
    return;
  }
  uart->dma_error_pending_.store(1U, std::memory_order_release);
  __DMB();
  NVIC_SetPendingIRQ(uart->res_.irqn);
}

void MSPM0UART::RxDmaCallback(void* context, uint32_t events)
{
  auto* uart = static_cast<MSPM0UART*>(context);
  uint32_t facts = 0U;
  if ((events & DispatcherEvent(MSPM0DmaDispatcher::Event::EARLY)) != 0U)
  {
    facts |= FactMask(RxDmaFact::HALF);
  }
  if ((events & DispatcherEvent(MSPM0DmaDispatcher::Event::COMPLETE)) != 0U)
  {
    facts |= FactMask(RxDmaFact::FULL);
  }
  if ((events & DispatcherEvent(MSPM0DmaDispatcher::Event::ERROR)) != 0U)
  {
    uart->dma_error_pending_.store(1U, std::memory_order_release);
  }
  if (facts != 0U)
  {
    uart->PublishRxDmaFacts(facts);
  }
  __DMB();
  NVIC_SetPendingIRQ(uart->res_.irqn);
}

void MSPM0UART::PublishRxDmaFacts(uint32_t facts)
{
  const uint32_t boundary_facts = FactMask(RxDmaFact::HALF) | FactMask(RxDmaFact::FULL);
  ASSERT(facts != 0U && (facts & ~boundary_facts) == 0U);
  const uint32_t epoch = rx_epoch_.load(std::memory_order_acquire);
  uint32_t observed = rx_dma_facts_.load(std::memory_order_relaxed);
  while (true)
  {
    const uint32_t observed_epoch = observed >> RX_DMA_EPOCH_SHIFT;
    uint32_t desired = (epoch << RX_DMA_EPOCH_SHIFT) | facts;
    if (observed_epoch == epoch)
    {
      desired = observed | facts;
      // A repeated boundary fact in one epoch means the owner missed an indistinguishable
      // same-phase wrap; retain CONFLICT so it cannot publish ambiguous data.
      if ((observed & facts & boundary_facts) != 0U)
      {
        desired |= FactMask(RxDmaFact::CONFLICT);
      }
    }
    if (rx_dma_facts_.compare_exchange_weak(observed, desired, std::memory_order_release,
                                            std::memory_order_relaxed))
    {
      return;
    }
  }
}

uint32_t MSPM0UART::NextEpoch(uint32_t epoch)
{
  constexpr uint32_t MAX_EPOCH =
      std::numeric_limits<uint32_t>::max() >> RX_DMA_EPOCH_SHIFT;
  return epoch >= MAX_EPOCH ? 1U : epoch + 1U;
}

uint32_t MSPM0UART::GetRxDropCount() const
{
  return counters_.rx_drop_.load(std::memory_order_relaxed);
}

uint32_t MSPM0UART::GetRxLossGeneration() const
{
  return counters_.rx_loss_generation_.load(std::memory_order_relaxed);
}

uint32_t MSPM0UART::GetRxDeadlineViolationCount() const
{
  return counters_.rx_deadline_violation_.load(std::memory_order_relaxed);
}

uint32_t MSPM0UART::GetRxStaleEventCount() const
{
  return counters_.rx_stale_event_.load(std::memory_order_relaxed);
}

uint32_t MSPM0UART::GetRxOverrunCount() const
{
  return counters_.rx_overrun_.load(std::memory_order_relaxed);
}

uint32_t MSPM0UART::GetRxFramingErrorCount() const
{
  return counters_.rx_framing_.load(std::memory_order_relaxed);
}

uint32_t MSPM0UART::GetRxParityErrorCount() const
{
  return counters_.rx_parity_.load(std::memory_order_relaxed);
}

uint32_t MSPM0UART::GetRxBreakErrorCount() const
{
  return counters_.rx_break_.load(std::memory_order_relaxed);
}

uint32_t MSPM0UART::GetRxNoiseErrorCount() const
{
  return counters_.rx_noise_.load(std::memory_order_relaxed);
}

uint32_t MSPM0UART::GetDmaErrorCount() const
{
  return counters_.dma_error_.load(std::memory_order_relaxed);
}

uint32_t MSPM0UART::GetRecoveryCount() const
{
  return counters_.recovery_.load(std::memory_order_relaxed);
}

#if defined(UART0_BASE)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART0_IRQHandler(void) { MSPM0UART::OnInterrupt(0U); }
#endif

#if defined(UART1_BASE)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART1_IRQHandler(void) { MSPM0UART::OnInterrupt(1U); }
#endif

#if defined(UART2_BASE)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART2_IRQHandler(void) { MSPM0UART::OnInterrupt(2U); }
#endif

#if defined(UART3_BASE)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART3_IRQHandler(void) { MSPM0UART::OnInterrupt(3U); }
#endif

#if defined(UART4_BASE)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART4_IRQHandler(void) { MSPM0UART::OnInterrupt(4U); }
#endif

#if defined(UART5_BASE)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART5_IRQHandler(void) { MSPM0UART::OnInterrupt(5U); }
#endif

#if defined(UART6_BASE)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART6_IRQHandler(void) { MSPM0UART::OnInterrupt(6U); }
#endif

#if defined(UART7_BASE)
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UART7_IRQHandler(void) { MSPM0UART::OnInterrupt(7U); }
#endif
