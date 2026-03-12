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
      // LibXR UART config only supports none/even/odd parity.
      ASSERT(false);
      config.parity = UART::Parity::NO_PARITY;
      break;
  }

  config.stop_bits = (DL_UART_getStopBits(instance) == DL_UART_STOP_BITS_TWO) ? 2U : 1U;
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

  CancelByteModeBlockTimeout();
  lin_compare_timeout_latched_.store(false, std::memory_order_release);

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

ErrorCode MSPM0UART::WriteFun(WritePort& port, bool)
{
  auto* uart = CONTAINER_OF(&port, MSPM0UART, _write_port);
  if (port.queue_info_->Size() == 0)
  {
    return ErrorCode::OK;
  }

  DL_UART_enableInterrupt(uart->res_.instance, DL_UART_INTERRUPT_TX);
  uart->res_.instance->CPU_INT.ISET = DL_UART_INTERRUPT_TX;
  return ErrorCode::PENDING;
}

ErrorCode MSPM0UART::ReadFun(ReadPort& port, bool)
{
  auto* uart = CONTAINER_OF(&port, MSPM0UART, _read_port);
  const uint32_t TIMEOUT_MASK = uart->GetTimeoutInterruptMask();
  uart->last_timeout_consumed_size_ = 0U;
  uart->CancelByteModeBlockTimeout();
  if (TIMEOUT_MASK != 0U)
  {
    if ((uart->rx_timeout_mode_ == RxTimeoutMode::LIN_COMPARE) &&
        uart->lin_compare_timeout_latched_.load(std::memory_order_acquire))
    {
      // 上一轮 read-arm 窗口里已经到达 timeout，保留该事件，等 PENDING 建立后立即消费。
      NVIC_ClearPendingIRQ(uart->res_.irqn);
      DL_UART_enableInterrupt(uart->res_.instance, TIMEOUT_MASK);
      uart->res_.instance->CPU_INT.ISET = TIMEOUT_MASK;
      return ErrorCode::PENDING;
    }

    // Re-arm LIN compare from a clean state so stale raw timeout flags cannot leak
    // into the next read request.
    uart->RearmLinCompareTimeout();
    DL_UART_enableInterrupt(uart->res_.instance, TIMEOUT_MASK);
  }
  else if ((uart->rx_timeout_mode_ == RxTimeoutMode::BYTE_INTERRUPT) &&
           (port.info_.op.type == ReadOperation::OperationType::BLOCK) &&
           (port.info_.op.data.sem_info.timeout != UINT32_MAX))
  {
    uart->ArmByteModeBlockTimeout(port.info_.op.data.sem_info.timeout);
  }

  return ErrorCode::PENDING;
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

void MSPM0UART::RearmLinCompareTimeout()
{
  if (rx_timeout_mode_ != RxTimeoutMode::LIN_COMPARE)
  {
    return;
  }

  if (lin_compare_window_ == 0U)
  {
    lin_compare_window_ = GetLinCompareRegisterValue();
  }
  if (lin_compare_window_ == 0U)
  {
    lin_compare_window_ = 1U;
  }

  const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
  lin_compare_timeout_latched_.store(false, std::memory_order_release);
  DL_UART_disableInterrupt(res_.instance, TIMEOUT_MASK);
  DL_UART_clearInterruptStatus(res_.instance, TIMEOUT_MASK);
  NVIC_ClearPendingIRQ(res_.irqn);
  DL_UART_disableLINCounterCompareMatch(res_.instance);
  DL_UART_setLINCounterValue(res_.instance, 0U);
  DL_UART_setLINCounterCompareValue(res_.instance, lin_compare_window_);
  DL_UART_enableLINCounterCompareMatch(res_.instance);
}

uint16_t MSPM0UART::GetLinCompareRegisterValue() const
{
  return DL_UART_getLINFallingEdgeCaptureValue(res_.instance);
}

size_t MSPM0UART::ConsumeTimedOutReadData(bool in_isr, bool copy_to_buffer)
{
  const size_t available = read_port_->queue_data_->Size();
  if (available == 0U)
  {
    last_timeout_consumed_size_ = 0U;
    return 0U;
  }

  const size_t requested = read_port_->info_.data.size_;
  const size_t copy_size = (available < requested) ? available : requested;
  uint8_t* dst = nullptr;
  if (copy_to_buffer)
  {
    dst = reinterpret_cast<uint8_t*>(read_port_->info_.data.addr_);
  }

  const ErrorCode pop_ans = read_port_->queue_data_->PopBatch(dst, copy_size);
  if (pop_ans != ErrorCode::OK)
  {
    last_timeout_consumed_size_ = 0U;
    return 0U;
  }

  last_timeout_consumed_size_ = static_cast<uint32_t>(copy_size);
  read_port_->OnRxDequeue(in_isr);
  return copy_size;
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
      lin_compare_window_ = GetLinCompareRegisterValue();
      if (lin_compare_window_ == 0U)
      {
        lin_compare_window_ = 1U;
        DL_UART_setLINCounterCompareValue(res_.instance, lin_compare_window_);
      }
      DL_UART_disableLINCountWhileLow(res_.instance);
      ResetLinCounter();
      break;

    // [BYTE路径 / BYTE path] 仅保留按字节 RX 中断，不依赖超时中断。
    // [BYTE路径 / BYTE path] Keep plain per-byte RX interrupt; no timeout IRQ.
    case RxTimeoutMode::BYTE_INTERRUPT:
    default:
      lin_compare_window_ = 0U;
      break;
  }
}

void MSPM0UART::EnsureByteModeBlockTimeoutTask()
{
  if (byte_mode_block_timeout_task_ != nullptr)
  {
    return;
  }

  byte_mode_block_timeout_task_ =
      Timer::CreateTask<MSPM0UART*>(OnByteModeBlockTimeout, this, 1U);
  Timer::Add(byte_mode_block_timeout_task_);
}

void MSPM0UART::ArmByteModeBlockTimeout(uint32_t timeout_ms)
{
  if ((rx_timeout_mode_ != RxTimeoutMode::BYTE_INTERRUPT) || (timeout_ms == UINT32_MAX))
  {
    return;
  }

  EnsureByteModeBlockTimeoutTask();

  const uint32_t cycle = NormalizeByteModeBlockTimeout(timeout_ms);
  Timer::Stop(byte_mode_block_timeout_task_);
  byte_mode_block_timeout_task_->data_.count_ = 0U;
  Timer::SetCycle(byte_mode_block_timeout_task_, cycle);
  byte_mode_block_timeout_armed_.store(true, std::memory_order_release);
  Timer::Start(byte_mode_block_timeout_task_);
}

void MSPM0UART::CancelByteModeBlockTimeout()
{
  byte_mode_block_timeout_armed_.store(false, std::memory_order_release);
  if (byte_mode_block_timeout_task_ == nullptr)
  {
    return;
  }

  Timer::Stop(byte_mode_block_timeout_task_);
  byte_mode_block_timeout_task_->data_.count_ = 0U;
}

void MSPM0UART::OnByteModeBlockTimeout(MSPM0UART* uart)
{
  if ((uart == nullptr) ||
      !uart->byte_mode_block_timeout_armed_.exchange(false, std::memory_order_acq_rel))
  {
    return;
  }

  if (uart->byte_mode_block_timeout_task_ != nullptr)
  {
    Timer::Stop(uart->byte_mode_block_timeout_task_);
    uart->byte_mode_block_timeout_task_->data_.count_ = 0U;
  }

  if (uart->rx_timeout_mode_ != RxTimeoutMode::BYTE_INTERRUPT)
  {
    return;
  }

  if (uart->read_port_->busy_.load(std::memory_order_acquire) !=
      ReadPort::BusyState::PENDING)
  {
    return;
  }

  if (uart->read_port_->info_.op.type != ReadOperation::OperationType::BLOCK)
  {
    return;
  }

  uart->read_port_->ProcessPendingReads(false);
  if (uart->read_port_->busy_.load(std::memory_order_acquire) ==
      ReadPort::BusyState::PENDING)
  {
    uart->read_port_->busy_.store(ReadPort::BusyState::IDLE, std::memory_order_release);
  }
}

uint32_t MSPM0UART::NormalizeByteModeBlockTimeout(uint32_t timeout_ms) const
{
  return (timeout_ms == 0U) ? 1U : timeout_ms;
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

  uint32_t pending = DL_UART_getEnabledInterruptStatus(res_.instance, IRQ_MASK);
  if ((TIMEOUT_MASK != 0U) &&
      ((DL_UART_getEnabledInterrupts(res_.instance, TIMEOUT_MASK) & TIMEOUT_MASK) != 0U))
  {
    // LIN compare 在部分路径需读取 RAW 位，避免漏掉超时事件 / For LIN
    // compare, raw status is required on some paths to avoid missing timeout events.
    pending |= DL_UART_getRawInterruptStatus(res_.instance, TIMEOUT_MASK);
  }

  const uint32_t PENDING = pending;
  if (PENDING == 0U)
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
    uint32_t timeout_pending = 0U;
    const uint32_t enabled_timeout =
        DL_UART_getEnabledInterrupts(res_.instance, TIMEOUT_MASK) & TIMEOUT_MASK;
    if (enabled_timeout != 0U)
    {
      timeout_pending = DL_UART_getEnabledInterruptStatus(res_.instance, TIMEOUT_MASK);
      timeout_pending |= DL_UART_getRawInterruptStatus(res_.instance, TIMEOUT_MASK);
    }
    HandleRxTimeoutInterrupt(timeout_pending, TIMEOUT_MASK);
  }

  if ((PENDING & DL_UART_INTERRUPT_OVERRUN_ERROR) != 0U)
  {
    HandleErrorInterrupt(DL_UART_IIDX_OVERRUN_ERROR);
  }
  if ((PENDING & DL_UART_INTERRUPT_BREAK_ERROR) != 0U)
  {
    HandleErrorInterrupt(DL_UART_IIDX_BREAK_ERROR);
  }
  if ((PENDING & DL_UART_INTERRUPT_PARITY_ERROR) != 0U)
  {
    HandleErrorInterrupt(DL_UART_IIDX_PARITY_ERROR);
  }
  if ((PENDING & DL_UART_INTERRUPT_FRAMING_ERROR) != 0U)
  {
    HandleErrorInterrupt(DL_UART_IIDX_FRAMING_ERROR);
  }
  if ((PENDING & DL_UART_INTERRUPT_NOISE_ERROR) != 0U)
  {
    HandleErrorInterrupt(DL_UART_IIDX_NOISE_ERROR);
  }

  if ((PENDING & DL_UART_INTERRUPT_TX) != 0U)
  {
    HandleTxInterrupt(true);
  }
}

void MSPM0UART::HandleRxInterrupt(uint32_t timeout_mask)
{
  bool pushed = false;
  bool received = false;

  DrainRxFIFO(received, pushed);

  if (!received)
  {
    // 对真实突发接收，继续依赖“读空 FIFO”撤销 RX 条件，避免像之前那样清掉同批次后续字节。
    // 但若进入 RX IRQ 后 FIFO 已经为空，而 RX 仍持续报 pending，则说明这是陈旧 RX 状态；
    // 这里定点清一次，避免在 partial/LIN 场景下陷入空转中断。
    DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_RX);
  }

  if (received && rx_timeout_mode_ == RxTimeoutMode::LIN_COMPARE)
  {
    // [LIN路径 / LIN path] 连续接收时重置 LIN 计数器，避免帧内误超时 /
    // Reset LIN counter on data reception to avoid in-frame timeout.
    ResetLinCounter();

    // 若 timeout 与新字节在同一次 IRQ 中相遇，则以“已收到新字节并重启窗口”为准，
    // 避免后续 timeout 路径继续消费旧快照并制造伪 timeout。
    lin_compare_timeout_latched_.store(false, std::memory_order_release);
    if (timeout_mask != 0U)
    {
      DL_UART_clearInterruptStatus(res_.instance, timeout_mask);
    }
  }

  if (pushed)
  {
    read_port_->ProcessPendingReads(true);
  }

  if ((rx_timeout_mode_ == RxTimeoutMode::BYTE_INTERRUPT) &&
      (read_port_->busy_.load(std::memory_order_relaxed) != ReadPort::BusyState::PENDING))
  {
    CancelByteModeBlockTimeout();
  }

  if (timeout_mask != 0U &&
      read_port_->busy_.load(std::memory_order_relaxed) != ReadPort::BusyState::PENDING)
  {
    // 无挂起读请求时关闭超时中断，减少无意义 IRQ / Disable timeout IRQ when no
    // pending read remains.
    lin_compare_timeout_latched_.store(false, std::memory_order_release);
    DL_UART_disableInterrupt(res_.instance, timeout_mask);
    DL_UART_clearInterruptStatus(res_.instance, timeout_mask);
  }
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

  const ReadPort::BusyState busy_before_timeout =
      read_port_->busy_.load(std::memory_order_relaxed);

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

  if (busy_before_timeout != ReadPort::BusyState::PENDING)
  {
    const bool has_buffered_data = pushed || (read_port_->queue_data_->Size() > 0U);
    lin_compare_timeout_latched_.store(!has_buffered_data, std::memory_order_release);
    DL_UART_clearInterruptStatus(res_.instance, pending & timeout_mask);
    // timeout 抢在 ReadPort 发布 PENDING 前到达时，只有在软件 RX 队列里仍无数据时
    // 才锁存该事件并让 CAS 失败重试；若本次 timeout IRQ 已经把字节搬进软件队列，
    // 下一轮读应先消费这些字节，不能再传播陈旧 timeout。
    if (busy_before_timeout == ReadPort::BusyState::IDLE)
    {
      read_port_->busy_.store(ReadPort::BusyState::EVENT, std::memory_order_release);
    }
    DL_UART_disableInterrupt(res_.instance, timeout_mask);
    NVIC_ClearPendingIRQ(res_.irqn);
    return;
  }

  rx_timeout_count_++;
  lin_compare_timeout_latched_.store(false, std::memory_order_release);
  DL_UART_clearInterruptStatus(res_.instance, pending & timeout_mask);

  if (rx_timeout_mode_ == RxTimeoutMode::LIN_COMPARE)
  {
    ResetLinCounter();
  }

  if (read_port_->busy_.load(std::memory_order_relaxed) != ReadPort::BusyState::PENDING)
  {
    DL_UART_disableInterrupt(res_.instance, timeout_mask);
    return;
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

  read_port_->ProcessPendingReads(in_isr);
  if (read_port_->busy_.load(std::memory_order_relaxed) != ReadPort::BusyState::PENDING)
  {
    return;
  }

  if (read_port_->info_.op.type == ReadOperation::OperationType::BLOCK)
  {
    // 对阻塞读，partial-timeout 不能伪装成成功完成；当前 API 没有返回实际长度的
    // 通道，所以这里直接丢弃残帧并释放 busy_，让外层 Wait(timeout) 返回 TIMEOUT。
    UNUSED(ConsumeTimedOutReadData(in_isr, false));
    read_port_->busy_.store(ReadPort::BusyState::IDLE, std::memory_order_release);
    return;
  }

  // 对非阻塞读，不再把 partial 数据偷偷塞进本次 read buffer。
  // 当前通用 ReadPort API 无法携带“ERROR + 实际长度”，因此这里保留软件 RX 队列，
  // 让上层在下一次 Read() 中按常规路径读取这些残帧字节，避免出现“buffer 里有数据，
  // 但 polling/callback 只看见 ERROR”的语义错位。
  last_timeout_consumed_size_ = 0U;
  read_port_->Finish(in_isr, ErrorCode::EMPTY, read_port_->info_);
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
        write_port_->Finish(in_isr, ErrorCode::FAILED, tx_active_info_);
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

    write_port_->Finish(in_isr, ErrorCode::OK, tx_active_info_);
    tx_active_valid_ = false;
    tx_active_remaining_ = 0;
    tx_active_total_ = 0;
  }
}

void MSPM0UART::HandleErrorInterrupt(DL_UART_IIDX iidx)
{
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
