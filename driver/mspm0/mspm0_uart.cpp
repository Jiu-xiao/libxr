#include "mspm0_uart.hpp"

#include <atomic>

using namespace LibXR;

MSPM0UART* MSPM0UART::instance_map_[MAX_UART_INSTANCES] = {nullptr};

static constexpr uint32_t MSPM0_UART_RX_ERROR_INTERRUPT_MASK =
    DL_UART_INTERRUPT_OVERRUN_ERROR | DL_UART_INTERRUPT_BREAK_ERROR |
    DL_UART_INTERRUPT_PARITY_ERROR | DL_UART_INTERRUPT_FRAMING_ERROR |
    DL_UART_INTERRUPT_NOISE_ERROR;

static constexpr uint32_t MSPM0_UART_BASE_INTERRUPT_MASK =
    // RX/TX + 地址匹配 + 错误中断；超时中断按需在 Read() 时动态开关 / RX, TX,
    // address-match and error IRQs; timeout IRQ is enabled on demand in Read().
    DL_UART_INTERRUPT_RX | DL_UART_INTERRUPT_TX | DL_UART_INTERRUPT_ADDRESS_MATCH |
    MSPM0_UART_RX_ERROR_INTERRUPT_MASK;

MSPM0UART::MSPM0UART(Resources res, RawData rx_stage_buffer, uint32_t tx_queue_size,
                     uint32_t tx_buffer_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      _read_port(rx_stage_buffer.size_),
      _write_port(tx_queue_size, tx_buffer_size),
      res_(res)
{
  ASSERT(res_.instance != nullptr);
  ASSERT(res_.clock_freq > 0);
  ASSERT(rx_stage_buffer.addr_ != nullptr);
  ASSERT(rx_stage_buffer.size_ > 0);
  ASSERT(tx_queue_size > 0);
  ASSERT(tx_buffer_size > 0);

  _read_port = ReadFun;
  _write_port = WriteFun;

  ASSERT(res_.index < MAX_UART_INSTANCES);
  ASSERT(instance_map_[res_.index] == nullptr);
  instance_map_[res_.index] = this;

  NVIC_ClearPendingIRQ(res_.irqn);
  NVIC_EnableIRQ(res_.irqn);

  const ErrorCode SET_CFG_ANS = SetConfig(config);
  ASSERT(SET_CFG_ANS == ErrorCode::OK);
}

UART::Configuration MSPM0UART::BuildConfigFromSysCfg(UART_Regs* instance,
                                                     uint32_t baudrate)
{
  ASSERT(instance != nullptr);
  ASSERT(baudrate > 0U);

  UART::Configuration config = {
      baudrate, UART::Parity::NO_PARITY, 8U, 1U};

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
      // LibXR UART config only supports none/even/odd parity.
      ASSERT(false);
      config.parity = UART::Parity::NO_PARITY;
      break;
  }

  config.stop_bits =
      (DL_UART_getStopBits(instance) == DL_UART_STOP_BITS_TWO) ? 2U : 1U;
  return config;
}

ErrorCode MSPM0UART::SetConfig(UART::Configuration config)
{
  if (config.baudrate == 0)
  {
    return ErrorCode::ARG_ERR;
  }

  if (config.data_bits < 5 || config.data_bits > 8)
  {
    return ErrorCode::ARG_ERR;
  }

  if (config.stop_bits != 1 && config.stop_bits != 2)
  {
    return ErrorCode::ARG_ERR;
  }

  DL_UART_WORD_LENGTH word_length = DL_UART_WORD_LENGTH_8_BITS;
  switch (config.data_bits)
  {
    case 5:
      word_length = DL_UART_WORD_LENGTH_5_BITS;
      break;
    case 6:
      word_length = DL_UART_WORD_LENGTH_6_BITS;
      break;
    case 7:
      word_length = DL_UART_WORD_LENGTH_7_BITS;
      break;
    case 8:
      word_length = DL_UART_WORD_LENGTH_8_BITS;
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  DL_UART_PARITY parity = DL_UART_PARITY_NONE;
  switch (config.parity)
  {
    case UART::Parity::NO_PARITY:
      parity = DL_UART_PARITY_NONE;
      break;
    case UART::Parity::EVEN:
      parity = DL_UART_PARITY_EVEN;
      break;
    case UART::Parity::ODD:
      parity = DL_UART_PARITY_ODD;
      break;
    default:
      return ErrorCode::ARG_ERR;
  }

  const DL_UART_STOP_BITS STOP_BITS =
      (config.stop_bits == 2) ? DL_UART_STOP_BITS_TWO : DL_UART_STOP_BITS_ONE;

  DL_UART_changeConfig(res_.instance);

  DL_UART_setWordLength(res_.instance, word_length);
  DL_UART_setParityMode(res_.instance, parity);
  DL_UART_setStopBits(res_.instance, STOP_BITS);

  DL_UART_enableFIFOs(res_.instance);
  DL_UART_setTXFIFOThreshold(res_.instance, DL_UART_TX_FIFO_LEVEL_ONE_ENTRY);

  DL_UART_configBaudRate(res_.instance, res_.clock_freq, config.baudrate);

  ApplyRxTimeoutMode();

  DL_UART_clearInterruptStatus(res_.instance, 0xFFFFFFFF);
  DL_UART_enableInterrupt(res_.instance, MSPM0_UART_BASE_INTERRUPT_MASK);
  DL_UART_disableInterrupt(res_.instance,
                           DL_UART_INTERRUPT_TX | GetTimeoutInterruptMask());

  tx_active_valid_ = false;
  tx_active_remaining_ = 0;
  tx_active_total_ = 0;

  DL_UART_enable(res_.instance);

  return ErrorCode::OK;
}

ErrorCode MSPM0UART::WriteFun(WritePort& port)
{
  auto* uart = CONTAINER_OF(&port, MSPM0UART, _write_port);
  if (port.queue_info_->Size() == 0)
  {
    return ErrorCode::OK;
  }

  DL_UART_enableInterrupt(uart->res_.instance, DL_UART_INTERRUPT_TX);
  uart->res_.instance->CPU_INT.ISET = DL_UART_INTERRUPT_TX;
  return ErrorCode::OK;
}

ErrorCode MSPM0UART::ReadFun(ReadPort& port)
{
  auto* uart = CONTAINER_OF(&port, MSPM0UART, _read_port);
  const uint32_t TIMEOUT_MASK = uart->GetTimeoutInterruptMask();
  if (TIMEOUT_MASK != 0U)
  {
    // 仅在有挂起读请求时启用超时中断，避免空闲无效触发 / Enable timeout IRQ
    // only when a read request is pending to avoid idle false triggers.
    if (uart->rx_timeout_mode_ == RxTimeoutMode::LIN_COMPARE)
    {
      // 以本次 Read 请求开始作为超时计时起点 / Start timeout timing from this
      // Read request boundary.
      uart->ResetLinCounter();
    }

    DL_UART_clearInterruptStatus(uart->res_.instance, TIMEOUT_MASK);
    DL_UART_enableInterrupt(uart->res_.instance, TIMEOUT_MASK);
  }

  return ErrorCode::EMPTY;
}

MSPM0UART::RxTimeoutMode MSPM0UART::ResolveRxTimeoutMode() const
{
  // 分发规则 / Dispatch rule:
  // 1) [LIN路径 / LIN path] UART0 且存在 LIN compare 宏配置 -> LIN_COMPARE
  // 2) [LIN路径 / LIN path] 运行时探测到 LIN counter+compare 已启用 -> LIN_COMPARE
  // 3) [BYTE路径 / BYTE path] 其他情况 -> BYTE_INTERRUPT
#if defined(UART_0_INST) && defined(UART_0_COUNTER_COMPARE_VALUE)
  if (res_.instance == UART_0_INST)
  {
    // 本项目 UART0 在 SysConfig 中配置为 LIN 扩展实例 / UART0 is configured as a
    // LIN extend instance by SysConfig in this project.
    // [LIN路径 / LIN path] 固定走 LIN compare / Force LIN compare path.
    return RxTimeoutMode::LIN_COMPARE;
  }
#endif

  if (DL_UART_isLINCounterEnabled(res_.instance) &&
      DL_UART_isLINCounterCompareMatchEnabled(res_.instance))
  {
    // [LIN路径 / LIN path] 非 UART0 按寄存器能力探测：若 LIN counter+compare
    // 已启用则走 LIN /
    // For non-UART0, use LIN path when LIN counter+compare are already enabled.
    return RxTimeoutMode::LIN_COMPARE;
  }

  // [BYTE路径 / BYTE path] 不满足 LIN 条件时回落到按字节中断路径 /
  // Fall back to byte-interrupt path when LIN conditions are not met.
  return RxTimeoutMode::BYTE_INTERRUPT;
}

uint32_t MSPM0UART::GetTimeoutInterruptMask() const
{
  switch (rx_timeout_mode_)
  {
    // [LIN路径 / LIN path] 使用 LINC0 compare match 作为帧间超时事件 /
    // Use LINC0 compare match as frame-gap timeout event.
    case RxTimeoutMode::LIN_COMPARE:
      return DL_UART_INTERRUPT_LINC0_MATCH;
    // [BYTE路径 / BYTE path] 不使用硬件超时中断 /
    // No hardware timeout interrupt in byte-interrupt mode.
    case RxTimeoutMode::BYTE_INTERRUPT:
    default:
      return 0;
  }
}

uint32_t MSPM0UART::GetTimeoutInterruptEnabledMask() const
{
  const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
  if (TIMEOUT_MASK == 0U)
  {
    return 0U;
  }
  return DL_UART_getEnabledInterrupts(res_.instance, TIMEOUT_MASK);
}

uint32_t MSPM0UART::GetTimeoutInterruptMaskedStatus() const
{
  const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
  if (TIMEOUT_MASK == 0U)
  {
    return 0U;
  }
  return DL_UART_getEnabledInterruptStatus(res_.instance, TIMEOUT_MASK);
}

uint32_t MSPM0UART::GetTimeoutInterruptRawStatus() const
{
  const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
  if (TIMEOUT_MASK == 0U)
  {
    return 0U;
  }
  return DL_UART_getRawInterruptStatus(res_.instance, TIMEOUT_MASK);
}

uint32_t MSPM0UART::GetRxInterruptTimeoutValue() const
{
  return DL_UART_getRXInterruptTimeout(res_.instance);
}

uint32_t MSPM0UART::GetRxFifoThresholdValue() const
{
  return static_cast<uint32_t>(DL_UART_getRXFIFOThreshold(res_.instance));
}

void MSPM0UART::ResetLinCounter()
{
  if (rx_timeout_mode_ == RxTimeoutMode::LIN_COMPARE)
  {
    DL_UART_setLINCounterValue(res_.instance, 0);
  }
}

void MSPM0UART::ApplyRxTimeoutMode()
{
  rx_timeout_mode_ = ResolveRxTimeoutMode();

  // 基础 UART 配置对 LIN/BYTE 两条路径一致，先统一配置后再处理模式差异 /
  // Apply shared UART settings first, then patch mode-specific differences.
  DL_UART_setCommunicationMode(res_.instance, DL_UART_MODE_NORMAL);
  DL_UART_setAddressMask(res_.instance, 0U);
  DL_UART_setAddress(res_.instance, 0U);
  DL_UART_setRXFIFOThreshold(res_.instance, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
  DL_UART_setRXInterruptTimeout(res_.instance, 0U);

  switch (rx_timeout_mode_)
  {
    // [LIN路径 / LIN path] 使能 LIN counter/compare，超时事件来自 LINC0_MATCH。
    // [LIN路径 / LIN path] Enable LIN counter/compare; timeout event is LINC0_MATCH.
    case RxTimeoutMode::LIN_COMPARE:
      if (!DL_UART_isLINCounterEnabled(res_.instance))
      {
        DL_UART_enableLINCounter(res_.instance);
      }
      if (!DL_UART_isLINCounterCompareMatchEnabled(res_.instance))
      {
        DL_UART_enableLINCounterCompareMatch(res_.instance);
      }
#if defined(UART_0_COUNTER_COMPARE_VALUE)
      DL_UART_setLINCounterCompareValue(res_.instance, UART_0_COUNTER_COMPARE_VALUE);
#endif
      DL_UART_disableLINCountWhileLow(res_.instance);
      ResetLinCounter();
      break;

    // [BYTE路径 / BYTE path] 仅保留按字节 RX 中断，不依赖超时中断。
    // [BYTE路径 / BYTE path] Keep plain per-byte RX interrupt; no timeout IRQ.
    case RxTimeoutMode::BYTE_INTERRUPT:
    default:
      break;
  }
}

void MSPM0UART::OnInterrupt(uint8_t index)
{
  if (index >= MAX_UART_INSTANCES)
  {
    return;
  }

  auto* uart = instance_map_[index];
  if (uart == nullptr)
  {
    return;
  }

  uart->HandleInterrupt();
}

void MSPM0UART::HandleInterrupt()
{
  const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
  const uint32_t IRQ_MASK = MSPM0_UART_BASE_INTERRUPT_MASK | TIMEOUT_MASK;

  // 处理中断服务过程中产生的新中断，避免长时间占用 IRQ / Handle IRQs raised
  // during ISR service and bound ISR residency time.
  constexpr uint32_t MAX_IRQ_ROUNDS = 32;
  for (uint32_t round = 0; round < MAX_IRQ_ROUNDS; ++round)
  {
    uint32_t pending = DL_UART_getEnabledInterruptStatus(res_.instance, IRQ_MASK);
    if (TIMEOUT_MASK != 0U)
    {
      // LIN compare 在部分路径需读取 RAW 位，避免漏掉超时事件 / For LIN
      // compare, raw status is required on some paths to avoid missing timeout events.
      pending |= DL_UART_getRawInterruptStatus(res_.instance, TIMEOUT_MASK);
    }

    const uint32_t PENDING = pending;
    if (PENDING == 0)
    {
      return;
    }

    constexpr uint32_t RX_PENDING_MASK =
        DL_UART_INTERRUPT_RX | DL_UART_INTERRUPT_ADDRESS_MATCH;
    if ((PENDING & RX_PENDING_MASK) != 0U)
    {
      HandleRxInterrupt(TIMEOUT_MASK);
      if ((PENDING & DL_UART_INTERRUPT_ADDRESS_MATCH) != 0U)
      {
        DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_ADDRESS_MATCH);
      }
    }

    if (TIMEOUT_MASK != 0U)
    {
      HandleRxTimeoutInterrupt(PENDING, TIMEOUT_MASK);
    }

    if ((PENDING & DL_UART_INTERRUPT_OVERRUN_ERROR) != 0)
    {
      HandleErrorInterrupt(DL_UART_IIDX_OVERRUN_ERROR);
    }
    if ((PENDING & DL_UART_INTERRUPT_BREAK_ERROR) != 0)
    {
      HandleErrorInterrupt(DL_UART_IIDX_BREAK_ERROR);
    }
    if ((PENDING & DL_UART_INTERRUPT_PARITY_ERROR) != 0)
    {
      HandleErrorInterrupt(DL_UART_IIDX_PARITY_ERROR);
    }
    if ((PENDING & DL_UART_INTERRUPT_FRAMING_ERROR) != 0)
    {
      HandleErrorInterrupt(DL_UART_IIDX_FRAMING_ERROR);
    }
    if ((PENDING & DL_UART_INTERRUPT_NOISE_ERROR) != 0)
    {
      HandleErrorInterrupt(DL_UART_IIDX_NOISE_ERROR);
    }

    if ((PENDING & DL_UART_INTERRUPT_TX) != 0)
    {
      HandleTxInterrupt(true);
    }
  }

  DL_UART_clearInterruptStatus(res_.instance, IRQ_MASK);
  NVIC_ClearPendingIRQ(res_.irqn);
}

void MSPM0UART::HandleRxInterrupt(uint32_t timeout_mask)
{
  bool pushed = false;
  bool received = false;

  DrainRxFIFO(received, pushed);

  if (received && rx_timeout_mode_ == RxTimeoutMode::LIN_COMPARE)
  {
    // [LIN路径 / LIN path] 连续接收时重置 LIN 计数器，避免帧内误超时 /
    // Reset LIN counter on data reception to avoid in-frame timeout.
    ResetLinCounter();
  }

  if (pushed)
  {
    read_port_->ProcessPendingReads(true);
  }

  if (timeout_mask != 0U &&
      read_port_->busy_.load(std::memory_order_relaxed) != ReadPort::BusyState::PENDING)
  {
    // 无挂起读请求时关闭超时中断，减少无意义 IRQ / Disable timeout IRQ when no
    // pending read remains.
    DL_UART_disableInterrupt(res_.instance, timeout_mask);
    DL_UART_clearInterruptStatus(res_.instance, timeout_mask);
  }

  DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_RX);
}

void MSPM0UART::DrainRxFIFO(bool& received, bool& pushed)
{
  while (!DL_UART_isRXFIFOEmpty(res_.instance))
  {
    const uint8_t RX_BYTE = DL_UART_receiveData(res_.instance);
    received = true;

    if (read_port_->queue_data_->Push(RX_BYTE) == ErrorCode::OK)
    {
      pushed = true;
    }
    else
    {
      rx_drop_count_++;
    }
  }
}

void MSPM0UART::HandleRxTimeoutInterrupt(uint32_t pending, uint32_t timeout_mask)
{
  // [BYTE路径 / BYTE path] timeout_mask=0，直接返回；本函数实际只在 LIN 路径生效。
  // In BYTE path timeout_mask is 0, so this function is effectively LIN-only.
  if ((timeout_mask == 0U) || ((pending & timeout_mask) == 0U))
  {
    return;
  }

  rx_timeout_count_++;
  DL_UART_clearInterruptStatus(res_.instance, pending & timeout_mask);

  // FULL 阈值模式下短帧可能滞留在 HW FIFO，直到超时中断到来 / In
  // FULL-threshold modes, short frames may remain in HW FIFO until timeout IRQ.
  // 先拉取 FIFO，再按超时语义完成挂起读请求 / Drain FIFO first so timeout can
  // complete pending reads with actual data.
  bool pushed = false;
  bool received = false;

  DrainRxFIFO(received, pushed);

  if (pushed)
  {
    read_port_->ProcessPendingReads(true);
  }

  const bool PENDING_READ =
      (read_port_->busy_.load(std::memory_order_relaxed) == ReadPort::BusyState::PENDING);
  if (!PENDING_READ)
  {
    // 超时到来时若无挂起读请求，仅清理硬件状态并退出 / If timeout arrives
    // with no pending read, only clear HW state and exit.
    DL_UART_disableInterrupt(res_.instance, timeout_mask);
    return;
  }

  if (rx_timeout_mode_ == RxTimeoutMode::LIN_COMPARE)
  {
    ResetLinCounter();
  }

  CompletePendingReadOnTimeout(true);

  if (read_port_->busy_.load(std::memory_order_relaxed) != ReadPort::BusyState::PENDING)
  {
    DL_UART_disableInterrupt(res_.instance, timeout_mask);
  }
}

void MSPM0UART::CompletePendingReadOnTimeout(bool in_isr)
{
  if (read_port_->busy_.load(std::memory_order_relaxed) != ReadPort::BusyState::PENDING)
  {
    return;
  }

  const size_t AVAILABLE = read_port_->queue_data_->Size();
  if (AVAILABLE == 0U)
  {
    return;
  }

  const size_t REQUESTED = read_port_->info_.data.size_;
  const size_t POP_SIZE = (AVAILABLE < REQUESTED) ? AVAILABLE : REQUESTED;
  if (POP_SIZE == 0U)
  {
    return;
  }

  const ErrorCode POP_ANS = read_port_->queue_data_->PopBatch(
      reinterpret_cast<uint8_t*>(read_port_->info_.data.addr_), POP_SIZE);
  if (POP_ANS != ErrorCode::OK)
  {
    return;
  }

  const ErrorCode STATUS =
      // 超时允许短包完成：不足请求长度时返回 EMPTY 并带回已收字节数 / Timeout
      // allows short-frame completion: return EMPTY with the received byte count.
      (POP_SIZE == REQUESTED) ? ErrorCode::OK : ErrorCode::EMPTY;
  read_port_->Finish(in_isr, STATUS, read_port_->info_, static_cast<uint32_t>(POP_SIZE));
}

void MSPM0UART::HandleTxInterrupt(bool in_isr)
{
  // 发送状态机：取写请求并尽量填满 TX FIFO，直到该请求完成 / TX state machine:
  // fetch one write request and keep filling TX FIFO until it completes.
  while (true)
  {
    if (!tx_active_valid_)
    {
      if (write_port_->queue_info_->Pop(tx_active_info_) != ErrorCode::OK)
      {
        DisableTxInterrupt();

        if (write_port_->queue_info_->Size() > 0)
        {
          DL_UART_enableInterrupt(res_.instance, DL_UART_INTERRUPT_TX);
          res_.instance->CPU_INT.ISET = DL_UART_INTERRUPT_TX;
        }

        return;
      }

      tx_active_total_ = tx_active_info_.data.size_;
      tx_active_remaining_ = tx_active_total_;
      tx_active_valid_ = true;
    }

    while (tx_active_remaining_ > 0 && !DL_UART_isTXFIFOFull(res_.instance))
    {
      uint8_t tx_byte = 0;
      if (write_port_->queue_data_->Pop(tx_byte) != ErrorCode::OK)
      {
        write_port_->Finish(in_isr, ErrorCode::FAILED, tx_active_info_,
                            tx_active_total_ - tx_active_remaining_);
        tx_active_valid_ = false;
        tx_active_remaining_ = 0;
        tx_active_total_ = 0;
        DisableTxInterrupt();
        return;
      }

      DL_UART_transmitData(res_.instance, tx_byte);
      tx_active_remaining_--;
    }

    if (tx_active_remaining_ > 0)
    {
      return;
    }

    write_port_->Finish(in_isr, ErrorCode::OK, tx_active_info_, tx_active_total_);
    tx_active_valid_ = false;
    tx_active_remaining_ = 0;
    tx_active_total_ = 0;
  }
}

void MSPM0UART::HandleErrorInterrupt(DL_UART_IIDX iidx)
{
  rx_error_count_++;

  uint32_t clear_mask = 0;

  switch (iidx)
  {
    case DL_UART_IIDX_OVERRUN_ERROR:
      clear_mask = DL_UART_INTERRUPT_OVERRUN_ERROR;
      break;

    case DL_UART_IIDX_BREAK_ERROR:
      clear_mask = DL_UART_INTERRUPT_BREAK_ERROR;
      break;

    case DL_UART_IIDX_PARITY_ERROR:
      clear_mask = DL_UART_INTERRUPT_PARITY_ERROR;
      break;

    case DL_UART_IIDX_FRAMING_ERROR:
      clear_mask = DL_UART_INTERRUPT_FRAMING_ERROR;
      break;

    case DL_UART_IIDX_NOISE_ERROR:
      clear_mask = DL_UART_INTERRUPT_NOISE_ERROR;
      break;

    default:
      break;
  }

  if (clear_mask != 0)
  {
    DL_UART_clearInterruptStatus(res_.instance, clear_mask);
  }
}

void MSPM0UART::DisableTxInterrupt()
{
  DL_UART_disableInterrupt(res_.instance, DL_UART_INTERRUPT_TX);
  DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_TX);
  NVIC_ClearPendingIRQ(res_.irqn);
}

#if defined(UART0_BASE)
extern "C" void UART0_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(0);
}
#endif

#if defined(UART1_BASE)
extern "C" void UART1_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(1);
}
#endif

#if defined(UART2_BASE)
extern "C" void UART2_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(2);
}
#endif

#if defined(UART3_BASE)
extern "C" void UART3_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(3);
}
#endif

#if defined(UART4_BASE)
extern "C" void UART4_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(4); }
#endif

#if defined(UART5_BASE)
extern "C" void UART5_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(5); }
#endif

#if defined(UART6_BASE)
extern "C" void UART6_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(6); }
#endif

#if defined(UART7_BASE)
extern "C" void UART7_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(7); }
#endif
