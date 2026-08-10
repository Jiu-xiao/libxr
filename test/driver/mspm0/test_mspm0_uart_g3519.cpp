#include "mspm0_uart_common_cases.hpp"

namespace
{

using LibXR::ErrorCode;
using LibXR::MSPM0UART;
using LibXR::OperationPollingStatus;
using LibXR::RawData;
using LibXR::ReadOperation;
using LibXR::UART;
using namespace MSPM0UartTest;

static_assert(DMA_SYS_N_DMA_CHANNEL == 12U);
static_assert(DMA_SYS_N_DMA_FULL_CHANNEL == 6U);
static_assert(UART_0_LIBXR_EXTEND_CAPABLE == 1);
static_assert(UART_7_LIBXR_EXTEND_CAPABLE == 1);
static_assert(UART_1_LIBXR_EXTEND_CAPABLE == 0);
static_assert(DMA_CH1_LIBXR_HALF_INTERRUPT == 1);
static_assert(DMA_CH3_LIBXR_HALF_INTERRUPT == 0);

size_t config_preemption_count = 0U;
size_t constructor_rx_hook_count = 0U;
bool constructor_rx_hook_advance_ok = false;
bool constructor_rx_hook_uart_irq_disabled = false;
bool constructor_rx_hook_uart_pending = false;
size_t queued_boundary_dispatch_count = 0U;
size_t queued_boundary_dma_irq_calls = 0U;
bool queued_boundary_dma_owner_was_clear = false;
size_t gap_retry_injection_count = 0U;
bool gap_retry_advance_ok = false;

bool DmaTraceIsControllerOnly(size_t begin)
{
  if (begin >= FakeMSPM0::dma_call_trace_size)
  {
    return false;
  }
  for (size_t index = begin; index < FakeMSPM0::dma_call_trace_size; ++index)
  {
    const auto& trace = FakeMSPM0::dma_call_trace[index];
    if (trace.channel != FakeMSPM0::kDmaControllerCall || trace.uart_owner != nullptr ||
        trace.ipsr != static_cast<uint32_t>(DMA_INT_IRQn) + 16U)
    {
      return false;
    }
  }
  return true;
}

bool DmaSizeTraceIsOwnedBy(UART_Regs* uart, size_t begin, bool& saw_size_access)
{
  saw_size_access = false;
  for (size_t index = begin; index < FakeMSPM0::dma_call_trace_size; ++index)
  {
    const auto& trace = FakeMSPM0::dma_call_trace[index];
    if (trace.call != FakeMSPM0::DmaCall::GET_TRANSFER_SIZE &&
        trace.call != FakeMSPM0::DmaCall::SET_TRANSFER_SIZE)
    {
      continue;
    }
    saw_size_access = true;
    if (trace.uart_owner != uart || trace.ipsr != static_cast<uint32_t>(uart->irqn) + 16U)
    {
      return false;
    }
  }
  return true;
}

void ServiceConfigAtFirstUnmask()
{
  FakeMSPM0::primask_restore_hook = nullptr;
  ++config_preemption_count;
  ServiceUart(UART0, 0U);
}

void InjectHalfDuringUartEnable(UART_Regs* uart)
{
  FakeMSPM0::uart_enable_hook = nullptr;
  ++constructor_rx_hook_count;
  constructor_rx_hook_uart_irq_disabled = NVIC_GetEnableIRQ(uart->irqn) == 0U;
  constructor_rx_hook_advance_ok =
      uart->enabled &&
      FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xD0U, 0xD1U, 0xD2U, 0xD3U});
  ServiceDma();
  constructor_rx_hook_uart_pending = NVIC_GetPendingIRQ(uart->irqn) != 0U;
}

void InjectDmaErrorDuringRxPublisherEnable(UART_Regs* uart, uint32_t interrupt)
{
  FakeMSPM0::uart_enable_dma_rx_hook = nullptr;
  ++constructor_rx_hook_count;
  constructor_rx_hook_uart_irq_disabled = NVIC_GetEnableIRQ(uart->irqn) == 0U;
  constructor_rx_hook_advance_ok = true;
  FakeMSPM0::RaiseDmaInterrupt(DL_DMA_INTERRUPT_DATA_ERROR);
  if (interrupt == DL_UART_DMA_INTERRUPT_RX)
  {
    ServiceDma();
  }
  constructor_rx_hook_uart_pending = NVIC_GetPendingIRQ(uart->irqn) != 0U;
}

void DispatchBoundaryBeforeRawRead(FakeMSPM0::DmaCall call, uint8_t channel)
{
  if (call != FakeMSPM0::DmaCall::GET_RAW_INTERRUPT_STATUS ||
      channel != FakeMSPM0::kDmaControllerCall)
  {
    return;
  }
  FakeMSPM0::dma_access_hook = nullptr;
  ++queued_boundary_dispatch_count;
  const size_t trace_begin = FakeMSPM0::dma_call_trace_size;
  ServiceDma();
  queued_boundary_dma_owner_was_clear = true;
  for (size_t index = trace_begin; index < FakeMSPM0::dma_call_trace_size; ++index)
  {
    const auto& trace = FakeMSPM0::dma_call_trace[index];
    if (trace.ipsr == static_cast<uint32_t>(DMA_INT_IRQn) + 16U)
    {
      ++queued_boundary_dma_irq_calls;
      queued_boundary_dma_owner_was_clear =
          queued_boundary_dma_owner_was_clear && trace.uart_owner == nullptr;
    }
  }
}

void AdvanceRxBetweenGapSamples(FakeMSPM0::DmaCall call, uint8_t channel)
{
  if (call != FakeMSPM0::DmaCall::GET_RAW_INTERRUPT_STATUS ||
      channel != FakeMSPM0::kDmaControllerCall)
  {
    return;
  }
  FakeMSPM0::dma_access_hook = nullptr;
  ++gap_retry_injection_count;
  gap_retry_advance_ok = FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xC1U});
}

struct ExtendFixture
{
  explicit ExtendFixture(uint32_t rx_queue_capacity = 32U)
      : rx_mapping(FakeMSPM0::RegisterHostMemory(rx_storage.data(), rx_storage.size())),
        uart(MSPM0_UART_EXTEND_INIT(UART_0, DMA_CH0, DMA_CH1, tx_storage, 6U, rx_storage,
                                    rx_queue_capacity))
  {
  }

  ~ExtendFixture() { FakeMSPM0::EndUartOwnerEnforcement(); }

  void EnforceOwner() { FakeMSPM0::BeginUartOwnerEnforcement(); }

  LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
  std::array<uint8_t, 8U> rx_storage{};
  uint32_t rx_mapping;
  MSPM0UART uart;
};

struct MainFixture
{
  MainFixture() : uart(MSPM0_UART_MAIN_INIT(UART_1, DMA_CH4, tx_storage, 4U, 8U)) {}

  LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
  MSPM0UART uart;
};

struct Extend7Fixture
{
  explicit Extend7Fixture(uint32_t rx_queue_capacity = 32U)
      : rx_mapping(FakeMSPM0::RegisterHostMemory(rx_storage.data(), rx_storage.size())),
        uart(MSPM0_UART_EXTEND_INIT(UART_7, DMA_CH2, DMA_CH3, tx_storage, 6U, rx_storage,
                                    rx_queue_capacity))
  {
  }

  ~Extend7Fixture() { FakeMSPM0::EndUartOwnerEnforcement(); }

  LibXR::MSPM0UARTTxBuffer<32U> tx_storage{};
  std::array<uint8_t, 8U> rx_storage{};
  uint32_t rx_mapping;
  MSPM0UART uart;
};

bool AdvanceAndDispatch(std::initializer_list<uint8_t> bytes)
{
  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, bytes));
  ServiceDma();
  return true;
}

bool TestConstructorPreservesHalfWakeup()
{
  ResetHarness();
  constructor_rx_hook_count = 0U;
  constructor_rx_hook_advance_ok = false;
  constructor_rx_hook_uart_irq_disabled = false;
  constructor_rx_hook_uart_pending = false;
  FakeMSPM0::uart_enable_hook = InjectHalfDuringUartEnable;

  ExtendFixture fixture;
  MSPM0_CHECK(constructor_rx_hook_count == 1U);
  MSPM0_CHECK(constructor_rx_hook_advance_ok);
  MSPM0_CHECK(constructor_rx_hook_uart_irq_disabled);
  MSPM0_CHECK(constructor_rx_hook_uart_pending);
  MSPM0_CHECK(NVIC_GetEnableIRQ(UART0_INT_IRQn) != 0U);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) != 0U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);

  std::array<uint8_t, 4U> received{};
  OperationPollingStatus read_status;
  ReadOperation read_operation(read_status);
  MSPM0_CHECK(fixture.uart.Read(RawData{received.data(), received.size()},
                                read_operation) == ErrorCode::OK);
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 4U> expected{0xD0U, 0xD1U, 0xD2U, 0xD3U};
  MSPM0_CHECK(read_status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(received == expected);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  return true;
}

bool TestConstructorPreservesErrorWakeup()
{
  ResetHarness();
  constructor_rx_hook_count = 0U;
  constructor_rx_hook_advance_ok = false;
  constructor_rx_hook_uart_irq_disabled = false;
  constructor_rx_hook_uart_pending = false;
  FakeMSPM0::uart_enable_dma_rx_hook = InjectDmaErrorDuringRxPublisherEnable;

  ExtendFixture fixture;
  MSPM0_CHECK(constructor_rx_hook_count == 1U);
  MSPM0_CHECK(constructor_rx_hook_advance_ok);
  MSPM0_CHECK(constructor_rx_hook_uart_irq_disabled);
  MSPM0_CHECK(constructor_rx_hook_uart_pending);
  MSPM0_CHECK(NVIC_GetEnableIRQ(UART0_INT_IRQn) != 0U);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) != 0U);
  MSPM0_CHECK(fixture.uart.GetDmaErrorCount() == 0U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 0U);

  ServiceUart(UART0, 0U);
  MSPM0_CHECK(fixture.uart.GetDmaErrorCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  return true;
}

bool TestG3519CapabilitiesAndContinuousRing()
{
  ResetHarness();
  ExtendFixture fixture;
  MSPM0_CHECK(fixture.rx_mapping != FakeMSPM0::kInvalidMcuAddress);
  MSPM0_CHECK(fixture.uart.GetRxMode() == MSPM0UART::RxMode::EXTEND_DMA);
  MSPM0_CHECK(fixture.uart.RxHalfInterruptEnabled());

  const auto& rx_dma = FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID];
  MSPM0_CHECK(rx_dma.full_channel);
  MSPM0_CHECK(rx_dma.config.trigger == DMA_UART0_RX_TRIG);
  MSPM0_CHECK(rx_dma.config.transferMode == DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE);
  MSPM0_CHECK(rx_dma.config.srcWidth == DL_DMA_WIDTH_BYTE);
  MSPM0_CHECK(rx_dma.config.destWidth == DL_DMA_WIDTH_BYTE);
  MSPM0_CHECK(rx_dma.config.srcIncrement == DL_DMA_ADDR_UNCHANGED);
  MSPM0_CHECK(rx_dma.config.destIncrement == DL_DMA_ADDR_INCREMENT);
  MSPM0_CHECK(rx_dma.early_threshold == DL_DMA_EARLY_INTERRUPT_THRESHOLD_HALF);
  MSPM0_CHECK(rx_dma.programmed_transfer_size == fixture.rx_storage.size());
  MSPM0_CHECK(rx_dma.source == LowAddress(&UART0->RXDATA));
  MSPM0_CHECK(rx_dma.destination == LowAddress(fixture.rx_storage.data()));
  MSPM0_CHECK(rx_dma.enabled);
  MSPM0_CHECK((DMA->CPU_INT.IMASK &
               (LibXR::MSPM0DmaDispatcher::EarlyMask(DMA_CH1_CHAN_ID) |
                LibXR::MSPM0DmaDispatcher::CompleteMask(DMA_CH1_CHAN_ID))) != 0U);
  MSPM0_CHECK((UART0->CPU_INT.IMASK & DL_UART_INTERRUPT_DMA_DONE_RX) == 0U);
  MSPM0_CHECK((UART0->CPU_INT.IMASK & DL_UART_INTERRUPT_RX) == 0U);
  MSPM0_CHECK(UART0->DMA_TRIG_RX.IMASK == DL_UART_DMA_INTERRUPT_RX);
  MSPM0_CHECK((UART0->CPU_INT.IMASK & DL_UART_INTERRUPT_LINC0_MATCH) != 0U);
  MSPM0_CHECK((UART0->CPU_INT.IMASK & (DL_UART_INTERRUPT_RXD_NEG_EDGE |
                                       DL_UART_INTERRUPT_RX_TIMEOUT_ERROR)) == 0U);
  MSPM0_CHECK(UART0->lin_counter_enabled);
  MSPM0_CHECK(UART0->lin_compare_enabled);
  MSPM0_CHECK(UART0->lin_compare_value == 5278U);

  fixture.EnforceOwner();
  FakeMSPM0::ResetDmaCallTrace();
  std::array<uint8_t, 8U> first_cycle{};
  OperationPollingStatus first_read_status;
  ReadOperation first_read(first_read_status);
  MSPM0_CHECK(fixture.uart.Read(RawData{first_cycle.data(), first_cycle.size()},
                                first_read) == ErrorCode::OK);

  const uint32_t disable_calls = rx_dma.disable_calls;
  const uint32_t enable_calls = rx_dma.enable_calls;
  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x10U, 0x11U, 0x12U, 0x13U}));
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].transfer_size ==
              fixture.rx_storage.size() / 2U);
  MSPM0_CHECK(NVIC_GetPendingIRQ(DMA_INT_IRQn) != 0U);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) == 0U);
  const size_t broker_trace_begin = FakeMSPM0::dma_call_trace_size;
  const size_t queue_size_before_broker = fixture.uart._read_port.queue_data_->Size();
  ServiceDma();
  MSPM0_CHECK(DmaTraceIsControllerOnly(broker_trace_begin));
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) != 0U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == queue_size_before_broker);
  MSPM0_CHECK(FakeMSPM0::uart_mmio_call_count == 0U);
  const size_t uart_trace_begin = FakeMSPM0::dma_call_trace_size;
  ServiceUart(UART0, 0U);
  bool saw_size_access = false;
  MSPM0_CHECK(DmaSizeTraceIsOwnedBy(UART0, uart_trace_begin, saw_size_access));
  MSPM0_CHECK(saw_size_access);
  MSPM0_CHECK(first_read_status.Load() == OperationPollingStatus::RUNNING);

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x14U, 0x15U, 0x16U, 0x17U}));
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].transfer_size ==
              fixture.rx_storage.size());
  ServiceDma();
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(first_read_status.Load() == OperationPollingStatus::DONE);
  const std::array<uint8_t, 8U> expected_first{0x10U, 0x11U, 0x12U, 0x13U,
                                               0x14U, 0x15U, 0x16U, 0x17U};
  MSPM0_CHECK(first_cycle == expected_first);

  MSPM0_CHECK(AdvanceAndDispatch({0x20U, 0x21U, 0x22U, 0x23U}));
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(AdvanceAndDispatch({0x24U, 0x25U, 0x26U, 0x27U}));
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 8U> expected_second{0x20U, 0x21U, 0x22U, 0x23U,
                                                0x24U, 0x25U, 0x26U, 0x27U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, expected_second));
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].wrap_count == 2U);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].disable_calls == disable_calls);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].enable_calls == enable_calls);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestPartialFlushAcrossHalfAndFullPublishesOnlySuffix()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();
  const auto& rx_dma = FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID];
  const uint32_t disable_calls = rx_dma.disable_calls;
  const uint32_t enable_calls = rx_dma.enable_calls;

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x20U, 0x21U, 0x22U}));
  FakeMSPM0::RaiseCpuInterrupt(UART0, DL_UART_INTERRUPT_LINC0_MATCH);
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 3U> first_prefix{0x20U, 0x21U, 0x22U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, first_prefix));

  FakeMSPM0::RaiseCpuInterrupt(UART0, DL_UART_INTERRUPT_LINC0_MATCH);
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x23U}));
  ServiceDma();
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 1U> first_suffix{0x23U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, first_suffix));

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x24U, 0x25U}));
  FakeMSPM0::RaiseCpuInterrupt(UART0, DL_UART_INTERRUPT_LINC0_MATCH);
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 2U> second_prefix{0x24U, 0x25U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, second_prefix));

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x26U, 0x27U}));
  ServiceDma();
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 2U> second_suffix{0x26U, 0x27U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, second_suffix));
  MSPM0_CHECK(rx_dma.disable_calls == disable_calls);
  MSPM0_CHECK(rx_dma.enable_calls == enable_calls);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestUart7FullOnlyAndSimultaneousExtendRouting()
{
  {
    ResetHarness();
    Extend7Fixture fixture;
    MSPM0_CHECK(fixture.rx_mapping != FakeMSPM0::kInvalidMcuAddress);
    MSPM0_CHECK(fixture.uart.GetRxMode() == MSPM0UART::RxMode::EXTEND_DMA);
    MSPM0_CHECK(!fixture.uart.RxHalfInterruptEnabled());
    const auto& rx_dma = FakeMSPM0::dma_channels[DMA_CH3_CHAN_ID];
    MSPM0_CHECK(rx_dma.config.trigger == DMA_UART7_RX_TRIG);
    MSPM0_CHECK(rx_dma.config.transferMode == DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE);
    MSPM0_CHECK(rx_dma.early_threshold == DL_DMA_EARLY_INTERRUPT_THRESHOLD_DISABLED);
    MSPM0_CHECK((DMA->CPU_INT.IMASK &
                 LibXR::MSPM0DmaDispatcher::CompleteMask(DMA_CH3_CHAN_ID)) != 0U);
    MSPM0_CHECK((DMA->CPU_INT.IMASK &
                 LibXR::MSPM0DmaDispatcher::EarlyMask(DMA_CH3_CHAN_ID)) == 0U);
    const uint32_t disable_calls = rx_dma.disable_calls;
    const uint32_t enable_calls = rx_dma.enable_calls;
    FakeMSPM0::BeginUartOwnerEnforcement();
    const std::array<uint8_t, 8U> expected{0x90U, 0x91U, 0x92U, 0x93U,
                                           0x94U, 0x95U, 0x96U, 0x97U};
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH3_CHAN_ID, expected));
    ServiceDma();
    ServiceUart(UART7, 7U);
    MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, expected));
    MSPM0_CHECK(rx_dma.disable_calls == disable_calls);
    MSPM0_CHECK(rx_dma.enable_calls == enable_calls);
  }

  {
    ResetHarness();
    ExtendFixture uart0_fixture;
    Extend7Fixture uart7_fixture;
    FakeMSPM0::BeginUartOwnerEnforcement();
    const std::array<uint8_t, 4U> uart0_bytes{0xA0U, 0xA1U, 0xA2U, 0xA3U};
    const std::array<uint8_t, 8U> uart7_bytes{0xB0U, 0xB1U, 0xB2U, 0xB3U,
                                              0xB4U, 0xB5U, 0xB6U, 0xB7U};
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, uart0_bytes));
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH3_CHAN_ID, uart7_bytes));
    ServiceDma();
    MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) != 0U);
    MSPM0_CHECK(NVIC_GetPendingIRQ(UART7_INT_IRQn) != 0U);
    MSPM0_CHECK(uart0_fixture.uart._read_port.queue_data_->Size() == 0U);
    MSPM0_CHECK(uart7_fixture.uart._read_port.queue_data_->Size() == 0U);
    ServiceUart(UART7, 7U);
    ServiceUart(UART0, 0U);
    MSPM0_CHECK(ExpectQueuedBytes(uart0_fixture.uart, uart0_bytes));
    MSPM0_CHECK(ExpectQueuedBytes(uart7_fixture.uart, uart7_bytes));
    MSPM0_CHECK(uart0_fixture.uart.GetRxDeadlineViolationCount() == 0U);
    MSPM0_CHECK(uart7_fixture.uart.GetRxDeadlineViolationCount() == 0U);
    MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  }
  return true;
}

bool TestPendingBoundaryFactsAreDiscardedOnErrors()
{
  {
    ResetHarness();
    ExtendFixture fixture;
    fixture.EnforceOwner();
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xC0U, 0xC1U, 0xC2U, 0xC3U}));
    ServiceDma();
    MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) != 0U);
    FakeMSPM0::RaiseCpuInterrupt(UART0, DL_UART_INTERRUPT_PARITY_ERROR);
    ServiceUart(UART0, 0U);
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
    MSPM0_CHECK(fixture.uart.GetRxParityErrorCount() == 1U);
    MSPM0_CHECK(fixture.uart.GetRxStaleEventCount() == 1U);
    MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
  }

  {
    ResetHarness();
    Extend7Fixture fixture;
    FakeMSPM0::BeginUartOwnerEnforcement();
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(
        DMA_CH3_CHAN_ID, {0xD0U, 0xD1U, 0xD2U, 0xD3U, 0xD4U, 0xD5U, 0xD6U, 0xD7U}));
    ServiceDma();
    MSPM0_CHECK(NVIC_GetPendingIRQ(UART7_INT_IRQn) != 0U);
    FakeMSPM0::RaiseDmaInterrupt(DL_DMA_INTERRUPT_DATA_ERROR);
    ServiceDma();
    ServiceUart(UART7, 7U);
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
    MSPM0_CHECK(fixture.uart.GetDmaErrorCount() == 1U);
    MSPM0_CHECK(fixture.uart.GetRxStaleEventCount() == 1U);
    MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
    MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  }
  return true;
}

bool TestChangingGapSnapshotRetriesInUartOwner()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();
  gap_retry_injection_count = 0U;
  gap_retry_advance_ok = false;

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xC0U}));
  FakeMSPM0::dma_access_hook = AdvanceRxBetweenGapSamples;
  FakeMSPM0::RaiseCpuInterrupt(UART0, DL_UART_INTERRUPT_LINC0_MATCH);
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(gap_retry_injection_count == 1U);
  MSPM0_CHECK(gap_retry_advance_ok);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) != 0U);

  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 2U> expected{0xC0U, 0xC1U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, expected));
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestQueueFullBoundaryAdvancesCursorWithoutRetry()
{
  ResetHarness();
  ExtendFixture fixture(2U);
  fixture.EnforceOwner();

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xE0U, 0xE1U, 0xE2U, 0xE3U}));
  ServiceDma();
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 2U> first_retained{0xE0U, 0xE1U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, first_retained));
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 2U);

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xE4U, 0xE5U, 0xE6U, 0xE7U}));
  ServiceDma();
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 2U> second_retained{0xE4U, 0xE5U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, second_retained));
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 4U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 2U);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestCoalescedAndRepeatedWatermarksAreLosses()
{
  {
    ResetHarness();
    ExtendFixture fixture;
    fixture.EnforceOwner();
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(
        DMA_CH1_CHAN_ID, {0x30U, 0x31U, 0x32U, 0x33U, 0x34U, 0x35U, 0x36U, 0x37U}));
    ServiceDma();
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
    ServiceUart(UART0, 0U);
    MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 1U);
    MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);
    MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  }

  {
    ResetHarness();
    ExtendFixture fixture;
    fixture.EnforceOwner();
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x40U, 0x41U, 0x42U, 0x43U}));
    ServiceDma();
    FakeMSPM0::RaiseDmaInterrupt(LibXR::MSPM0DmaDispatcher::EarlyMask(DMA_CH1_CHAN_ID));
    ServiceDma();
    ServiceUart(UART0, 0U);
    MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 1U);
    MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  }

  {
    ResetHarness();
    ExtendFixture fixture;
    fixture.EnforceOwner();
    MSPM0_CHECK(AdvanceAndDispatch({0x50U, 0x51U, 0x52U, 0x53U}));
    ServiceUart(UART0, 0U);
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 4U);
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x54U, 0x55U, 0x56U, 0x57U}));
    ServiceDma();
    FakeMSPM0::RaiseDmaInterrupt(
        LibXR::MSPM0DmaDispatcher::CompleteMask(DMA_CH1_CHAN_ID));
    ServiceDma();
    ServiceUart(UART0, 0U);
    MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 1U);
    MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 4U);
  }
  return true;
}

bool TestLateHalfFactIsRejectedAfterDmaCrossesFullBoundary()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xE0U, 0xE1U, 0xE2U, 0xE3U}));
  ServiceDma();
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) != 0U);
  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xE4U, 0xE5U, 0xE6U, 0xE7U}));
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].transfer_size ==
              fixture.rx_storage.size());
  MSPM0_CHECK(NVIC_GetPendingIRQ(DMA_INT_IRQn) != 0U);

  ServiceUart(UART0, 0U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestLateFullFactIsRejectedAfterNextHalfBoundary()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();

  MSPM0_CHECK(AdvanceAndDispatch({0xF0U, 0xF1U, 0xF2U, 0xF3U}));
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 4U> first_half{0xF0U, 0xF1U, 0xF2U, 0xF3U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, first_half));

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xF4U, 0xF5U, 0xF6U, 0xF7U}));
  ServiceDma();
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].transfer_size ==
              fixture.rx_storage.size());
  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xF8U, 0xF9U, 0xFAU, 0xFBU}));
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].transfer_size ==
              fixture.rx_storage.size() / 2U);
  MSPM0_CHECK(NVIC_GetPendingIRQ(DMA_INT_IRQn) != 0U);

  ServiceUart(UART0, 0U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestRawBoundaryRejectsAliasedHalfAtExactN()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x20U, 0x21U, 0x22U, 0x23U}));
  ServiceDma();
  MSPM0_CHECK(FakeMSPM0::AdvanceRx(
      DMA_CH1_CHAN_ID, {0x24U, 0x25U, 0x26U, 0x27U, 0x28U, 0x29U, 0x2AU, 0x2BU}));
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].transfer_size ==
              fixture.rx_storage.size() / 2U);
  const uint32_t boundaries = LibXR::MSPM0DmaDispatcher::EarlyMask(DMA_CH1_CHAN_ID) |
                              LibXR::MSPM0DmaDispatcher::CompleteMask(DMA_CH1_CHAN_ID);
  MSPM0_CHECK((DMA->CPU_INT.RIS & boundaries) == boundaries);

  ServiceUart(UART0, 0U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestQueuedBoundaryRejectsAliasedFullAtExact2N()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();
  queued_boundary_dispatch_count = 0U;
  queued_boundary_dma_irq_calls = 0U;
  queued_boundary_dma_owner_was_clear = false;

  MSPM0_CHECK(AdvanceAndDispatch({0x30U, 0x31U, 0x32U, 0x33U}));
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 4U> first_half{0x30U, 0x31U, 0x32U, 0x33U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, first_half));

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x34U, 0x35U, 0x36U, 0x37U}));
  ServiceDma();
  MSPM0_CHECK(FakeMSPM0::AdvanceRx(
      DMA_CH1_CHAN_ID, {0x38U, 0x39U, 0x3AU, 0x3BU, 0x3CU, 0x3DU, 0x3EU, 0x3FU}));
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].transfer_size ==
              fixture.rx_storage.size());
  FakeMSPM0::dma_access_hook = DispatchBoundaryBeforeRawRead;

  ServiceUart(UART0, 0U);
  MSPM0_CHECK(queued_boundary_dispatch_count == 1U);
  MSPM0_CHECK(queued_boundary_dma_irq_calls != 0U);
  MSPM0_CHECK(queued_boundary_dma_owner_was_clear);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestQueueLossAndErrorRecovery()
{
  ResetHarness();
  ExtendFixture fixture(4U);
  fixture.EnforceOwner();

  MSPM0_CHECK(AdvanceAndDispatch({0x60U, 0x61U, 0x62U, 0x63U}));
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(AdvanceAndDispatch({0x64U, 0x65U, 0x66U, 0x67U}));
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 4U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 4U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].enabled);
  const std::array<uint8_t, 4U> retained{0x60U, 0x61U, 0x62U, 0x63U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, retained));

  FakeMSPM0::InjectRx(UART0, 0x70U, UART_RXDATA_PARERR_MASK);
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(fixture.uart.GetRxParityErrorCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].enabled);

  FakeMSPM0::RaiseDmaInterrupt(DL_DMA_INTERRUPT_DATA_ERROR);
  ServiceDma();
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) != 0U);
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(fixture.uart.GetDmaErrorCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 2U);

  MSPM0_CHECK(AdvanceAndDispatch({0x80U, 0x81U, 0x82U, 0x83U}));
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 4U> clean{0x80U, 0x81U, 0x82U, 0x83U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, clean));
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestDmaSnapshotCannotCrossConfigRestart()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();
  config_preemption_count = 0U;

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0xA0U, 0xA1U, 0xA2U, 0xA3U}));
  const UART::Configuration config{57600U, UART::Parity::EVEN, 7U, 2U};
  MSPM0_CHECK(fixture.uart.SetConfig(config) == ErrorCode::OK);
  FakeMSPM0::primask_restore_hook = ServiceConfigAtFirstUnmask;

  ServiceDma();
  MSPM0_CHECK(config_preemption_count == 1U);
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(UART0->baudrate == config.baudrate);
  MSPM0_CHECK(UART0->parity == DL_UART_PARITY_EVEN);
  MSPM0_CHECK(UART0->lin_compare_value == 11667U);
  MSPM0_CHECK(fixture.uart.GetRxStaleEventCount() == 1U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);

  MSPM0_CHECK(AdvanceAndDispatch({0xB0U, 0xB1U, 0xB2U, 0xB3U}));
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 4U> clean{0xB0U, 0xB1U, 0xB2U, 0xB3U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, clean));
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestG3519MainBindingStillUsesByteIrq()
{
  ResetHarness();
  MainFixture fixture;
  MSPM0_CHECK(fixture.uart.GetRxMode() == MSPM0UART::RxMode::MAIN_BYTE_IRQ);
  MSPM0_CHECK((UART1->CPU_INT.IMASK & DL_UART_INTERRUPT_RX) != 0U);
  MSPM0_CHECK(UART1->DMA_TRIG_RX.IMASK == 0U);

  const UART::Configuration config{9600U, UART::Parity::EVEN, 8U, 2U};
  MSPM0_CHECK(fixture.uart.SetConfig(config) == ErrorCode::OK);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(UART1->baudrate == config.baudrate);
  MSPM0_CHECK(UART1->parity == DL_UART_PARITY_EVEN);
  MSPM0_CHECK(UART1->word_length == DL_UART_WORD_LENGTH_8_BITS);
  MSPM0_CHECK(UART1->stop_bits == DL_UART_STOP_BITS_TWO);
  MSPM0_CHECK(!UART1->lin_counter_enabled);
  MSPM0_CHECK(!UART1->lin_compare_enabled);
  MSPM0_CHECK(UART1->lin_compare_value == 0U);
  MSPM0_CHECK((UART1->CPU_INT.IMASK & DL_UART_INTERRUPT_LINC0_MATCH) == 0U);

  uint8_t received = 0U;
  OperationPollingStatus status;
  ReadOperation operation(status);
  MSPM0_CHECK(fixture.uart.Read(RawData{&received, 1U}, operation) == ErrorCode::OK);
  FakeMSPM0::InjectRx(UART1, 0x91U);
  ServiceUart(UART1, 1U);
  MSPM0_CHECK(status.Load() == OperationPollingStatus::DONE);
  MSPM0_CHECK(received == 0x91U);
  return true;
}

}  // namespace

int main()
{
  if (!TestConstructorPreservesHalfWakeup() || !TestConstructorPreservesErrorWakeup() ||
      !TestG3519CapabilitiesAndContinuousRing() ||
      !TestPartialFlushAcrossHalfAndFullPublishesOnlySuffix() ||
      !TestUart7FullOnlyAndSimultaneousExtendRouting() ||
      !TestPendingBoundaryFactsAreDiscardedOnErrors() ||
      !TestChangingGapSnapshotRetriesInUartOwner() ||
      !TestQueueFullBoundaryAdvancesCursorWithoutRetry() ||
      !TestCoalescedAndRepeatedWatermarksAreLosses() ||
      !TestLateHalfFactIsRejectedAfterDmaCrossesFullBoundary() ||
      !TestLateFullFactIsRejectedAfterNextHalfBoundary() ||
      !TestRawBoundaryRejectsAliasedHalfAtExactN() ||
      !TestQueuedBoundaryRejectsAliasedFullAtExact2N() ||
      !TestQueueLossAndErrorRecovery() || !TestDmaSnapshotCannotCrossConfigRestart() ||
      !TestG3519MainBindingStillUsesByteIrq())
  {
    return 1;
  }
  std::cout << "MSPM0 G3519 UART tests passed\n";
  return 0;
}
