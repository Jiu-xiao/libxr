#include "mspm0_uart_common_cases.hpp"

namespace
{

using LibXR::ConstRawData;
using LibXR::ErrorCode;
using LibXR::MSPM0UART;
using LibXR::OperationPollingStatus;
using LibXR::RawData;
using LibXR::ReadOperation;
using LibXR::UART;
using LibXR::WriteOperation;
using namespace MSPM0UartTest;

static_assert(DMA_SYS_N_DMA_CHANNEL == 7U);
static_assert(DMA_SYS_N_DMA_FULL_CHANNEL == 3U);
static_assert(MSPM0UART::ResolveIndex(UART1_INT_IRQn) == 1U);
static_assert(UART_0_LIBXR_EXTEND_CAPABLE == 1);
static_assert(UART_1_LIBXR_EXTEND_CAPABLE == 0);
static_assert(!LibXR::MSPM0DmaDispatcher::EarlyInterruptSupported(DMA_CH1_CHAN_ID));

size_t late_tx_completion_injections = 0U;
size_t rx_clear_window_injections = 0U;
size_t early_tx_event_injections = 0U;
size_t config_during_rx_injections = 0U;
MSPM0UART* config_during_rx_uart = nullptr;
UART::Configuration config_during_rx_value{};
ErrorCode config_during_rx_result = ErrorCode::PENDING;

struct CompletionState
{
  size_t calls = 0U;
  bool in_isr = false;
  ErrorCode result = ErrorCode::PENDING;
};

struct ResubmitState
{
  MSPM0UART* uart = nullptr;
  const uint8_t* data = nullptr;
  size_t size = 0U;
  WriteOperation* operation = nullptr;
  size_t calls = 0U;
  bool in_isr = false;
  ErrorCode completion_result = ErrorCode::PENDING;
  ErrorCode write_result = ErrorCode::PENDING;
  size_t uart_calls_before = 0U;
  size_t uart_calls_after = 0U;
  size_t dma_calls_before = 0U;
  size_t dma_calls_after = 0U;
  uint32_t ipsr = 0U;
};

void RecordCompletion(bool in_isr, CompletionState* state, ErrorCode result)
{
  ++state->calls;
  state->in_isr = in_isr;
  state->result = result;
}

void ResubmitWrite(bool in_isr, ResubmitState* state, ErrorCode result)
{
  ++state->calls;
  state->in_isr = in_isr;
  state->completion_result = result;
  state->ipsr = __get_IPSR();
  state->uart_calls_before = FakeMSPM0::uart_mmio_call_count;
  state->dma_calls_before = FakeMSPM0::dma_call_trace_size;
  state->write_result =
      state->uart->Write(ConstRawData{state->data, state->size}, *state->operation);
  state->uart_calls_after = FakeMSPM0::uart_mmio_call_count;
  state->dma_calls_after = FakeMSPM0::dma_call_trace_size;
}

void InjectTxCompletionAfterStop(UART_Regs* uart)
{
  FakeMSPM0::uart_is_tx_fifo_empty_hook = nullptr;
  ++late_tx_completion_injections;
  FakeMSPM0::RaiseCpuInterrupt(uart, DL_UART_INTERRUPT_DMA_DONE_TX);
}

void InjectParityErrorDuringClear(UART_Regs* uart, uint32_t mask)
{
  if ((mask & DL_UART_INTERRUPT_PARITY_ERROR) == 0U)
  {
    return;
  }
  FakeMSPM0::uart_clear_interrupt_hook = nullptr;
  ++rx_clear_window_injections;
  (void)FakeMSPM0::InjectRx(uart, 0xC1U, UART_RXDATA_PARERR_MASK);
}

void InjectCompletionDuringTxEnable(uint8_t channel)
{
  if (channel != DMA_CH3_CHAN_ID)
  {
    return;
  }
  FakeMSPM0::dma_enable_hook = nullptr;
  ++early_tx_event_injections;
  FakeMSPM0::dma_channels[channel].enabled = false;
  FakeMSPM0::dma_channels[channel].transfer_size = 0U;
  FakeMSPM0::RaiseCpuInterrupt(UART1, DL_UART_INTERRUPT_DMA_DONE_TX);
  ServiceUart(UART1, 1U);
}

void InjectErrorDuringTxEnable(uint8_t channel)
{
  if (channel != DMA_CH3_CHAN_ID)
  {
    return;
  }
  FakeMSPM0::dma_enable_hook = nullptr;
  ++early_tx_event_injections;
  FakeMSPM0::RaiseDmaInterrupt(DL_DMA_INTERRUPT_DATA_ERROR);
  ServiceDma();
}

void ReserveConfigDuringRxRead(UART_Regs*)
{
  FakeMSPM0::rxdata_read_hook = nullptr;
  ++config_during_rx_injections;
  config_during_rx_result = config_during_rx_uart->SetConfig(config_during_rx_value);
}

struct Fixture
{
  explicit Fixture(uint32_t rx_queue_capacity = 8U)
      : uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH3, tx_storage, 6U, rx_queue_capacity))
  {
  }

  ~Fixture() { FakeMSPM0::EndUartOwnerEnforcement(); }

  void EnforceOwner() { FakeMSPM0::BeginUartOwnerEnforcement(); }

  LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
  MSPM0UART uart;
};

void IgnoreDma(void*, uint32_t) {}

bool TestG3507CapabilitiesAndDeferredTx()
{
  ResetHarness();
  LibXR::MSPM0DmaDispatcher::Registration early_registration;
  MSPM0_CHECK(LibXR::MSPM0DmaDispatcher::Register(
                  0U, LibXR::MSPM0DmaDispatcher::EARLY, IgnoreDma, nullptr,
                  early_registration) == ErrorCode::NOT_SUPPORT);

  Fixture fixture;
  MSPM0_CHECK(fixture.uart.GetRxMode() == MSPM0UART::RxMode::MAIN_BYTE_IRQ);
  MSPM0_CHECK(fixture.uart._write_port.queue_data_->MaxSize() ==
              fixture.tx_storage.Size() / 2U);
  MSPM0_CHECK(UART1->rx_fifo_threshold == DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
  MSPM0_CHECK((UART1->CPU_INT.IMASK & DL_UART_INTERRUPT_RX) != 0U);
  MSPM0_CHECK(UART1->DMA_TRIG_RX.IMASK == 0U);
  MSPM0_CHECK(UART1->direction == DL_UART_DIRECTION_TX_RX);

  const auto& configured_dma = FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID];
  MSPM0_CHECK(configured_dma.config.trigger == DMA_UART1_TX_TRIG);
  MSPM0_CHECK(configured_dma.config.transferMode == DL_DMA_SINGLE_TRANSFER_MODE);
  MSPM0_CHECK(configured_dma.config.srcIncrement == DL_DMA_ADDR_INCREMENT);
  MSPM0_CHECK(configured_dma.config.destIncrement == DL_DMA_ADDR_UNCHANGED);

  fixture.EnforceOwner();
  const std::array<uint8_t, 3U> first{0x10U, 0x11U, 0x12U};
  const std::array<uint8_t, 4U> second{0x20U, 0x21U, 0x22U, 0x23U};
  OperationPollingStatus first_status;
  OperationPollingStatus second_status;
  WriteOperation first_operation(first_status);
  WriteOperation second_operation(second_status);

  MSPM0_CHECK(fixture.uart.Write(ConstRawData{first.data(), first.size()},
                                 first_operation) == ErrorCode::OK);
  MSPM0_CHECK(fixture.uart.Write(ConstRawData{second.data(), second.size()},
                                 second_operation) == ErrorCode::OK);
  MSPM0_CHECK(first_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(second_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_mmio_call_count == 0U);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART1_INT_IRQn) != 0U);

  ServiceUart(UART1, 1U);
  MSPM0_CHECK(first_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(second_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 0U, first));

  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(second_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 2U);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 1U, second));
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].source_history[0U] !=
              FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].source_history[1U]);

  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID, true);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 2U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestMainRxErrorsAndQueueDrops()
{
  ResetHarness();
  Fixture fixture(4U);
  fixture.EnforceOwner();

  uint8_t received = 0U;
  OperationPollingStatus read_status;
  ReadOperation read_operation(read_status);
  MSPM0_CHECK(fixture.uart.Read(RawData{&received, 1U}, read_operation) == ErrorCode::OK);

  FakeMSPM0::InjectRx(UART1, 0x31U, UART_RXDATA_PARERR_MASK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(fixture.uart.GetRxParityErrorCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 1U);

  FakeMSPM0::InjectRx(UART1, 0x32U, UART_RXDATA_FRMERR_MASK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(fixture.uart.GetRxFramingErrorCount() == 1U);

  FakeMSPM0::InjectRx(UART1, 0x33U, UART_RXDATA_BRKERR_MASK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(fixture.uart.GetRxBreakErrorCount() == 1U);

  FakeMSPM0::InjectRx(UART1, 0x34U, UART_RXDATA_NERR_MASK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(fixture.uart.GetRxNoiseErrorCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 4U);

  FakeMSPM0::InjectRx(UART1, 0x35U, UART_RXDATA_OVRERR_MASK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(received == 0x35U);
  MSPM0_CHECK(fixture.uart.GetRxOverrunCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 5U);

  const uint32_t drops_before = fixture.uart.GetRxDropCount();
  const std::array<uint8_t, 6U> burst{0x40U, 0x41U, 0x42U, 0x43U, 0x44U, 0x45U};
  for (const uint8_t value : burst)
  {
    MSPM0_CHECK(FakeMSPM0::InjectRx(UART1, value));
    ServiceUart(UART1, 1U);
  }
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 4U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == drops_before + 2U);
  MSPM0_CHECK(
      ExpectQueuedBytes(fixture.uart, std::span<const uint8_t>{burst.data(), 4U}));
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestMainHardwareFifoOverrunIsSeparateFromQueueSaturation()
{
  ResetHarness();
  Fixture fixture(8U);
  fixture.EnforceOwner();

  const std::array<uint8_t, 4U> retained{0x70U, 0x71U, 0x72U, 0x73U};
  for (const uint8_t value : retained)
  {
    MSPM0_CHECK(FakeMSPM0::InjectRx(UART1, value));
  }
  MSPM0_CHECK(UART1->rx_size == UART1->rx_fifo.size());
  MSPM0_CHECK(!FakeMSPM0::InjectRx(UART1, 0x74U));

  ServiceUart(UART1, 1U);
  MSPM0_CHECK(fixture.uart.GetRxOverrunCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, retained));
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestMainRxErrorClearWindowIsDiscarded()
{
  ResetHarness();
  Fixture fixture(4U);
  fixture.EnforceOwner();
  rx_clear_window_injections = 0U;

  uint8_t received = 0U;
  OperationPollingStatus read_status;
  ReadOperation read_operation(read_status);
  MSPM0_CHECK(fixture.uart.Read(RawData{&received, 1U}, read_operation) == ErrorCode::OK);

  const uint32_t drops_before = fixture.uart.GetRxDropCount();
  const uint32_t losses_before = fixture.uart.GetRxLossGeneration();
  FakeMSPM0::uart_clear_interrupt_hook = InjectParityErrorDuringClear;
  MSPM0_CHECK(FakeMSPM0::InjectRx(UART1, 0xC0U, UART_RXDATA_PARERR_MASK));
  ServiceUart(UART1, 1U);

  MSPM0_CHECK(rx_clear_window_injections == 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == drops_before + 2U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == losses_before + 1U);
  MSPM0_CHECK(fixture.uart.GetRxParityErrorCount() == 2U);

  MSPM0_CHECK(FakeMSPM0::InjectRx(UART1, 0xC2U));
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(received == 0xC2U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == drops_before + 2U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestMainRawErrorWithoutFifoDataLatchesLoss()
{
  ResetHarness();
  Fixture fixture(4U);
  fixture.EnforceOwner();

  uint8_t received = 0U;
  OperationPollingStatus read_status;
  ReadOperation read_operation(read_status);
  MSPM0_CHECK(fixture.uart.Read(RawData{&received, 1U}, read_operation) == ErrorCode::OK);

  const uint32_t drops_before = fixture.uart.GetRxDropCount();
  const uint32_t losses_before = fixture.uart.GetRxLossGeneration();
  const uint32_t parity_before = fixture.uart.GetRxParityErrorCount();
  MSPM0_CHECK(UART1->rx_size == 0U);
  FakeMSPM0::RaiseCpuInterrupt(UART1, DL_UART_INTERRUPT_PARITY_ERROR);
  ServiceUart(UART1, 1U);

  MSPM0_CHECK(fixture.uart.GetRxParityErrorCount() == parity_before + 1U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == drops_before);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == losses_before + 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);

  MSPM0_CHECK(FakeMSPM0::InjectRx(UART1, 0xC3U));
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(received == 0xC3U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == drops_before);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == losses_before + 1U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestConfigAdmissionFromTaskAndIsr()
{
  ResetHarness();
  Fixture fixture;
  fixture.EnforceOwner();

  const UART::Configuration first{230400U, UART::Parity::EVEN, 7U, 2U};
  const UART::Configuration second{460800U, UART::Parity::ODD, 8U, 1U};
  MSPM0_CHECK(fixture.uart.SetConfig(first) == ErrorCode::OK);
  MSPM0_CHECK(fixture.uart.SetConfig(second) == ErrorCode::BUSY);
  MSPM0_CHECK(FakeMSPM0::uart_mmio_call_count == 0U);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(UART1->baudrate == first.baudrate);
  MSPM0_CHECK(UART1->parity == DL_UART_PARITY_EVEN);
  MSPM0_CHECK(UART1->word_length == DL_UART_WORD_LENGTH_7_BITS);
  MSPM0_CHECK(UART1->stop_bits == DL_UART_STOP_BITS_TWO);

  const size_t mmio_before_isr_call = FakeMSPM0::uart_mmio_call_count;
  {
    FakeMSPM0::IrqScope ordinary_isr(UART2_INT_IRQn);
    MSPM0_CHECK(fixture.uart.SetConfig(second) == ErrorCode::OK);
  }
  MSPM0_CHECK(FakeMSPM0::uart_mmio_call_count == mmio_before_isr_call);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART1_INT_IRQn) != 0U);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(UART1->baudrate == second.baudrate);
  MSPM0_CHECK(UART1->parity == DL_UART_PARITY_ODD);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestConfigDoesNotWaitForEotOnRxOnlyBusy()
{
  ResetHarness();
  Fixture fixture;
  fixture.EnforceOwner();

  const std::array<uint8_t, 3U> payload{0xA0U, 0xA1U, 0xA2U};
  OperationPollingStatus write_status;
  WriteOperation write_operation(write_status);
  MSPM0_CHECK(fixture.uart.Write(ConstRawData{payload.data(), payload.size()},
                                 write_operation) == ErrorCode::OK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(write_status.Load() == OperationPollingStatus::DONE);
  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);

  UART1->tx_fifo_empty = true;
  UART1->busy = true;
  const UART::Configuration config{57600U, UART::Parity::EVEN, 7U, 2U};
  MSPM0_CHECK(fixture.uart.SetConfig(config) == ErrorCode::OK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(UART1->baudrate != config.baudrate);
  MSPM0_CHECK((UART1->CPU_INT.IMASK & DL_UART_INTERRUPT_EOT_DONE) == 0U);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART1_INT_IRQn) != 0U);

  UART1->busy = false;
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(UART1->baudrate == config.baudrate);
  MSPM0_CHECK((UART1->CPU_INT.IMASK & DL_UART_INTERRUPT_EOT_DONE) == 0U);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestLateDmaCompletionAtQuiescenceIsAuthoritative()
{
  ResetHarness();
  Fixture fixture;
  fixture.EnforceOwner();
  late_tx_completion_injections = 0U;

  const std::array<uint8_t, 4U> payload{0xB0U, 0xB1U, 0xB2U, 0xB3U};
  OperationPollingStatus write_status;
  WriteOperation write_operation(write_status);
  MSPM0_CHECK(fixture.uart.Write(ConstRawData{payload.data(), payload.size()},
                                 write_operation) == ErrorCode::OK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(write_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);

  UART1->tx_fifo_empty = true;
  UART1->busy = false;
  FakeMSPM0::uart_is_tx_fifo_empty_hook = InjectTxCompletionAfterStop;
  const UART::Configuration config{230400U, UART::Parity::ODD, 8U, 1U};
  MSPM0_CHECK(fixture.uart.SetConfig(config) == ErrorCode::OK);
  ServiceUart(UART1, 1U);

  MSPM0_CHECK(late_tx_completion_injections == 1U);
  MSPM0_CHECK(UART1->baudrate == config.baudrate);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
  MSPM0_CHECK((UART1->CPU_INT.RIS & DL_UART_INTERRUPT_DMA_DONE_TX) == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestConfigReplaysUncompletedActiveTxFromByteZero()
{
  ResetHarness();
  Fixture fixture;
  fixture.EnforceOwner();

  const std::array<uint8_t, 5U> payload{0xD0U, 0xD1U, 0xD2U, 0xD3U, 0xD4U};
  OperationPollingStatus status;
  WriteOperation operation(status);
  MSPM0_CHECK(fixture.uart.Write(ConstRawData{payload.data(), payload.size()},
                                 operation) == ErrorCode::OK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 0U, payload));

  const UART::Configuration config{57600U, UART::Parity::EVEN, 7U, 2U};
  MSPM0_CHECK(fixture.uart.SetConfig(config) == ErrorCode::OK);
  ServiceUart(UART1, 1U);

  const auto& dma = FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID];
  MSPM0_CHECK(UART1->baudrate == config.baudrate);
  MSPM0_CHECK(dma.history_size == 2U);
  MSPM0_CHECK(dma.source_history[0U] == dma.source_history[1U]);
  MSPM0_CHECK(dma.size_history[0U] == payload.size());
  MSPM0_CHECK(dma.size_history[1U] == payload.size());
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 1U, payload));

  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(dma.history_size == 2U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestDmaErrorRetainsActivePendingAndQueuedOrder()
{
  ResetHarness();
  Fixture fixture;
  fixture.EnforceOwner();

  const std::array<uint8_t, 3U> first{0x10U, 0x11U, 0x12U};
  const std::array<uint8_t, 4U> second{0x20U, 0x21U, 0x22U, 0x23U};
  const std::array<uint8_t, 2U> third{0x30U, 0x31U};
  OperationPollingStatus first_status;
  OperationPollingStatus second_status;
  OperationPollingStatus third_status;
  WriteOperation first_operation(first_status);
  WriteOperation second_operation(second_status);
  WriteOperation third_operation(third_status);

  MSPM0_CHECK(fixture.uart.Write(ConstRawData{first.data(), first.size()},
                                 first_operation) == ErrorCode::OK);
  MSPM0_CHECK(fixture.uart.Write(ConstRawData{second.data(), second.size()},
                                 second_operation) == ErrorCode::OK);
  MSPM0_CHECK(fixture.uart.Write(ConstRawData{third.data(), third.size()},
                                 third_operation) == ErrorCode::OK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(first_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(second_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(third_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 0U, first));

  FakeMSPM0::RaiseDmaInterrupt(DL_DMA_INTERRUPT_DATA_ERROR);
  ServiceDma();
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(fixture.uart.GetDmaErrorCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 2U);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 1U, first));

  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(second_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(third_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 3U);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 2U, second));

  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(third_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 4U);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 3U, third));
  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestConfigWaitsForEotWithoutDisablingReceiver()
{
  ResetHarness();
  Fixture fixture;
  fixture.EnforceOwner();

  const std::array<uint8_t, 4U> payload{0x40U, 0x41U, 0x42U, 0x43U};
  OperationPollingStatus status;
  WriteOperation operation(status);
  MSPM0_CHECK(fixture.uart.Write(ConstRawData{payload.data(), payload.size()},
                                 operation) == ErrorCode::OK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(status.Load() == OperationPollingStatus::DONE);

  UART1->tx_fifo_empty = false;
  const UART::Configuration config{230400U, UART::Parity::ODD, 8U, 1U};
  MSPM0_CHECK(fixture.uart.SetConfig(config) == ErrorCode::OK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(UART1->baudrate != config.baudrate);
  MSPM0_CHECK(UART1->enabled);
  MSPM0_CHECK((UART1->CPU_INT.IMASK & DL_UART_INTERRUPT_EOT_DONE) != 0U);
  MSPM0_CHECK(!FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].enabled);
  MSPM0_CHECK(UART1->direction == DL_UART_DIRECTION_TX_RX);

  UART1->tx_fifo_empty = true;
  UART1->busy = false;
  FakeMSPM0::RaiseCpuInterrupt(UART1, DL_UART_INTERRUPT_EOT_DONE);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(UART1->baudrate == config.baudrate);
  MSPM0_CHECK(UART1->enabled);
  MSPM0_CHECK((UART1->CPU_INT.IMASK & DL_UART_INTERRUPT_RX) != 0U);
  MSPM0_CHECK(UART1->direction == DL_UART_DIRECTION_TX_RX);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 2U);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 1U, payload));

  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestEarlyTerminalEventsDuringStartAreDeferred()
{
  {
    ResetHarness();
    Fixture fixture;
    fixture.EnforceOwner();
    early_tx_event_injections = 0U;
    CompletionState completion;
    auto callback = LibXR::Callback<ErrorCode>::Create(RecordCompletion, &completion);
    WriteOperation operation(callback);
    const std::array<uint8_t, 3U> payload{0x50U, 0x51U, 0x52U};
    FakeMSPM0::dma_enable_hook = InjectCompletionDuringTxEnable;

    MSPM0_CHECK(fixture.uart.Write(ConstRawData{payload.data(), payload.size()},
                                   operation) == ErrorCode::OK);
    ServiceUart(UART1, 1U);
    MSPM0_CHECK(early_tx_event_injections == 1U);
    MSPM0_CHECK(completion.calls == 1U);
    MSPM0_CHECK(completion.in_isr);
    MSPM0_CHECK(completion.result == ErrorCode::OK);
    MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
    MSPM0_CHECK(NVIC_GetPendingIRQ(UART1_INT_IRQn) != 0U);
    ServiceUart(UART1, 1U);
    MSPM0_CHECK(completion.calls == 1U);

    const std::array<uint8_t, 2U> next{0x53U, 0x54U};
    OperationPollingStatus next_status;
    WriteOperation next_operation(next_status);
    MSPM0_CHECK(fixture.uart.Write(ConstRawData{next.data(), next.size()},
                                   next_operation) == ErrorCode::OK);
    ServiceUart(UART1, 1U);
    MSPM0_CHECK(next_status.Load() == OperationPollingStatus::DONE);
    MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 2U);
    MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 1U, next));
    CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  }

  {
    ResetHarness();
    Fixture fixture;
    fixture.EnforceOwner();
    early_tx_event_injections = 0U;
    CompletionState completion;
    auto callback = LibXR::Callback<ErrorCode>::Create(RecordCompletion, &completion);
    WriteOperation operation(callback);
    const std::array<uint8_t, 3U> payload{0x60U, 0x61U, 0x62U};
    FakeMSPM0::dma_enable_hook = InjectErrorDuringTxEnable;

    MSPM0_CHECK(fixture.uart.Write(ConstRawData{payload.data(), payload.size()},
                                   operation) == ErrorCode::OK);
    ServiceUart(UART1, 1U);
    MSPM0_CHECK(early_tx_event_injections == 1U);
    MSPM0_CHECK(completion.calls == 1U);
    MSPM0_CHECK(completion.result == ErrorCode::OK);
    MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
    ServiceUart(UART1, 1U);
    MSPM0_CHECK(completion.calls == 1U);
    MSPM0_CHECK(fixture.uart.GetDmaErrorCount() == 1U);
    MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
    MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 2U);
    MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 1U, payload));
    CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  }
  return true;
}

bool TestOrdinaryIsrWriteIsDeferredToUartOwner()
{
  ResetHarness();
  Fixture fixture;
  fixture.EnforceOwner();
  FakeMSPM0::ResetDmaCallTrace();

  const std::array<uint8_t, 3U> payload{0x70U, 0x71U, 0x72U};
  OperationPollingStatus status;
  WriteOperation operation(status);
  {
    FakeMSPM0::IrqScope ordinary_isr(UART2_INT_IRQn);
    MSPM0_CHECK(fixture.uart.Write(ConstRawData{payload.data(), payload.size()},
                                   operation) == ErrorCode::OK);
  }

  MSPM0_CHECK(status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(FakeMSPM0::uart_mmio_call_count == 0U);
  MSPM0_CHECK(FakeMSPM0::dma_call_trace_size == 0U);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 0U);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART1_INT_IRQn) != 0U);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 0U, payload));
  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestCompletionCallbackWriteOnlyPublishesMoreWork()
{
  ResetHarness();
  Fixture fixture;
  fixture.EnforceOwner();

  const std::array<uint8_t, 3U> first{0x80U, 0x81U, 0x82U};
  const std::array<uint8_t, 4U> nested{0x90U, 0x91U, 0x92U, 0x93U};
  OperationPollingStatus nested_status;
  WriteOperation nested_operation(nested_status);
  ResubmitState state;
  state.uart = &fixture.uart;
  state.data = nested.data();
  state.size = nested.size();
  state.operation = &nested_operation;
  auto callback = LibXR::Callback<ErrorCode>::Create(ResubmitWrite, &state);
  WriteOperation first_operation(callback);

  MSPM0_CHECK(fixture.uart.Write(ConstRawData{first.data(), first.size()},
                                 first_operation) == ErrorCode::OK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(state.calls == 1U);
  MSPM0_CHECK(state.in_isr);
  MSPM0_CHECK(state.ipsr == static_cast<uint32_t>(UART1_INT_IRQn) + 16U);
  MSPM0_CHECK(state.completion_result == ErrorCode::OK);
  MSPM0_CHECK(state.write_result == ErrorCode::OK);
  MSPM0_CHECK(state.uart_calls_before == state.uart_calls_after);
  MSPM0_CHECK(state.dma_calls_before == state.dma_calls_after);
  MSPM0_CHECK(nested_status.Load() == OperationPollingStatus::RUNNING);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART1_INT_IRQn) != 0U);
  MSPM0_CHECK(FakeMSPM0::nvic_software_pending[static_cast<size_t>(UART1_INT_IRQn)]);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 0U, first));

  const size_t nested_start_trace_begin = FakeMSPM0::dma_call_trace_size;
  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(nested_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 2U);
  MSPM0_CHECK(ExpectTxPayload(fixture.tx_storage, DMA_CH3_CHAN_ID, 1U, nested));
  size_t nested_enable_count = 0U;
  for (size_t i = nested_start_trace_begin; i < FakeMSPM0::dma_call_trace_size; ++i)
  {
    const auto& call = FakeMSPM0::dma_call_trace[i];
    if (call.call == FakeMSPM0::DmaCall::ENABLE_CHANNEL &&
        call.channel == DMA_CH3_CHAN_ID)
    {
      ++nested_enable_count;
      MSPM0_CHECK(call.uart_owner == UART1);
      MSPM0_CHECK(call.ipsr == static_cast<uint32_t>(UART1_INT_IRQn) + 16U);
    }
  }
  MSPM0_CHECK(nested_enable_count == 1U);
  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(state.calls == 1U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestRxOnlyErrorDoesNotDisturbActiveTx()
{
  ResetHarness();
  Fixture fixture;
  fixture.EnforceOwner();

  const std::array<uint8_t, 4U> payload{0xA0U, 0xA1U, 0xA2U, 0xA3U};
  OperationPollingStatus write_status;
  WriteOperation write_operation(write_status);
  MSPM0_CHECK(fixture.uart.Write(ConstRawData{payload.data(), payload.size()},
                                 write_operation) == ErrorCode::OK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(write_status.Load() == OperationPollingStatus::DONE);

  uint8_t received = 0U;
  OperationPollingStatus read_status;
  ReadOperation read_operation(read_status);
  MSPM0_CHECK(fixture.uart.Read(RawData{&received, 1U}, read_operation) == ErrorCode::OK);
  MSPM0_CHECK(FakeMSPM0::InjectRx(UART1, 0xB0U, UART_RXDATA_PARERR_MASK));
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(fixture.uart.GetRxParityErrorCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 0U);
  MSPM0_CHECK(fixture.uart.GetDmaErrorCount() == 0U);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].enabled);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::RUNNING);

  MSPM0_CHECK(FakeMSPM0::InjectRx(UART1, 0xB1U));
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(received == 0xB1U);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].enabled);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID].history_size == 1U);
  CompleteTx(UART1, 1U, DMA_CH3_CHAN_ID);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestRxConfigGateAdmitsEnteredFragmentAndRejectsLateFragment()
{
  {
    ResetHarness();
    Fixture fixture;
    fixture.EnforceOwner();
    config_during_rx_injections = 0U;
    config_during_rx_uart = &fixture.uart;
    config_during_rx_value = UART::Configuration{57600U, UART::Parity::EVEN, 7U, 2U};
    config_during_rx_result = ErrorCode::PENDING;

    uint8_t received = 0U;
    OperationPollingStatus status;
    ReadOperation operation(status);
    MSPM0_CHECK(fixture.uart.Read(RawData{&received, 1U}, operation) == ErrorCode::OK);
    MSPM0_CHECK(FakeMSPM0::InjectRx(UART1, 0xC0U));
    FakeMSPM0::rxdata_read_hook = ReserveConfigDuringRxRead;
    ServiceUart(UART1, 1U);

    MSPM0_CHECK(config_during_rx_injections == 1U);
    MSPM0_CHECK(config_during_rx_result == ErrorCode::OK);
    MSPM0_CHECK(status.Load() == OperationPollingStatus::DONE);
    MSPM0_CHECK(received == 0xC0U);
    MSPM0_CHECK(fixture.uart.GetRxDropCount() == 0U);
    MSPM0_CHECK(UART1->baudrate == config_during_rx_value.baudrate);
  }

  {
    ResetHarness();
    Fixture fixture;
    fixture.EnforceOwner();
    const UART::Configuration config{230400U, UART::Parity::ODD, 8U, 1U};
    uint8_t received = 0U;
    OperationPollingStatus status;
    ReadOperation operation(status);
    MSPM0_CHECK(fixture.uart.Read(RawData{&received, 1U}, operation) == ErrorCode::OK);

    MSPM0_CHECK(fixture.uart.SetConfig(config) == ErrorCode::OK);
    MSPM0_CHECK(FakeMSPM0::InjectRx(UART1, 0xD0U));
    ServiceUart(UART1, 1U);
    MSPM0_CHECK(status.Load() == OperationPollingStatus::RUNNING);
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
    MSPM0_CHECK(fixture.uart.GetRxDropCount() == 1U);
    MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);
    MSPM0_CHECK(UART1->baudrate == config.baudrate);

    MSPM0_CHECK(FakeMSPM0::InjectRx(UART1, 0xD1U));
    ServiceUart(UART1, 1U);
    MSPM0_CHECK(status.Load() == OperationPollingStatus::DONE);
    MSPM0_CHECK(received == 0xD1U);
    MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  }
  config_during_rx_uart = nullptr;
  return true;
}

}  // namespace

int main()
{
  if (!TestG3507CapabilitiesAndDeferredTx() || !TestMainRxErrorsAndQueueDrops() ||
      !TestMainHardwareFifoOverrunIsSeparateFromQueueSaturation() ||
      !TestMainRxErrorClearWindowIsDiscarded() ||
      !TestMainRawErrorWithoutFifoDataLatchesLoss() ||
      !TestConfigAdmissionFromTaskAndIsr() ||
      !TestConfigDoesNotWaitForEotOnRxOnlyBusy() ||
      !TestLateDmaCompletionAtQuiescenceIsAuthoritative() ||
      !TestConfigReplaysUncompletedActiveTxFromByteZero() ||
      !TestDmaErrorRetainsActivePendingAndQueuedOrder() ||
      !TestConfigWaitsForEotWithoutDisablingReceiver() ||
      !TestEarlyTerminalEventsDuringStartAreDeferred() ||
      !TestOrdinaryIsrWriteIsDeferredToUartOwner() ||
      !TestCompletionCallbackWriteOnlyPublishesMoreWork() ||
      !TestRxOnlyErrorDoesNotDisturbActiveTx() ||
      !TestRxConfigGateAdmitsEnteredFragmentAndRejectsLateFragment())
  {
    return 1;
  }
  std::cout << "MSPM0 G3507 UART tests passed\n";
  return 0;
}
