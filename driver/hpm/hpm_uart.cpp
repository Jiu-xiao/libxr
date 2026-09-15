#include "hpm_uart.hpp"

#include <algorithm>

#include "hpm_common.h"
#include "hpm_interrupt.h"
#include "hpm_l1c_drv.h"

#if __has_include("board.h")
#include "board.h"
#define LIBXR_HPM_UART_HAS_BOARD_HELPER 1
#else
#define LIBXR_HPM_UART_HAS_BOARD_HELPER 0
#endif

using namespace LibXR;

namespace
{

static constexpr uint32_t HPM_UART_IRQ_MASK = uart_intr_rx_line_stat
#if defined(HPM_IP_FEATURE_UART_RX_IDLE_DETECT) && \
    (HPM_IP_FEATURE_UART_RX_IDLE_DETECT == 1)
                                              | uart_intr_rx_line_idle
#endif
#if defined(HPM_IP_FEATURE_UART_RX_LINE_ERROR_DETECT) && \
    (HPM_IP_FEATURE_UART_RX_LINE_ERROR_DETECT == 1)
                                              | uart_intr_errf | uart_intr_break_err |
                                              uart_intr_framing_err |
                                              uart_intr_parity_err | uart_intr_overrun
#endif
    ;

static constexpr uint32_t HPM_UART_RX_ERROR_STATUS_MASK =
    uart_stat_overrun_error | uart_stat_parity_error | uart_stat_framing_error |
    uart_stat_line_break | uart_stat_rx_fifo_error;

template <typename T>
uint32_t HPMUARTToDmaAddr(T* ptr)
{
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ptr));
}

uintptr_t HPMUARTCacheAlignDown(uintptr_t addr)
{
  return addr & ~(static_cast<uintptr_t>(HPM_L1C_CACHELINE_SIZE) - 1u);
}

uintptr_t HPMUARTCacheAlignUp(uintptr_t addr)
{
  return (addr + HPM_L1C_CACHELINE_SIZE - 1u) &
         ~(static_cast<uintptr_t>(HPM_L1C_CACHELINE_SIZE) - 1u);
}

void HPMUARTCleanDCache(void* addr, size_t size)
{
  if (addr == nullptr || size == 0 || !l1c_dc_is_enabled())
  {
    return;
  }

  const uintptr_t start = HPMUARTCacheAlignDown(reinterpret_cast<uintptr_t>(addr));
  const uintptr_t end = HPMUARTCacheAlignUp(reinterpret_cast<uintptr_t>(addr) + size);
  l1c_dc_writeback(static_cast<uint32_t>(start), static_cast<uint32_t>(end - start));
}

void HPMUARTInvalidateDCache(void* addr, size_t size)
{
  if (addr == nullptr || size == 0 || !l1c_dc_is_enabled())
  {
    return;
  }

  const uintptr_t start = HPMUARTCacheAlignDown(reinterpret_cast<uintptr_t>(addr));
  const uintptr_t end = HPMUARTCacheAlignUp(reinterpret_cast<uintptr_t>(addr) + size);
  l1c_dc_invalidate(static_cast<uint32_t>(start), static_cast<uint32_t>(end - start));
}

uint8_t HPMUARTDmamuxChannel(DMAV2_Type* dma, uint8_t dma_channel)
{
  return static_cast<uint8_t>(DMA_SOC_CHN_TO_DMAMUX_CHN(dma, dma_channel));
}

#if defined(HPM_IP_FEATURE_UART_DMA_STOP) && (HPM_IP_FEATURE_UART_DMA_STOP == 1) && \
    defined(UART_FCRR_DMA_STOPPED_WRITE_CLEAR_MASK)
void HPMUARTClearRxDmaAutoStopFlag(UART_Type* ptr)
{
  ptr->FCRR = ptr->FCRR | UART_FCRR_DMA_STOPPED_WRITE_CLEAR_MASK;
}
#endif

template <typename T>
uint32_t HPMUARTToSystemDmaAddr(T* ptr)
{
#if defined(BOARD_RUNNING_CORE)
  return core_local_mem_to_sys_address(BOARD_RUNNING_CORE, HPMUARTToDmaAddr(ptr));
#else
  return core_local_mem_to_sys_address(HPM_CORE0, HPMUARTToDmaAddr(ptr));
#endif
}

#if LIBXR_HPM_UART_HAS_DMA_MGR
ErrorCode HPMUARTConvertDmaMgrStatus(hpm_stat_t status)
{
  switch (status)
  {
    case status_success:
      return ErrorCode::OK;
    case status_invalid_argument:
      return ErrorCode::ARG_ERR;
    case status_dma_mgr_no_resource:
      return ErrorCode::FULL;
    default:
      return ErrorCode::FAILED;
  }
}
#endif

}  // namespace

HPMUART* HPMUART::instances_[HPMUART::MAX_UART_INSTANCES] = {};
HPMUART* HPMUART::rx_dma_map_[DMA_SOC_CHANNEL_NUM] = {};
HPMUART* HPMUART::tx_dma_map_[DMA_SOC_CHANNEL_NUM] = {};

HPMUART::HPMUART(Resources res, RawData dma_buff_rx, RawData dma_buff_tx,
                 uint32_t tx_queue_size, UART::Configuration config, bool auto_board_init)
    : UART(&_read_port, &_write_port),
      _read_port(dma_buff_rx.size_),
      _write_port(tx_queue_size, dma_buff_tx.size_ / 2),
      dma_buff_rx_(dma_buff_rx),
      dma_buff_tx_(dma_buff_tx),
      res_(NormalizeResources(res)),
      auto_board_init_(auto_board_init)
{
  ASSERT(res_.instance != nullptr);
  ASSERT(res_.irq != INVALID_IRQ);
  ASSERT(res_.index < MAX_UART_INSTANCES);
  ASSERT(instances_[res_.index] == nullptr);
  ASSERT(res_.dma != nullptr);
  ASSERT(res_.dmamux != nullptr);
  ASSERT(res_.dma_irq != INVALID_IRQ);
  ASSERT(res_.rx_dma_channel < DMA_SOC_CHANNEL_NUM);
  ASSERT(res_.tx_dma_channel < DMA_SOC_CHANNEL_NUM);
  ASSERT(res_.rx_dma_channel != res_.tx_dma_channel);
  ASSERT(res_.rx_dma_req != INVALID_DMA_REQ);
  ASSERT(res_.tx_dma_req != INVALID_DMA_REQ);
  ASSERT(dma_buff_rx_.addr_ != nullptr);
  ASSERT(dma_buff_rx_.size_ > 1 && dma_buff_rx_.size_ % 2U == 0);
  ASSERT(dma_buff_rx_.size_ <= UINT32_MAX);
  ASSERT(dma_buff_tx.addr_ != nullptr);
  ASSERT(dma_buff_tx.size_ >= 2);
  ASSERT(dma_buff_tx.size_ / 2 > 0);
  ASSERT(tx_queue_size > 0);

  _write_port = WriteFun;

  instances_[res_.index] = this;

  clock_add_to_group(clock_hdma, 0);

  const ErrorCode dma_ans = PrepareDmaChannels();
  ASSERT(dma_ans == ErrorCode::OK);
  ASSERT(res_.rx_dma_channel < DMA_SOC_CHANNEL_NUM);
  ASSERT(res_.tx_dma_channel < DMA_SOC_CHANNEL_NUM);
  ASSERT(res_.rx_dma_channel != res_.tx_dma_channel);
  ASSERT(rx_dma_map_[res_.rx_dma_channel] == nullptr);
  ASSERT(tx_dma_map_[res_.tx_dma_channel] == nullptr);
  ASSERT(tx_dma_map_[res_.rx_dma_channel] == nullptr);
  ASSERT(rx_dma_map_[res_.tx_dma_channel] == nullptr);
  rx_dma_map_[res_.rx_dma_channel] = this;
  tx_dma_map_[res_.tx_dma_channel] = this;

  clock_hz_ = ResolveClockHz();
  ASSERT(ValidateConfig(config) == ErrorCode::OK);
  const ErrorCode set_config_ans = ApplyConfig(config);
  ASSERT(set_config_ans == ErrorCode::OK);

  intc_m_enable_irq_with_priority(res_.irq, 1);
  intc_m_enable_irq_with_priority(res_.dma_irq, 1);
}

HPMUART::HPMUART(UART_Type* instance, clock_name_t clock, uint32_t irq,
                 RawData dma_buff_rx, RawData dma_buff_tx, uint8_t rx_dma_channel,
                 uint8_t tx_dma_channel, uint32_t tx_queue_size,
                 UART::Configuration config, bool auto_board_init)
    : HPMUART(MakeResources(instance, clock, irq, rx_dma_channel, tx_dma_channel),
              dma_buff_rx, dma_buff_tx, tx_queue_size, config, auto_board_init)
{
}

HPMUART::Resources HPMUART::MakeResources(UART_Type* instance, clock_name_t clock,
                                          uint32_t irq, uint8_t rx_dma_channel,
                                          uint8_t tx_dma_channel, DMAV2_Type* dma,
                                          DMAMUX_Type* dmamux, uint32_t dma_irq,
                                          uint8_t index)
{
  Resources res;
  res.instance = instance;
  res.irq = irq;
  res.clock = clock;
  res.dma = dma;
  res.dmamux = dmamux;
  res.dma_irq = dma_irq;
  res.rx_dma_channel = rx_dma_channel;
  res.tx_dma_channel = tx_dma_channel;
  res.index = index;
  return NormalizeResources(res);
}

HPMUART::Resources HPMUART::NormalizeResources(Resources res)
{
  if (res.index == INVALID_INSTANCE_INDEX)
  {
    res.index = ResolveIndex(res.instance);
  }

  if (res.irq == INVALID_IRQ)
  {
    res.irq = ResolveIrq(res.instance);
  }

  if (res.clock == INVALID_CLOCK)
  {
    res.clock = ResolveClock(res.instance);
  }

  if (res.rx_dma_req == INVALID_DMA_REQ)
  {
    res.rx_dma_req = ResolveRxDmaReq(res.instance);
  }

  if (res.tx_dma_req == INVALID_DMA_REQ)
  {
    res.tx_dma_req = ResolveTxDmaReq(res.instance);
  }

#if LIBXR_HPM_UART_HAS_BOARD_HELPER && defined(BOARD_APP_HDMA) && \
    defined(BOARD_APP_DMAMUX) && defined(BOARD_APP_HDMA_IRQ)
  if (res.dma == nullptr)
  {
    res.dma = BOARD_APP_HDMA;
  }
  if (res.dmamux == nullptr)
  {
    res.dmamux = BOARD_APP_DMAMUX;
  }
  if (res.dma_irq == INVALID_IRQ)
  {
    res.dma_irq = BOARD_APP_HDMA_IRQ;
  }
#endif

#if defined(HPM_HDMA)
  if (res.dma == nullptr)
  {
    res.dma = HPM_HDMA;
  }
#endif
#if defined(HPM_DMAMUX)
  if (res.dmamux == nullptr)
  {
    res.dmamux = HPM_DMAMUX;
  }
#endif
#if defined(IRQn_HDMA)
  if (res.dma_irq == INVALID_IRQ)
  {
    res.dma_irq = IRQn_HDMA;
  }
#endif

  return res;
}

uint8_t HPMUART::ResolveIndex(UART_Type* instance)
{
#if defined(HPM_UART0)
  if (instance == HPM_UART0)
  {
    return 0;
  }
#endif
#if defined(HPM_UART1)
  if (instance == HPM_UART1)
  {
    return 1;
  }
#endif
#if defined(HPM_UART2)
  if (instance == HPM_UART2)
  {
    return 2;
  }
#endif
#if defined(HPM_UART3)
  if (instance == HPM_UART3)
  {
    return 3;
  }
#endif
#if defined(HPM_UART4)
  if (instance == HPM_UART4)
  {
    return 4;
  }
#endif
#if defined(HPM_UART5)
  if (instance == HPM_UART5)
  {
    return 5;
  }
#endif
#if defined(HPM_UART6)
  if (instance == HPM_UART6)
  {
    return 6;
  }
#endif
#if defined(HPM_UART7)
  if (instance == HPM_UART7)
  {
    return 7;
  }
#endif
#if defined(HPM_PUART)
  if (instance == HPM_PUART)
  {
    return 8;
  }
#endif
  return INVALID_INSTANCE_INDEX;
}

uint8_t HPMUART::ResolveIndexByIrq(uint32_t irq)
{
#if defined(IRQn_UART0)
  if (irq == IRQn_UART0)
  {
    return 0;
  }
#endif
#if defined(IRQn_UART1)
  if (irq == IRQn_UART1)
  {
    return 1;
  }
#endif
#if defined(IRQn_UART2)
  if (irq == IRQn_UART2)
  {
    return 2;
  }
#endif
#if defined(IRQn_UART3)
  if (irq == IRQn_UART3)
  {
    return 3;
  }
#endif
#if defined(IRQn_UART4)
  if (irq == IRQn_UART4)
  {
    return 4;
  }
#endif
#if defined(IRQn_UART5)
  if (irq == IRQn_UART5)
  {
    return 5;
  }
#endif
#if defined(IRQn_UART6)
  if (irq == IRQn_UART6)
  {
    return 6;
  }
#endif
#if defined(IRQn_UART7)
  if (irq == IRQn_UART7)
  {
    return 7;
  }
#endif
#if defined(IRQn_PUART)
  if (irq == IRQn_PUART)
  {
    return 8;
  }
#endif
  return INVALID_INSTANCE_INDEX;
}

uint32_t HPMUART::ResolveIrq(UART_Type* instance)
{
#if defined(HPM_UART0) && defined(IRQn_UART0)
  if (instance == HPM_UART0)
  {
    return IRQn_UART0;
  }
#endif
#if defined(HPM_UART1) && defined(IRQn_UART1)
  if (instance == HPM_UART1)
  {
    return IRQn_UART1;
  }
#endif
#if defined(HPM_UART2) && defined(IRQn_UART2)
  if (instance == HPM_UART2)
  {
    return IRQn_UART2;
  }
#endif
#if defined(HPM_UART3) && defined(IRQn_UART3)
  if (instance == HPM_UART3)
  {
    return IRQn_UART3;
  }
#endif
#if defined(HPM_UART4) && defined(IRQn_UART4)
  if (instance == HPM_UART4)
  {
    return IRQn_UART4;
  }
#endif
#if defined(HPM_UART5) && defined(IRQn_UART5)
  if (instance == HPM_UART5)
  {
    return IRQn_UART5;
  }
#endif
#if defined(HPM_UART6) && defined(IRQn_UART6)
  if (instance == HPM_UART6)
  {
    return IRQn_UART6;
  }
#endif
#if defined(HPM_UART7) && defined(IRQn_UART7)
  if (instance == HPM_UART7)
  {
    return IRQn_UART7;
  }
#endif
#if defined(HPM_PUART) && defined(IRQn_PUART)
  if (instance == HPM_PUART)
  {
    return IRQn_PUART;
  }
#endif
  return INVALID_IRQ;
}

clock_name_t HPMUART::ResolveClock(UART_Type* instance)
{
#if defined(HPM_UART0)
  if (instance == HPM_UART0)
  {
    return clock_uart0;
  }
#endif
#if defined(HPM_UART1)
  if (instance == HPM_UART1)
  {
    return clock_uart1;
  }
#endif
#if defined(HPM_UART2)
  if (instance == HPM_UART2)
  {
    return clock_uart2;
  }
#endif
#if defined(HPM_UART3)
  if (instance == HPM_UART3)
  {
    return clock_uart3;
  }
#endif
#if defined(HPM_UART4)
  if (instance == HPM_UART4)
  {
    return clock_uart4;
  }
#endif
#if defined(HPM_UART5)
  if (instance == HPM_UART5)
  {
    return clock_uart5;
  }
#endif
#if defined(HPM_UART6)
  if (instance == HPM_UART6)
  {
    return clock_uart6;
  }
#endif
#if defined(HPM_UART7)
  if (instance == HPM_UART7)
  {
    return clock_uart7;
  }
#endif
#if defined(HPM_PUART)
  if (instance == HPM_PUART)
  {
    return clock_puart;
  }
#endif
  return INVALID_CLOCK;
}

uint8_t HPMUART::ResolveRxDmaReq(UART_Type* instance)
{
#if defined(HPM_UART0) && defined(HPM_DMA_SRC_UART0_RX)
  if (instance == HPM_UART0)
  {
    return HPM_DMA_SRC_UART0_RX;
  }
#endif
#if defined(HPM_UART1) && defined(HPM_DMA_SRC_UART1_RX)
  if (instance == HPM_UART1)
  {
    return HPM_DMA_SRC_UART1_RX;
  }
#endif
#if defined(HPM_UART2) && defined(HPM_DMA_SRC_UART2_RX)
  if (instance == HPM_UART2)
  {
    return HPM_DMA_SRC_UART2_RX;
  }
#endif
#if defined(HPM_UART3) && defined(HPM_DMA_SRC_UART3_RX)
  if (instance == HPM_UART3)
  {
    return HPM_DMA_SRC_UART3_RX;
  }
#endif
#if defined(HPM_UART4) && defined(HPM_DMA_SRC_UART4_RX)
  if (instance == HPM_UART4)
  {
    return HPM_DMA_SRC_UART4_RX;
  }
#endif
#if defined(HPM_UART5) && defined(HPM_DMA_SRC_UART5_RX)
  if (instance == HPM_UART5)
  {
    return HPM_DMA_SRC_UART5_RX;
  }
#endif
#if defined(HPM_UART6) && defined(HPM_DMA_SRC_UART6_RX)
  if (instance == HPM_UART6)
  {
    return HPM_DMA_SRC_UART6_RX;
  }
#endif
#if defined(HPM_UART7) && defined(HPM_DMA_SRC_UART7_RX)
  if (instance == HPM_UART7)
  {
    return HPM_DMA_SRC_UART7_RX;
  }
#endif
#if defined(HPM_PUART) && defined(HPM_DMA_SRC_PUART_RX)
  if (instance == HPM_PUART)
  {
    return HPM_DMA_SRC_PUART_RX;
  }
#endif
  return INVALID_DMA_REQ;
}

uint8_t HPMUART::ResolveTxDmaReq(UART_Type* instance)
{
#if defined(HPM_UART0) && defined(HPM_DMA_SRC_UART0_TX)
  if (instance == HPM_UART0)
  {
    return HPM_DMA_SRC_UART0_TX;
  }
#endif
#if defined(HPM_UART1) && defined(HPM_DMA_SRC_UART1_TX)
  if (instance == HPM_UART1)
  {
    return HPM_DMA_SRC_UART1_TX;
  }
#endif
#if defined(HPM_UART2) && defined(HPM_DMA_SRC_UART2_TX)
  if (instance == HPM_UART2)
  {
    return HPM_DMA_SRC_UART2_TX;
  }
#endif
#if defined(HPM_UART3) && defined(HPM_DMA_SRC_UART3_TX)
  if (instance == HPM_UART3)
  {
    return HPM_DMA_SRC_UART3_TX;
  }
#endif
#if defined(HPM_UART4) && defined(HPM_DMA_SRC_UART4_TX)
  if (instance == HPM_UART4)
  {
    return HPM_DMA_SRC_UART4_TX;
  }
#endif
#if defined(HPM_UART5) && defined(HPM_DMA_SRC_UART5_TX)
  if (instance == HPM_UART5)
  {
    return HPM_DMA_SRC_UART5_TX;
  }
#endif
#if defined(HPM_UART6) && defined(HPM_DMA_SRC_UART6_TX)
  if (instance == HPM_UART6)
  {
    return HPM_DMA_SRC_UART6_TX;
  }
#endif
#if defined(HPM_UART7) && defined(HPM_DMA_SRC_UART7_TX)
  if (instance == HPM_UART7)
  {
    return HPM_DMA_SRC_UART7_TX;
  }
#endif
#if defined(HPM_PUART) && defined(HPM_DMA_SRC_PUART_TX)
  if (instance == HPM_PUART)
  {
    return HPM_DMA_SRC_PUART_TX;
  }
#endif
  return INVALID_DMA_REQ;
}

ErrorCode HPMUART::PrepareDmaChannels()
{
#if LIBXR_HPM_UART_HAS_DMA_MGR
  if (dma_mgr_ready_)
  {
    return ErrorCode::OK;
  }

  dma_mgr_init();

  rx_dma_resource_.base = res_.dma;
  rx_dma_resource_.irq_num = static_cast<int32_t>(res_.dma_irq);
  tx_dma_resource_.base = res_.dma;
  tx_dma_resource_.irq_num = static_cast<int32_t>(res_.dma_irq);

  hpm_stat_t status = dma_mgr_request_specified_resource(&rx_dma_resource_, res_.dma);
  if (status != status_success)
  {
    return HPMUARTConvertDmaMgrStatus(status);
  }
  res_.rx_dma_channel = static_cast<uint8_t>(rx_dma_resource_.channel);

  status = dma_mgr_request_specified_resource(&tx_dma_resource_, res_.dma);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }
  res_.tx_dma_channel = static_cast<uint8_t>(tx_dma_resource_.channel);

  status = dma_mgr_install_chn_half_tc_callback(&rx_dma_resource_,
                                                &HPMUART::OnDmaMgrHalfTcCallback, this);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&tx_dma_resource_);
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }
  status = dma_mgr_install_chn_tc_callback(&rx_dma_resource_,
                                           &HPMUART::OnDmaMgrTcCallback, this);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&tx_dma_resource_);
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }
  status = dma_mgr_install_chn_error_callback(&rx_dma_resource_,
                                              &HPMUART::OnDmaMgrErrorCallback, this);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&tx_dma_resource_);
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }
  status = dma_mgr_install_chn_abort_callback(&rx_dma_resource_,
                                              &HPMUART::OnDmaMgrAbortCallback, this);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&tx_dma_resource_);
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }

  status = dma_mgr_install_chn_tc_callback(&tx_dma_resource_,
                                           &HPMUART::OnDmaMgrTcCallback, this);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&tx_dma_resource_);
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }
  status = dma_mgr_install_chn_error_callback(&tx_dma_resource_,
                                              &HPMUART::OnDmaMgrErrorCallback, this);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&tx_dma_resource_);
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }
  status = dma_mgr_install_chn_abort_callback(&tx_dma_resource_,
                                              &HPMUART::OnDmaMgrAbortCallback, this);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&tx_dma_resource_);
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }

  status = dma_mgr_enable_chn_irq(
      &rx_dma_resource_, DMA_MGR_INTERRUPT_MASK_TC | DMA_MGR_INTERRUPT_MASK_HALF_TC |
                             DMA_MGR_INTERRUPT_MASK_ERROR | DMA_MGR_INTERRUPT_MASK_ABORT);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&tx_dma_resource_);
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }
  status = dma_mgr_enable_chn_irq(&tx_dma_resource_, DMA_MGR_INTERRUPT_MASK_TC |
                                                         DMA_MGR_INTERRUPT_MASK_ERROR |
                                                         DMA_MGR_INTERRUPT_MASK_ABORT);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&tx_dma_resource_);
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }

  status = dma_mgr_enable_dma_irq_with_priority(&tx_dma_resource_, 1U);
  if (status != status_success)
  {
    (void)dma_mgr_release_resource(&tx_dma_resource_);
    (void)dma_mgr_release_resource(&rx_dma_resource_);
    return HPMUARTConvertDmaMgrStatus(status);
  }

  dma_mgr_ready_ = true;
#endif
  return ErrorCode::OK;
}

ErrorCode HPMUART::ValidateConfig(UART::Configuration config) const
{
  if (clock_hz_ == 0U || config.baudrate < 200U || config.data_bits < 5U ||
      config.data_bits > 8U || (config.stop_bits != 1U && config.stop_bits != 2U) ||
      (config.data_bits == 5U && config.stop_bits == 2U) ||
      (config.parity != UART::Parity::NO_PARITY && config.parity != UART::Parity::EVEN &&
       config.parity != UART::Parity::ODD))
  {
    return ErrorCode::ARG_ERR;
  }
  // Same divisor selection/tolerance as the SDK; validate without touching DLAB.
  const uint64_t baud = config.baudrate;
  if (uint64_t(clock_hz_) < baud * 8U || uint64_t(clock_hz_) / 65535U > baud * 32U)
  {
    return ErrorCode::ARG_ERR;
  }
  const uint64_t scaled = uint64_t(clock_hz_) * 1000U / baud;
#ifdef UART_SOC_OVERSAMPLE_MAX
  constexpr uint32_t max_oversample = UART_SOC_OVERSAMPLE_MAX;
#else
  constexpr uint32_t max_oversample = 32U;
#endif
#ifdef HPM_UART_BAUDRATE_TOLERANCE
  constexpr uint32_t tolerance = HPM_UART_BAUDRATE_TOLERANCE;
#else
  constexpr uint32_t tolerance = 3U;
#endif
  for (uint32_t osc = 8U; osc <= max_oversample; osc += 2U)
  {
    const uint64_t div = (scaled + osc * 500U) / (osc * 1000U);
    if (div == 0U || div > 65535U)
    {
      continue;
    }
    const uint64_t actual = div * osc * 1000U;
    const uint64_t error = actual > scaled ? actual - scaled : scaled - actual;
    if (error * 100U / scaled <= tolerance)
    {
      return ErrorCode::OK;
    }
  }
  return ErrorCode::ARG_ERR;
}

ErrorCode HPMUART::SetConfig(UART::Configuration config, bool in_isr)
{
  const auto valid = ValidateConfig(config);
  if (valid != ErrorCode::OK)
  {
    return valid;
  }
#if !defined(HPM_IP_FEATURE_UART_TX_IDLE_DETECT) || \
    (HPM_IP_FEATURE_UART_TX_IDLE_DETECT != 1)
  // Older UART IP has no line-idle IRQ: admission must be retried after TX drains.
  if ((uart_get_status(res_.instance) & uart_stat_transmitter_empty) == 0U)
  {
    return ErrorCode::BUSY;
  }
#endif
  ConfigState expected = ConfigState::EMPTY;
  if (!config_state_.compare_exchange_strong(expected, ConfigState::RESERVED,
                                             std::memory_order_acq_rel))
  {
    return ErrorCode::BUSY;
  }
  pending_config_ = config;
  Invoke(EVENT_CONFIG, in_isr);
  return ErrorCode::OK;
}

void HPMUART::Invoke(uint32_t events, bool in_isr)
{
  service_.Invoke(events, in_isr, [this](uint32_t work, bool owner_in_isr)
                  { HandleService(work, owner_in_isr); });
}

void HPMUART::WriteFun(WritePort& port, bool in_isr)
{
  auto* uart = LibXR::ContainerOf(&port, &HPMUART::_write_port);
  uart->Invoke(EVENT_WRITE, in_isr);
}

void HPMUART::HandleService(uint32_t events, bool in_isr)
{
  if ((events & EVENT_CONFIG) != 0U)
  {
    config_state_.store(ConfigState::PUBLISHED, std::memory_order_release);
  }
  if ((events & (EVENT_TX_DONE | EVENT_TX_ERROR)) != 0U)
  {
    // Completion refers to staging, so a later DMA fault cannot complete it again.
    // Drop the failed active transfer without replaying its already-sent prefix.
    if ((events & EVENT_TX_ERROR) != 0U)
    {
      dma_disable_channel(res_.dma, res_.tx_dma_channel);
      dma_clear_transfer_status(res_.dma, res_.tx_dma_channel);
    }
    tx_busy_.Clear();
    dma_buff_tx_.SetActiveLength(0U);
  }
  if ((events & EVENT_RX_ERROR) != 0U)
  {
    HandleErrorInterrupt();
    const auto result = StartRxDMA();
    REQUIRE_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
  }
  else if ((events & (EVENT_RX | EVENT_RX_IDLE)) != 0U)
  {
    DrainRx(in_isr, (events & EVENT_RX_IDLE) != 0U);
  }
  TryApplyConfig(in_isr);
  FillTx(in_isr);
}

void HPMUART::TryApplyConfig(bool in_isr)
{
  if (config_state_.load(std::memory_order_acquire) != ConfigState::PUBLISHED ||
      tx_busy_.IsSet())
  {
    return;
  }
  if ((uart_get_status(res_.instance) & uart_stat_transmitter_empty) == 0U)
  {
#if defined(HPM_IP_FEATURE_UART_TX_IDLE_DETECT) && \
    (HPM_IP_FEATURE_UART_TX_IDLE_DETECT == 1)
    uart_enable_irq(res_.instance, uart_intr_tx_line_idle);
#endif
    return;
  }
#if defined(HPM_IP_FEATURE_UART_TX_IDLE_DETECT) && \
    (HPM_IP_FEATURE_UART_TX_IDLE_DETECT == 1)
  uart_disable_irq(res_.instance, uart_intr_tx_line_idle);
  uart_clear_txline_idle_flag(res_.instance);
#endif
  // No active DMA/TX FIFO remains. Preserve already-published RX and both staged TX
  // halves.
  const auto result = ApplyConfig(pending_config_);
  REQUIRE_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
  config_state_.store(ConfigState::EMPTY, std::memory_order_release);
}

void HPMUART::FillTx(bool in_isr)
{
  if (config_state_.load(std::memory_order_acquire) != ConfigState::EMPTY)
  {
    return;
  }
  if (!tx_busy_.IsSet())
  {
    if (dma_buff_tx_.HasPending())
    {
      const size_t count = dma_buff_tx_.GetPendingLength();
      dma_buff_tx_.Switch();
      dma_buff_tx_.SetActiveLength(count);
    }
    if (dma_buff_tx_.GetActiveLength() == 0U)
    {
      auto queue = _write_port.GetWriteQueue(in_isr);
      if (queue.Empty())
      {
        return;
      }
      const size_t count = queue.AvailableSize();
      DEV_ASSERT_FROM_CALLBACK(count <= dma_buff_tx_.Size(), in_isr);
      queue.PopAll(dma_buff_tx_.ActiveBuffer());
      dma_buff_tx_.SetActiveLength(count);
    }
    // A completion callback above may have published configuration; retain staging.
    if (config_state_.load(std::memory_order_acquire) != ConfigState::EMPTY)
    {
      return;
    }
    tx_busy_.Set();
    const auto result = StartTxDMA(dma_buff_tx_.GetActiveLength());
    REQUIRE_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
  }
  if (!dma_buff_tx_.HasPending() &&
      config_state_.load(std::memory_order_acquire) == ConfigState::EMPTY)
  {
    auto queue = _write_port.GetWriteQueue(in_isr);
    if (!queue.Empty())
    {
      const size_t count = queue.AvailableSize();
      DEV_ASSERT_FROM_CALLBACK(count <= dma_buff_tx_.Size(), in_isr);
      queue.PopAll(dma_buff_tx_.PendingBuffer());
      dma_buff_tx_.SetPendingLength(count);
      dma_buff_tx_.EnablePending();
    }
  }
}

void HPMUART::ProcessRxDMA(bool in_isr) { Invoke(EVENT_RX, in_isr); }

void HPMUART::DrainRx(bool in_isr, bool idle_event)
{
  const size_t capacity = dma_buff_rx_.size_;
  // A second, larger remaining count can be a legitimate circular reload. Never
  // select the minimum of samples from opposite sides of that reload.
  const uint32_t remaining =
      dma_get_remaining_transfer_size(res_.dma, res_.rx_dma_channel);
  DEV_ASSERT_FROM_CALLBACK(remaining <= capacity, in_isr);
  size_t current = capacity - remaining;
  if (current == last_rx_pos_)
  {
    return;
  }
  if (current == 0U && last_rx_pos_ == capacity)
  {
    last_rx_pos_ = 0U;
    return;
  }
#if defined(UART_LSR_RXIDLE_MASK)
  // The running DMA may have fetched its last byte before the destination write.
  // Keep that byte until the next progress/idle event, as in the original backend.
  if (!idle_event && (res_.instance->LSR & UART_LSR_RXIDLE_MASK) == 0U &&
      current != last_rx_pos_)
  {
    current = current == 0U ? capacity - 1U : current - 1U;
  }
#endif
  const size_t previous = last_rx_pos_;
  if (current == previous)
  {
    return;
  }
  HPMUARTInvalidateDCache(dma_buff_rx_.addr_, capacity);
  auto* data = static_cast<uint8_t*>(dma_buff_rx_.addr_);
  auto queue = _read_port.GetReadQueue(in_isr);
  const size_t total =
      current >= previous ? current - previous : capacity - previous + current;
  const size_t accepted = std::min(total, queue.EmptySize());
  const size_t first = std::min(accepted, capacity - previous);
  if (first != 0U)
  {
    [[maybe_unused]] const auto result = queue.PushBatch(data + previous, first);
    DEV_ASSERT_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
  }
  if (accepted > first)
  {
    [[maybe_unused]] const auto result = queue.PushBatch(data, accepted - first);
    DEV_ASSERT_FROM_CALLBACK(result == ErrorCode::OK, in_isr);
  }
  rx_drop_count_ += total - accepted;
  // Keep the full-buffer boundary distinct until reload/progress is observed.
  // TC followed by IDLE at remaining==0 must not publish the same buffer twice.
  last_rx_pos_ = current;
  // Cursor state must be complete before Publish invokes application callbacks.
  queue.Publish();
}

void HPMUART::HandleUartInterrupt()
{
  uint32_t events = 0U;
  const uint8_t irq_id = uart_get_irq_id(res_.instance);
  if (irq_id == uart_intr_id_rx_line_stat ||
      (uart_get_status(res_.instance) & HPM_UART_RX_ERROR_STATUS_MASK) != 0U)
  {
    events |= EVENT_RX_ERROR;
  }
#if defined(HPM_IP_FEATURE_UART_RX_IDLE_DETECT) && \
    (HPM_IP_FEATURE_UART_RX_IDLE_DETECT == 1)
  if (uart_is_rxline_idle(res_.instance))
  {
    uart_clear_rxline_idle_flag(res_.instance);
    events |= EVENT_RX_IDLE;
  }
#endif
#if defined(HPM_IP_FEATURE_UART_TX_IDLE_DETECT) && \
    (HPM_IP_FEATURE_UART_TX_IDLE_DETECT == 1)
  if (uart_is_txline_idle(res_.instance))
  {
    uart_clear_txline_idle_flag(res_.instance);
    uart_disable_irq(res_.instance, uart_intr_tx_line_idle);
    events |= EVENT_TX_IDLE;
  }
#endif
  if (events != 0U)
  {
    Invoke(events, true);
  }
}

void HPMUART::HandleRxDmaEvent(uint32_t status)
{
  Invoke((status & (DMA_CHANNEL_STATUS_ERROR | DMA_CHANNEL_STATUS_ABORT)) != 0U
             ? EVENT_RX_ERROR
             : EVENT_RX,
         true);
}

void HPMUART::HandleTxDmaComplete(ErrorCode result)
{
  Invoke(result == ErrorCode::OK ? EVENT_TX_DONE : EVENT_TX_ERROR, true);
}

ErrorCode HPMUART::ApplyConfig(UART::Configuration config)
{
  if (config.baudrate == 0 || config.data_bits < 5 || config.data_bits > 8)
  {
    return ErrorCode::ARG_ERR;
  }

  if (config.stop_bits != 1 && config.stop_bits != 2)
  {
    return ErrorCode::ARG_ERR;
  }

  word_length_t word_length = word_length_8_bits;
  switch (config.data_bits)
  {
    case 5:
      word_length = word_length_5_bits;
      break;
    case 6:
      word_length = word_length_6_bits;
      break;
    case 7:
      word_length = word_length_7_bits;
      break;
    case 8:
      word_length = word_length_8_bits;
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  parity_setting_t parity = parity_none;
  switch (config.parity)
  {
    case UART::Parity::NO_PARITY:
      parity = parity_none;
      break;
    case UART::Parity::EVEN:
      parity = parity_even;
      break;
    case UART::Parity::ODD:
      parity = parity_odd;
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  dma_disable_channel(res_.dma, res_.rx_dma_channel);
  dma_disable_channel(res_.dma, res_.tx_dma_channel);
  dma_clear_transfer_status(res_.dma, res_.rx_dma_channel);
  dma_clear_transfer_status(res_.dma, res_.tx_dma_channel);

  const uint32_t clock_hz = clock_hz_;
  if (clock_hz == 0)
  {
    return ErrorCode::INIT_ERR;
  }

  uart_config_t uart_config;
  uart_default_config(res_.instance, &uart_config);
  uart_config.src_freq_in_hz = clock_hz;
  uart_config.baudrate = config.baudrate;
  uart_config.word_length = word_length;
  uart_config.num_of_stop_bits = (config.stop_bits == 2) ? stop_bits_2 : stop_bits_1;
  uart_config.parity = parity;
  uart_config.fifo_enable = true;
  uart_config.dma_enable = true;
  uart_config.rx_fifo_level = uart_rx_fifo_trg_not_empty;
  uart_config.tx_fifo_level = uart_tx_fifo_trg_not_full;
#if defined(HPM_IP_FEATURE_UART_RX_EN) && (HPM_IP_FEATURE_UART_RX_EN == 1)
  uart_config.rx_enable = true;
#endif
#if defined(HPM_IP_FEATURE_UART_RX_IDLE_DETECT) && \
    (HPM_IP_FEATURE_UART_RX_IDLE_DETECT == 1)
  uart_config.rxidle_config.detect_enable = true;
  uart_config.rxidle_config.detect_irq_enable = true;
  uart_config.rxidle_config.idle_cond = uart_rxline_idle_cond_state_machine_idle;
  uart_config.rxidle_config.threshold =
      static_cast<uint8_t>(1u + config.data_bits + config.stop_bits +
                           (config.parity == UART::Parity::NO_PARITY ? 0u : 1u));
#endif

#if defined(HPM_IP_FEATURE_UART_TX_IDLE_DETECT) && \
    (HPM_IP_FEATURE_UART_TX_IDLE_DETECT == 1)
  uart_config.txidle_config = {true, false, uart_rxline_idle_cond_state_machine_idle, 1U};
#endif

  if (uart_init(res_.instance, &uart_config) != status_success)
  {
    return ErrorCode::INIT_ERR;
  }

  uart_disable_irq(res_.instance, 0xFFFFFFFFu);

#if defined(HPM_IP_FEATURE_UART_RX_IDLE_DETECT) && \
    (HPM_IP_FEATURE_UART_RX_IDLE_DETECT == 1)
  if (uart_is_rxline_idle(res_.instance))
  {
    uart_clear_rxline_idle_flag(res_.instance);
  }
#endif

#if defined(HPM_IP_FEATURE_UART_DMA_STOP) && (HPM_IP_FEATURE_UART_DMA_STOP == 1)
  uart_rx_enable_dma_auto_stop(res_.instance);
  HPMUARTClearRxDmaAutoStopFlag(res_.instance);
#endif

  SetRxDMA();
  uart_enable_irq(res_.instance, HPM_UART_IRQ_MASK);

  return ErrorCode::OK;
}

uint32_t HPMUART::ResolveClockHz()
{
  uint32_t clock_hz = 0;

#if LIBXR_HPM_UART_HAS_BOARD_HELPER
  if (auto_board_init_)
  {
    init_uart_pins(res_.instance);
    clock_hz = board_init_uart_clock(res_.instance);
  }
#endif

  if (clock_hz == 0 && res_.clock != INVALID_CLOCK)
  {
    clock_add_to_group(res_.clock, 0);
    clock_hz = clock_get_frequency(res_.clock);
  }

  clock_hz_ = clock_hz;
  return clock_hz;
}

void HPMUART::SetRxDMA()
{
  const ErrorCode ans = StartRxDMA();
  ASSERT(ans == ErrorCode::OK);
}

ErrorCode HPMUART::StartRxDMA()
{
  dma_disable_channel(res_.dma, res_.rx_dma_channel);
  dma_clear_transfer_status(res_.dma, res_.rx_dma_channel);
  dmamux_config(res_.dmamux, HPMUARTDmamuxChannel(res_.dma, res_.rx_dma_channel),
                res_.rx_dma_req, true);

  HPMUARTInvalidateDCache(dma_buff_rx_.addr_, dma_buff_rx_.size_);

  dma_handshake_config_t dma_config;
  dma_default_handshake_config(res_.dma, &dma_config);
  dma_config.ch_index = res_.rx_dma_channel;
  dma_config.src = HPMUARTToDmaAddr(&res_.instance->RBR);
  dma_config.dst = HPMUARTToSystemDmaAddr(static_cast<uint8_t*>(dma_buff_rx_.addr_));
  dma_config.size_in_byte = dma_buff_rx_.size_;
  dma_config.data_width = DMA_TRANSFER_WIDTH_BYTE;
  dma_config.src_fixed = true;
  dma_config.dst_fixed = false;
  dma_config.en_infiniteloop = true;
  dma_config.interrupt_mask = DMA_INTERRUPT_MASK_NONE;

  if (dma_setup_handshake(res_.dma, &dma_config, true) != status_success)
  {
    return ErrorCode::INIT_ERR;
  }

#if LIBXR_HPM_UART_HAS_DMA_MGR
  dma_enable_channel_interrupt(res_.dma, res_.rx_dma_channel,
                               DMA_INTERRUPT_MASK_TERMINAL_COUNT |
                                   DMA_INTERRUPT_MASK_HALF_TC | DMA_INTERRUPT_MASK_ERROR |
                                   DMA_INTERRUPT_MASK_ABORT);
#endif

  last_rx_pos_ = 0;
  return ErrorCode::OK;
}

ErrorCode HPMUART::StartTxDMA(size_t length)
{
  if (length == 0 || length > dma_buff_tx_.Size())
  {
    return ErrorCode::ARG_ERR;
  }

  dma_disable_channel(res_.dma, res_.tx_dma_channel);
  dma_clear_transfer_status(res_.dma, res_.tx_dma_channel);
  dmamux_config(res_.dmamux, HPMUARTDmamuxChannel(res_.dma, res_.tx_dma_channel),
                res_.tx_dma_req, true);

  HPMUARTCleanDCache(dma_buff_tx_.ActiveBuffer(), length);

  dma_handshake_config_t dma_config;
  dma_default_handshake_config(res_.dma, &dma_config);
  dma_config.ch_index = res_.tx_dma_channel;
  dma_config.src = HPMUARTToSystemDmaAddr(dma_buff_tx_.ActiveBuffer());
  dma_config.dst = HPMUARTToDmaAddr(&res_.instance->THR);
  dma_config.size_in_byte = length;
  dma_config.data_width = DMA_TRANSFER_WIDTH_BYTE;
  dma_config.src_fixed = false;
  dma_config.dst_fixed = true;
  dma_config.en_infiniteloop = false;
  dma_config.interrupt_mask = DMA_INTERRUPT_MASK_HALF_TC;

  if (dma_setup_handshake(res_.dma, &dma_config, true) != status_success)
  {
    return ErrorCode::FAILED;
  }

#if LIBXR_HPM_UART_HAS_DMA_MGR
  dma_enable_channel_interrupt(res_.dma, res_.tx_dma_channel,
                               DMA_INTERRUPT_MASK_TERMINAL_COUNT |
                                   DMA_INTERRUPT_MASK_ERROR | DMA_INTERRUPT_MASK_ABORT);
#endif

  return ErrorCode::OK;
}

void HPMUART::HandleDmaInterrupt(uint32_t status, uint8_t channel)
{
  if (channel == res_.rx_dma_channel)
  {
    HandleRxDmaEvent(status);
    return;
  }

  if (channel == res_.tx_dma_channel)
  {
    if ((status & (DMA_CHANNEL_STATUS_ERROR | DMA_CHANNEL_STATUS_ABORT)) != 0)
    {
      HandleTxDmaComplete(ErrorCode::FAILED);
    }
    else if ((status & DMA_CHANNEL_STATUS_TC) != 0)
    {
      HandleTxDmaComplete(ErrorCode::OK);
    }
  }
}

void HPMUART::HandleErrorInterrupt()
{
  const uint32_t status = uart_get_status(res_.instance);
  (void)status;

#if defined(HPM_IP_FEATURE_UART_RX_LINE_ERROR_DETECT) && \
    (HPM_IP_FEATURE_UART_RX_LINE_ERROR_DETECT == 1)
  if (uart_rx_is_fifo_error(res_.instance))
  {
    uart_clear_rx_fifo_error_flag(res_.instance);
  }
  if (uart_rx_is_break(res_.instance))
  {
    uart_clear_rx_break_flag(res_.instance);
  }
  if (uart_rx_is_framing_error(res_.instance))
  {
    uart_clear_rx_framing_error_flag(res_.instance);
  }
  if (uart_rx_is_parity_error(res_.instance))
  {
    uart_clear_rx_parity_error_flag(res_.instance);
  }
  if (uart_rx_is_overrun(res_.instance))
  {
    uart_clear_rx_overrun_flag(res_.instance);
  }
#endif

#if defined(HPM_IP_FEATURE_UART_DMA_STOP) && (HPM_IP_FEATURE_UART_DMA_STOP == 1)
  HPMUARTClearRxDmaAutoStopFlag(res_.instance);
#endif
}

void HPMUART::OnInterrupt(uint8_t index)
{
  if (index >= MAX_UART_INSTANCES)
  {
    return;
  }

  auto* uart = instances_[index];
  if (uart == nullptr)
  {
    return;
  }

  uart->HandleUartInterrupt();
}

void HPMUART::OnDmaInterrupt(DMAV2_Type* dma)
{
  for (uint8_t channel = 0; channel < DMA_SOC_CHANNEL_NUM; ++channel)
  {
    auto* rx_uart = rx_dma_map_[channel];
    auto* tx_uart = tx_dma_map_[channel];

    const bool has_rx = (rx_uart != nullptr) && (rx_uart->res_.dma == dma);
    const bool has_tx = (tx_uart != nullptr) && (tx_uart->res_.dma == dma);
    if (!has_rx && !has_tx)
    {
      continue;
    }

    const uint32_t status = dma_check_transfer_status(dma, channel);
    if (status == DMA_CHANNEL_STATUS_ONGOING)
    {
      continue;
    }

    if (has_rx)
    {
      rx_uart->HandleDmaInterrupt(status, channel);
    }

    if (has_tx && tx_uart != rx_uart)
    {
      tx_uart->HandleDmaInterrupt(status, channel);
    }
  }
}

#if LIBXR_HPM_UART_HAS_DMA_MGR
void HPMUART::OnDmaMgrTcCallback(DMA_Type* base, uint32_t channel, void* user_data)
{
  auto* uart = static_cast<HPMUART*>(user_data);
  if (uart == nullptr)
  {
    return;
  }
  uart->HandleDmaInterrupt(DMA_CHANNEL_STATUS_TC, static_cast<uint8_t>(channel));
}

void HPMUART::OnDmaMgrHalfTcCallback(DMA_Type* base, uint32_t channel, void* user_data)
{
  auto* uart = static_cast<HPMUART*>(user_data);
  if (uart == nullptr)
  {
    return;
  }
  uart->HandleDmaInterrupt(DMA_CHANNEL_STATUS_HALF_TC, static_cast<uint8_t>(channel));
}

void HPMUART::OnDmaMgrErrorCallback(DMA_Type* base, uint32_t channel, void* user_data)
{
  auto* uart = static_cast<HPMUART*>(user_data);
  if (uart == nullptr)
  {
    return;
  }
  uart->HandleDmaInterrupt(DMA_CHANNEL_STATUS_ERROR, static_cast<uint8_t>(channel));
}

void HPMUART::OnDmaMgrAbortCallback(DMA_Type* base, uint32_t channel, void* user_data)
{
  auto* uart = static_cast<HPMUART*>(user_data);
  if (uart == nullptr)
  {
    return;
  }
  uart->HandleDmaInterrupt(DMA_CHANNEL_STATUS_ABORT, static_cast<uint8_t>(channel));
}
#endif

#if defined(HPM_UART0) && defined(IRQn_UART0)
SDK_DECLARE_EXT_ISR_M(IRQn_UART0, libxr_hpm_uart0_isr)
void libxr_hpm_uart0_isr(void) { LibXR::HPMUART::OnInterrupt(0); }
#endif

#if defined(HPM_UART1) && defined(IRQn_UART1)
SDK_DECLARE_EXT_ISR_M(IRQn_UART1, libxr_hpm_uart1_isr)
void libxr_hpm_uart1_isr(void) { LibXR::HPMUART::OnInterrupt(1); }
#endif

#if defined(HPM_UART2) && defined(IRQn_UART2)
SDK_DECLARE_EXT_ISR_M(IRQn_UART2, libxr_hpm_uart2_isr)
void libxr_hpm_uart2_isr(void) { LibXR::HPMUART::OnInterrupt(2); }
#endif

#if defined(HPM_UART3) && defined(IRQn_UART3)
SDK_DECLARE_EXT_ISR_M(IRQn_UART3, libxr_hpm_uart3_isr)
void libxr_hpm_uart3_isr(void) { LibXR::HPMUART::OnInterrupt(3); }
#endif

#if defined(HPM_UART4) && defined(IRQn_UART4)
SDK_DECLARE_EXT_ISR_M(IRQn_UART4, libxr_hpm_uart4_isr)
void libxr_hpm_uart4_isr(void) { LibXR::HPMUART::OnInterrupt(4); }
#endif

#if defined(HPM_UART5) && defined(IRQn_UART5)
SDK_DECLARE_EXT_ISR_M(IRQn_UART5, libxr_hpm_uart5_isr)
void libxr_hpm_uart5_isr(void) { LibXR::HPMUART::OnInterrupt(5); }
#endif

#if defined(HPM_UART6) && defined(IRQn_UART6)
SDK_DECLARE_EXT_ISR_M(IRQn_UART6, libxr_hpm_uart6_isr)
void libxr_hpm_uart6_isr(void) { LibXR::HPMUART::OnInterrupt(6); }
#endif

#if defined(HPM_UART7) && defined(IRQn_UART7)
SDK_DECLARE_EXT_ISR_M(IRQn_UART7, libxr_hpm_uart7_isr)
void libxr_hpm_uart7_isr(void) { LibXR::HPMUART::OnInterrupt(7); }
#endif

#if defined(HPM_PUART) && defined(IRQn_PUART)
SDK_DECLARE_EXT_ISR_M(IRQn_PUART, libxr_hpm_puart_isr)
void libxr_hpm_puart_isr(void) { LibXR::HPMUART::OnInterrupt(8); }
#endif

#if defined(IRQn_HDMA) && defined(HPM_HDMA) && !LIBXR_HPM_UART_HAS_DMA_MGR
SDK_DECLARE_EXT_ISR_M(IRQn_HDMA, libxr_hpm_hdma_isr)
void libxr_hpm_hdma_isr(void) { LibXR::HPMUART::OnDmaInterrupt(HPM_HDMA); }
#endif
