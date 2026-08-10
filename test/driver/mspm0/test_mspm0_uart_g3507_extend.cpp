#include "mspm0_uart_common_cases.hpp"

namespace
{

using LibXR::ErrorCode;
using LibXR::MSPM0UART;
using LibXR::UART;
using namespace MSPM0UartTest;

static_assert(DMA_SYS_N_DMA_CHANNEL == 7U);
static_assert(DMA_SYS_N_DMA_FULL_CHANNEL == 3U);
static_assert(MSPM0UART::ResolveIndex(UART0_INT_IRQn) == 0U);
static_assert(UART_0_LIBXR_EXTEND_CAPABLE == 1);
static_assert(DMA_CH1_LIBXR_FULL_CHANNEL == 1);
static_assert(DMA_CH1_LIBXR_HALF_INTERRUPT == 0);
static_assert(!LibXR::MSPM0DmaDispatcher::EarlyInterruptSupported(DMA_CH1_CHAN_ID));

struct ExtendFixture
{
  explicit ExtendFixture(uint32_t rx_queue_capacity = 16U)
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

void RaisePartialFlush()
{
  FakeMSPM0::RaiseCpuInterrupt(UART0, DL_UART_INTERRUPT_LINC0_MATCH);
  ServiceUart(UART0, 0U);
}

bool TestG3507ExtendUsesFullOnlyRepeatDma()
{
  ResetHarness();
  ExtendFixture fixture;
  MSPM0_CHECK(fixture.rx_mapping != FakeMSPM0::kInvalidMcuAddress);
  MSPM0_CHECK(fixture.uart.GetRxMode() == MSPM0UART::RxMode::EXTEND_DMA);
  MSPM0_CHECK(!fixture.uart.RxHalfInterruptEnabled());

  const auto& rx_dma = FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID];
  MSPM0_CHECK(rx_dma.full_channel);
  MSPM0_CHECK(rx_dma.config.trigger == DMA_UART0_RX_TRIG);
  MSPM0_CHECK(rx_dma.config.transferMode == DL_DMA_FULL_CH_REPEAT_SINGLE_TRANSFER_MODE);
  MSPM0_CHECK(rx_dma.early_threshold == DL_DMA_EARLY_INTERRUPT_THRESHOLD_DISABLED);
  MSPM0_CHECK(rx_dma.programmed_transfer_size == fixture.rx_storage.size());
  MSPM0_CHECK(rx_dma.enabled);
  MSPM0_CHECK((DMA->CPU_INT.IMASK &
               LibXR::MSPM0DmaDispatcher::CompleteMask(DMA_CH1_CHAN_ID)) != 0U);
  MSPM0_CHECK(
      (DMA->CPU_INT.IMASK & LibXR::MSPM0DmaDispatcher::EarlyMask(DMA_CH1_CHAN_ID)) == 0U);
  MSPM0_CHECK((UART0->CPU_INT.IMASK & DL_UART_INTERRUPT_RX) == 0U);
  MSPM0_CHECK((UART0->CPU_INT.IMASK & DL_UART_INTERRUPT_DMA_DONE_RX) == 0U);
  MSPM0_CHECK((UART0->CPU_INT.IMASK & DL_UART_INTERRUPT_LINC0_MATCH) != 0U);
  MSPM0_CHECK(UART0->DMA_TRIG_RX.IMASK == DL_UART_DMA_INTERRUPT_RX);
  MSPM0_CHECK(UART0->lin_counter_enabled);
  MSPM0_CHECK(UART0->lin_compare_enabled);
  MSPM0_CHECK(UART0->lin_compare_value == 5278U);

  fixture.EnforceOwner();
  const uint32_t disable_calls = rx_dma.disable_calls;
  const uint32_t enable_calls = rx_dma.enable_calls;
  const std::array<uint8_t, 8U> expected{0x10U, 0x11U, 0x12U, 0x13U,
                                         0x14U, 0x15U, 0x16U, 0x17U};
  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, expected));
  ServiceDma();
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, expected));
  MSPM0_CHECK(rx_dma.wrap_count == 1U);
  MSPM0_CHECK(rx_dma.disable_calls == disable_calls);
  MSPM0_CHECK(rx_dma.enable_calls == enable_calls);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestPartialFlushPublishesOnceAndFullPublishesSuffix()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();
  const auto& rx_dma = FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID];
  const uint32_t disable_calls = rx_dma.disable_calls;
  const uint32_t enable_calls = rx_dma.enable_calls;

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x20U, 0x21U, 0x22U}));
  RaisePartialFlush();
  const std::array<uint8_t, 3U> prefix{0x20U, 0x21U, 0x22U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, prefix));

  RaisePartialFlush();
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x23U, 0x24U, 0x25U, 0x26U, 0x27U}));
  ServiceDma();
  ServiceUart(UART0, 0U);
  const std::array<uint8_t, 5U> suffix{0x23U, 0x24U, 0x25U, 0x26U, 0x27U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, suffix));
  MSPM0_CHECK(rx_dma.disable_calls == disable_calls);
  MSPM0_CHECK(rx_dma.enable_calls == enable_calls);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestPartialFlushDefersToRawAndQueuedFullBoundaries()
{
  {
    ResetHarness();
    ExtendFixture fixture;
    fixture.EnforceOwner();
    const std::array<uint8_t, 8U> expected{0x30U, 0x31U, 0x32U, 0x33U,
                                           0x34U, 0x35U, 0x36U, 0x37U};
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, expected));
    RaisePartialFlush();
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
    ServiceDma();
    ServiceUart(UART0, 0U);
    MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, expected));
    MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  }

  {
    ResetHarness();
    ExtendFixture fixture;
    fixture.EnforceOwner();
    const std::array<uint8_t, 8U> expected{0x40U, 0x41U, 0x42U, 0x43U,
                                           0x44U, 0x45U, 0x46U, 0x47U};
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, expected));
    ServiceDma();
    RaisePartialFlush();
    MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, expected));
    MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  }
  return true;
}

bool TestDelayedFullAfterNextCycleWriteIsLoss()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(
      DMA_CH1_CHAN_ID, {0x50U, 0x51U, 0x52U, 0x53U, 0x54U, 0x55U, 0x56U, 0x57U}));
  ServiceDma();
  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x58U}));
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestTwoUnservicedFullWrapsAliasToLatestRing()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(
      DMA_CH1_CHAN_ID, {0x60U, 0x61U, 0x62U, 0x63U, 0x64U, 0x65U, 0x66U, 0x67U, 0x68U,
                        0x69U, 0x6AU, 0x6BU, 0x6CU, 0x6DU, 0x6EU, 0x6FU}));
  MSPM0_CHECK(FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID].wrap_count == 2U);

  ServiceDma();
  ServiceUart(UART0, 0U);

  const std::array<uint8_t, 8U> latest_ring{0x68U, 0x69U, 0x6AU, 0x6BU,
                                            0x6CU, 0x6DU, 0x6EU, 0x6FU};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, latest_ring));
  MSPM0_CHECK(fixture.uart.GetRxDeadlineViolationCount() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 0U);
  MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestQueueDropAdvancesPartialCursor()
{
  ResetHarness();
  ExtendFixture fixture(2U);
  fixture.EnforceOwner();

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x60U, 0x61U, 0x62U}));
  RaisePartialFlush();
  const std::array<uint8_t, 2U> retained{0x60U, 0x61U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, retained));
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 1U);
  MSPM0_CHECK(fixture.uart.GetRxLossGeneration() == 1U);

  RaisePartialFlush();
  MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 1U);

  MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x63U}));
  RaisePartialFlush();
  const std::array<uint8_t, 1U> suffix{0x63U};
  MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, suffix));
  MSPM0_CHECK(fixture.uart.GetRxDropCount() == 1U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

bool TestConfigAndErrorClearStalePartialState()
{
  {
    ResetHarness();
    ExtendFixture fixture;
    fixture.EnforceOwner();
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x70U, 0x71U, 0x72U}));
    FakeMSPM0::RaiseCpuInterrupt(UART0, DL_UART_INTERRUPT_LINC0_MATCH);
    const UART::Configuration config{230400U, UART::Parity::EVEN, 7U, 2U};
    MSPM0_CHECK(fixture.uart.SetConfig(config) == ErrorCode::OK);
    ServiceUart(UART0, 0U);
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
    MSPM0_CHECK(UART0->baudrate == config.baudrate);
    MSPM0_CHECK(UART0->lin_compare_value == 2917U);

    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x73U, 0x74U}));
    RaisePartialFlush();
    const std::array<uint8_t, 2U> clean{0x73U, 0x74U};
    MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, clean));
  }

  {
    ResetHarness();
    ExtendFixture fixture;
    fixture.EnforceOwner();
    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x80U, 0x81U, 0x82U}));
    FakeMSPM0::RaiseCpuInterrupt(
        UART0, DL_UART_INTERRUPT_LINC0_MATCH | DL_UART_INTERRUPT_PARITY_ERROR);
    ServiceUart(UART0, 0U);
    MSPM0_CHECK(fixture.uart._read_port.queue_data_->Size() == 0U);
    MSPM0_CHECK(fixture.uart.GetRxParityErrorCount() == 1U);
    MSPM0_CHECK(fixture.uart.GetRecoveryCount() == 1U);

    MSPM0_CHECK(FakeMSPM0::AdvanceRx(DMA_CH1_CHAN_ID, {0x83U, 0x84U}));
    RaisePartialFlush();
    const std::array<uint8_t, 2U> clean{0x83U, 0x84U};
    MSPM0_CHECK(ExpectQueuedBytes(fixture.uart, clean));
  }
  return true;
}

bool TestGapCompareOverflowIsRejectedBeforeConfigAdmission()
{
  ResetHarness();
  ExtendFixture fixture;
  fixture.EnforceOwner();
  const auto& rx_dma = FakeMSPM0::dma_channels[DMA_CH1_CHAN_ID];
  const uint32_t disable_calls = rx_dma.disable_calls;
  const uint32_t enable_calls = rx_dma.enable_calls;
  const size_t uart_mmio_calls = FakeMSPM0::uart_mmio_call_count;
  NVIC_ClearPendingIRQ(UART0_INT_IRQn);

  const UART::Configuration invalid{9600U, UART::Parity::EVEN, 8U, 2U};
  MSPM0_CHECK(fixture.uart.SetConfig(invalid) == ErrorCode::ARG_ERR);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) == 0U);
  MSPM0_CHECK(FakeMSPM0::uart_mmio_call_count == uart_mmio_calls);
  MSPM0_CHECK(rx_dma.disable_calls == disable_calls);
  MSPM0_CHECK(rx_dma.enable_calls == enable_calls);
  MSPM0_CHECK(UART0->baudrate == 115200U);
  MSPM0_CHECK(UART0->word_length == DL_UART_WORD_LENGTH_8_BITS);
  MSPM0_CHECK(UART0->parity == DL_UART_PARITY_NONE);
  MSPM0_CHECK(UART0->stop_bits == DL_UART_STOP_BITS_ONE);
  MSPM0_CHECK(UART0->lin_compare_value == 5278U);

  const UART::Configuration valid{57600U, UART::Parity::EVEN, 7U, 2U};
  MSPM0_CHECK(fixture.uart.SetConfig(valid) == ErrorCode::OK);
  MSPM0_CHECK(NVIC_GetPendingIRQ(UART0_INT_IRQn) != 0U);
  ServiceUart(UART0, 0U);
  MSPM0_CHECK(UART0->baudrate == valid.baudrate);
  MSPM0_CHECK(UART0->lin_compare_value == 11667U);
  MSPM0_CHECK(FakeMSPM0::uart_owner_violation_count == 0U);
  return true;
}

}  // namespace

int main()
{
  if (!TestG3507ExtendUsesFullOnlyRepeatDma() ||
      !TestPartialFlushPublishesOnceAndFullPublishesSuffix() ||
      !TestPartialFlushDefersToRawAndQueuedFullBoundaries() ||
      !TestDelayedFullAfterNextCycleWriteIsLoss() ||
      !TestTwoUnservicedFullWrapsAliasToLatestRing() ||
      !TestQueueDropAdvancesPartialCursor() ||
      !TestConfigAndErrorClearStalePartialState() ||
      !TestGapCompareOverflowIsRejectedBeforeConfigAdmission())
  {
    return 1;
  }
  std::cout << "MSPM0 G3507 Extend UART tests passed\n";
  return 0;
}
