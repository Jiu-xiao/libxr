#include "mspm0_uart.hpp"

#include <atomic>

#include "timebase.hpp"

using namespace LibXR;

std::atomic<MSPM0UART*> MSPM0UART::instance_map_[MAX_UART_INSTANCES] = {};

static constexpr uint32_t MSPM0_UART_RX_ERROR_INTERRUPT_MASK =
    DL_UART_INTERRUPT_OVERRUN_ERROR | DL_UART_INTERRUPT_BREAK_ERROR |
    DL_UART_INTERRUPT_PARITY_ERROR | DL_UART_INTERRUPT_FRAMING_ERROR |
    DL_UART_INTERRUPT_NOISE_ERROR;

static constexpr uint32_t MSPM0_UART_BASE_INTERRUPT_MASK =
    // RX/TX、地址匹配和错误中断的基础掩码；timeout 中断在 Read() 中按需动态开关 /
    // Base mask for RX/TX, address-match, and error IRQs; timeout IRQ is toggled
    // on demand in Read().
    DL_UART_INTERRUPT_RX | DL_UART_INTERRUPT_TX | DL_UART_INTERRUPT_ADDRESS_MATCH |
    MSPM0_UART_RX_ERROR_INTERRUPT_MASK;

namespace
{
class AtomicBoolGuard
{
 public:
  explicit AtomicBoolGuard(std::atomic<bool>& flag) : flag_(flag) {}

  ~AtomicBoolGuard() { flag_.store(false, std::memory_order_release); }

 private:
  std::atomic<bool>& flag_;
};

class AtomicCounterGuard
{
 public:
  explicit AtomicCounterGuard(std::atomic<uint32_t>& counter) : counter_(counter)
  {
    counter_.fetch_add(1U, std::memory_order_acq_rel);
  }

  ~AtomicCounterGuard() { counter_.fetch_sub(1U, std::memory_order_acq_rel); }

 private:
  std::atomic<uint32_t>& counter_;
};

uint32_t NextNonZeroEpoch(std::atomic<uint32_t>& epoch)
{
  uint32_t next_epoch = epoch.fetch_add(1U, std::memory_order_acq_rel) + 1U;
  if (next_epoch == 0U)
  {
    // Keep 0 reserved as the sentinel value of "no epoch".
    next_epoch = 1U;
    epoch.store(next_epoch, std::memory_order_release);
  }
  return next_epoch;
}

bool IsInDetachedDropWindow(uint32_t active_epoch, uint32_t drop_epoch)
{
  if ((active_epoch == 0U) || (drop_epoch == 0U))
  {
    return false;
  }
  // Keep dropping in the timeout epoch and its immediate retry epoch so late
  // completion bytes from the previous request cannot leak into the next one.
  return (active_epoch == drop_epoch) || (active_epoch == (drop_epoch + 1U));
}
}  // namespace

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
  ASSERT(res_.index != INVALID_INSTANCE_INDEX);

  const uint8_t IRQN_INDEX = ResolveIndex(res_.irqn);
  const uint8_t INSTANCE_INDEX = ResolveIndex(res_.instance);
  ASSERT((IRQN_INDEX == res_.index) && (INSTANCE_INDEX == res_.index));

  auto* const REGISTERED = instance_map_[res_.index].load(std::memory_order_acquire);
  ASSERT(REGISTERED == nullptr);

  NVIC_DisableIRQ(res_.irqn);
  NVIC_ClearPendingIRQ(res_.irqn);

  const ErrorCode SET_CFG_ANS = SetConfig(config);
  ASSERT(SET_CFG_ANS == ErrorCode::OK);

  MSPM0UART* expected_null = nullptr;
  ASSERT(instance_map_[res_.index].compare_exchange_strong(
      expected_null, this, std::memory_order_acq_rel, std::memory_order_acquire));
  NVIC_ClearPendingIRQ(res_.irqn);
  NVIC_EnableIRQ(res_.irqn);
}

UART::Configuration MSPM0UART::BuildConfigFromSysCfg(UART_Regs* instance,
                                                     uint32_t baudrate)  // TODO: NOT USED
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
      // LibXR UART 配置当前只支持 none/even/odd parity / LibXR UART config
      // currently supports only none/even/odd parity.
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

  if (res_.use_lin_compare && (res_.lin_compare_value == 0U))
  {
    // SysConfig 指示启用 LIN compare，但 compare 值无效时直接报错。
    return ErrorCode::ARG_ERR;
  }

  bool expected_reconfig = false;
  if (!reconfig_in_progress_.compare_exchange_strong(
          expected_reconfig, true, std::memory_order_acq_rel, std::memory_order_acquire))
  {
    return ErrorCode::BUSY;
  }
  AtomicBoolGuard reconfig_guard(reconfig_in_progress_);

  const bool IRQ_WAS_ENABLED = (NVIC_GetEnableIRQ(res_.irqn) != 0U);
  if (IRQ_WAS_ENABLED)
  {
    NVIC_DisableIRQ(res_.irqn);
  }

  if (io_handler_inflight_.load(std::memory_order_acquire) != 0U)
  {
    if (IRQ_WAS_ENABLED)
    {
      NVIC_ClearPendingIRQ(res_.irqn);
      NVIC_EnableIRQ(res_.irqn);
    }
    return ErrorCode::BUSY;
  }

  if (timeout_cb_inflight_.load(std::memory_order_acquire) != 0U)
  {
    if (IRQ_WAS_ENABLED)
    {
      NVIC_ClearPendingIRQ(res_.irqn);
      NVIC_EnableIRQ(res_.irqn);
    }
    return ErrorCode::BUSY;
  }

  ReadPort::BusyState expected_idle = ReadPort::BusyState::IDLE;
  const bool READ_GATE_ARMED = read_port_->busy_.compare_exchange_strong(
      expected_idle, ReadPort::BusyState::EVENT, std::memory_order_acq_rel,
      std::memory_order_acquire);

  if (IsRxBusy() || IsTxBusy())
  {
    if (READ_GATE_ARMED)
    {
      read_port_->busy_.store(ReadPort::BusyState::IDLE, std::memory_order_release);
    }
    if (IRQ_WAS_ENABLED)
    {
      NVIC_EnableIRQ(res_.irqn);
    }
    return ErrorCode::BUSY;
  }

  // 重配前先关闭并清理 LIN 超时源，避免上一次 ApplyRxTimeoutMode() 遗留的
  // LINC0_MATCH 在 DL_UART_changeConfig 期间产生悬空中断。
  // Tear down the previous LIN timeout source before reconfiguration to
  // prevent a stale LINC0_MATCH from causing a spurious IRQ during
  // DL_UART_changeConfig.
  CancelByteModeBlockTimeout();
  byte_mode_drop_detached_rx_.store(false, std::memory_order_release);
  byte_mode_drop_detached_rx_epoch_.store(0U, std::memory_order_release);
  active_read_request_epoch_.store(0U, std::memory_order_release);
  DL_UART_disableInterrupt(res_.instance, DL_UART_INTERRUPT_LINC0_MATCH);
  DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_LINC0_MATCH);
  DL_UART_disableLINCounterCompareMatch(res_.instance);
  DL_UART_disableLINCounter(res_.instance);
  NVIC_ClearPendingIRQ(res_.irqn);

  DL_UART_changeConfig(res_.instance);

  DL_UART_setWordLength(res_.instance, word_length);
  DL_UART_setParityMode(res_.instance, parity);
  DL_UART_setStopBits(res_.instance, STOP_BITS);

  DL_UART_enableFIFOs(res_.instance);
  DL_UART_setTXFIFOThreshold(res_.instance, DL_UART_TX_FIFO_LEVEL_ONE_ENTRY);

  DL_UART_configBaudRate(res_.instance, res_.clock_freq, config.baudrate);

  lin_compare_window_.store(res_.use_lin_compare ? res_.lin_compare_value : 0U,
                            std::memory_order_release);

  const ErrorCode APPLY_TIMEOUT_ANS = ApplyRxTimeoutMode();
  if (APPLY_TIMEOUT_ANS != ErrorCode::OK)
  {
    if (READ_GATE_ARMED)
    {
      read_port_->busy_.store(ReadPort::BusyState::IDLE, std::memory_order_release);
    }
    if (IRQ_WAS_ENABLED)
    {
      NVIC_ClearPendingIRQ(res_.irqn);
      NVIC_EnableIRQ(res_.irqn);
    }
    return APPLY_TIMEOUT_ANS;
  }

  DL_UART_clearInterruptStatus(res_.instance, 0xFFFFFFFF);
  DL_UART_enableInterrupt(res_.instance, MSPM0_UART_BASE_INTERRUPT_MASK);
  DL_UART_disableInterrupt(res_.instance,
                           DL_UART_INTERRUPT_TX | GetTimeoutInterruptMask());

  tx_active_valid_.store(false, std::memory_order_release);
  tx_active_remaining_ = 0;
  tx_active_total_ = 0;

  DL_UART_enable(res_.instance);
  KickTxIfPending();

  if (READ_GATE_ARMED)
  {
    read_port_->busy_.store(ReadPort::BusyState::IDLE, std::memory_order_release);
  }

  if (IRQ_WAS_ENABLED)
  {
    NVIC_ClearPendingIRQ(res_.irqn);
    NVIC_EnableIRQ(res_.irqn);
  }

  return ErrorCode::OK;
}

ErrorCode MSPM0UART::WriteFun(WritePort& port)
{
  auto* uart = CONTAINER_OF(&port, MSPM0UART, _write_port);
  AtomicCounterGuard io_guard(uart->io_handler_inflight_);

  if (uart->reconfig_in_progress_.load(std::memory_order_acquire))
  {
    return ErrorCode::BUSY;
  }

  if (port.queue_info_->Size() == 0U)
  {
    return uart->IsTxBusy() ? ErrorCode::BUSY : ErrorCode::OK;
  }

  DL_UART_enableInterrupt(uart->res_.instance, DL_UART_INTERRUPT_TX);
  uart->res_.instance->CPU_INT.ISET = DL_UART_INTERRUPT_TX;
  return ErrorCode::PENDING;
}

ErrorCode MSPM0UART::ReadFun(ReadPort& port)
{
  auto* uart = CONTAINER_OF(&port, MSPM0UART, _read_port);
  AtomicCounterGuard io_guard(uart->io_handler_inflight_);

  if (uart->reconfig_in_progress_.load(std::memory_order_acquire))
  {
    // 在重配窗口内阻止 read 进入 pending：把 busy 标记为 EVENT，让
    // ReadPort::operator() 的 IDLE->PENDING CAS 失败并重试 / During
    // reconfiguration, mark busy as EVENT so ReadPort::operator() fails the
    // IDLE->PENDING CAS and retries instead of publishing a stale pending read.
    port.busy_.store(ReadPort::BusyState::EVENT, std::memory_order_release);
    return ErrorCode::BUSY;
  }

  const uint32_t ACTIVE_READ_EPOCH = NextNonZeroEpoch(uart->read_request_epoch_);
  uart->active_read_request_epoch_.store(ACTIVE_READ_EPOCH, std::memory_order_release);

  const uint32_t TIMEOUT_MASK = uart->GetTimeoutInterruptMask();
  const bool IS_BLOCK_INFINITE =
      (port.info_.op.type == ReadOperation::OperationType::BLOCK) &&
      (port.info_.op.data.sem_info.timeout == UINT32_MAX);
  const bool IS_BLOCK_ZERO_TIMEOUT =
      (port.info_.op.type == ReadOperation::OperationType::BLOCK) &&
      (port.info_.op.data.sem_info.timeout == 0U);
  uart->last_timeout_consumed_size_.store(0U, std::memory_order_release);

  if (uart->rx_timeout_mode_.load(std::memory_order_acquire) ==
      RxTimeoutMode::BYTE_INTERRUPT)
  {
    const uint32_t DROP_EPOCH =
        uart->byte_mode_drop_detached_rx_epoch_.load(std::memory_order_acquire);
    if (uart->byte_mode_drop_detached_rx_.load(std::memory_order_acquire) &&
        !IsInDetachedDropWindow(ACTIVE_READ_EPOCH, DROP_EPOCH))
    {
      bool expected_drop = true;
      if (uart->byte_mode_drop_detached_rx_.compare_exchange_strong(
              expected_drop, false, std::memory_order_acq_rel,
              std::memory_order_acquire))
      {
        uart->byte_mode_drop_detached_rx_epoch_.store(0U, std::memory_order_release);
        UNUSED(uart->ConsumeTimedOutReadData(false, false));
      }
    }
  }
  else
  {
    uart->byte_mode_drop_detached_rx_.store(false, std::memory_order_release);
    uart->byte_mode_drop_detached_rx_epoch_.store(0U, std::memory_order_release);
  }

  if (IS_BLOCK_ZERO_TIMEOUT)
  {
    uart->CancelByteModeBlockTimeout();
    if (TIMEOUT_MASK != 0U)
    {
      DL_UART_disableInterrupt(uart->res_.instance, TIMEOUT_MASK);
      DL_UART_clearInterruptStatus(uart->res_.instance, TIMEOUT_MASK);
    }

    // 对 timeout=0 的阻塞读做一次同步 FIFO 拉取，避免 LIN/full-threshold 场景下
    // 已到达但尚未触发 RX IRQ 的短帧继续滞留在 HW FIFO 导致后续读请求直接挂起 / For block
    // read with timeout=0, perform a sync pull of the FIFO to avoid the scenario where a
    // short frame that has arrived but not yet triggered an RX IRQ remains in the HW
    // FIFO, causing subsequent read requests to be directly pending.
    const bool IRQ_WAS_ENABLED = (NVIC_GetEnableIRQ(uart->res_.irqn) != 0U);
    if (IRQ_WAS_ENABLED)
    {
      NVIC_DisableIRQ(uart->res_.irqn);
    }
    bool received = false;
    bool pushed = false;
    uart->DrainRxFIFO(received, pushed);
    if (IRQ_WAS_ENABLED)
    {
      NVIC_ClearPendingIRQ(uart->res_.irqn);
      NVIC_EnableIRQ(uart->res_.irqn);
    }

    if (received && (uart->rx_timeout_mode_.load(std::memory_order_acquire) ==
                     RxTimeoutMode::LIN_COMPARE))
    {
      uart->ResetLinCounter();
    }

    if (port.queue_data_->Size() >= port.info_.data.size_)
    {
      if (port.info_.data.size_ > 0U)
      {
        const ErrorCode POP_ANS = port.queue_data_->PopBatch(
            reinterpret_cast<uint8_t*>(port.info_.data.addr_), port.info_.data.size_);
        UNUSED(POP_ANS);
        ASSERT(POP_ANS == ErrorCode::OK);
      }
      port.read_size_ = port.info_.data.size_;
      return ErrorCode::OK;
    }

    return ErrorCode::BUSY;
  }

  uart->CancelByteModeBlockTimeout();
  if (TIMEOUT_MASK != 0U)
  {
    if (IS_BLOCK_INFINITE)
    {
      DL_UART_disableInterrupt(uart->res_.instance, TIMEOUT_MASK);
      DL_UART_clearInterruptStatus(uart->res_.instance, TIMEOUT_MASK);
      NVIC_ClearPendingIRQ(uart->res_.irqn);
    }
    else
    {
      // [LIN路径 / LIN path] 在发布本次挂起读请求前重新装载 LIN compare 窗口 /
      // Re-arm the LIN compare window before publishing the pending read.
      uart->RearmLinCompareTimeout();
      DL_UART_enableInterrupt(uart->res_.instance, TIMEOUT_MASK);
    }
  }
  else if ((uart->rx_timeout_mode_.load(std::memory_order_acquire) ==
            RxTimeoutMode::BYTE_INTERRUPT) &&
           (port.info_.op.type == ReadOperation::OperationType::BLOCK) &&
           (port.info_.op.data.sem_info.timeout != UINT32_MAX) &&
           (port.info_.op.data.sem_info.timeout != 0U))
  {
    uart->ArmByteModeBlockTimeout(port.info_.op.data.sem_info.timeout);
  }

  return ErrorCode::BUSY;
}

void MSPM0UART::Abort(bool in_isr)
{
  CancelByteModeBlockTimeout();
  byte_mode_drop_detached_rx_.store(false, std::memory_order_release);
  byte_mode_drop_detached_rx_epoch_.store(0U, std::memory_order_release);
  active_read_request_epoch_.store(0U, std::memory_order_release);

  const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
  const uint32_t ABORT_MASK =
      DL_UART_INTERRUPT_TX | TIMEOUT_MASK | MSPM0_UART_RX_ERROR_INTERRUPT_MASK;

  DL_UART_disableInterrupt(res_.instance, ABORT_MASK);
  DL_UART_clearInterruptStatus(res_.instance, ABORT_MASK);

  AbortTx(in_isr);
  AbortRx(in_isr);

  DL_UART_clearInterruptStatus(res_.instance, 0xFFFFFFFFU);
  DL_UART_enableInterrupt(res_.instance, MSPM0_UART_BASE_INTERRUPT_MASK);
  DL_UART_disableInterrupt(res_.instance, DL_UART_INTERRUPT_TX | TIMEOUT_MASK);
  NVIC_ClearPendingIRQ(res_.irqn);
}

void MSPM0UART::OnInterrupt(uint8_t index)
{
  if (index >= MAX_UART_INSTANCES)
  {
    return;
  }

  auto* uart = instance_map_[index].load(std::memory_order_acquire);
  if (uart == nullptr)
  {
    return;
  }

  uart->HandleInterrupt();
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

MSPM0UART::RxTimeoutMode MSPM0UART::ResolveRxTimeoutMode() const
{
  // 超时模式由 SysConfig 决定 / Timeout mode is driven by SysConfig.
  return res_.use_lin_compare ? RxTimeoutMode::LIN_COMPARE
                              : RxTimeoutMode::BYTE_INTERRUPT;
}

bool MSPM0UART::IsTxBusy() const
{
  return tx_active_valid_.load(std::memory_order_acquire) ||
         (write_port_->queue_info_->Size() > 0U) ||
         (write_port_->queue_data_->Size() > 0U) || DL_UART_isBusy(res_.instance);
}

bool MSPM0UART::IsRxBusy() const
{
  return read_port_->busy_.load(std::memory_order_acquire) ==
         ReadPort::BusyState::PENDING;
}

bool MSPM0UART::IsZeroTimeoutPendingBlockRead() const
{
  if (read_port_->busy_.load(std::memory_order_acquire) != ReadPort::BusyState::PENDING)
  {
    return false;
  }

  if (read_port_->info_.op.type != ReadOperation::OperationType::BLOCK)
  {
    return false;
  }

  return read_port_->info_.op.data.sem_info.timeout == 0U;
}

  void MSPM0UART::KickTxIfPending()
  {
    if ((write_port_->queue_info_->Size() == 0U) &&
        !tx_active_valid_.load(std::memory_order_acquire))
    {
      return;
    }

    DL_UART_enableInterrupt(res_.instance, DL_UART_INTERRUPT_TX);
    res_.instance->CPU_INT.ISET = DL_UART_INTERRUPT_TX;
  }

  uint16_t MSPM0UART::ResolveLinCompareWindow() const
  {
    if (!res_.use_lin_compare)
    {
      return 0U;
    }
    return res_.lin_compare_value;
  }

  uint32_t MSPM0UART::GetTimeoutInterruptMask() const
  {
    switch (rx_timeout_mode_.load(std::memory_order_acquire))
    {
      // [LIN路径 / LIN path] 使用 LINC0 compare match 作为帧间超时事件 / Use
      // LINC0 compare match as the frame-gap timeout event.
      case RxTimeoutMode::LIN_COMPARE:
        return DL_UART_INTERRUPT_LINC0_MATCH;
      // [BYTE路径 / BYTE path] 不使用硬件超时中断 / No hardware timeout IRQ is
      // used in byte-interrupt mode.
      case RxTimeoutMode::BYTE_INTERRUPT:
      default:
        return 0;
    }
  }

  void MSPM0UART::RearmLinCompareTimeout()
  {
    if (rx_timeout_mode_.load(std::memory_order_acquire) != RxTimeoutMode::LIN_COMPARE)
    {
      return;
    }

    uint16_t lin_compare_window = lin_compare_window_.load(std::memory_order_acquire);
    if (lin_compare_window == 0U)
    {
      lin_compare_window = ResolveLinCompareWindow();
      lin_compare_window_.store(lin_compare_window, std::memory_order_release);
    }
    if (lin_compare_window == 0U)
    {
      return;
    }

    const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
    DL_UART_disableInterrupt(res_.instance, TIMEOUT_MASK);
    DL_UART_clearInterruptStatus(res_.instance, TIMEOUT_MASK);
    NVIC_ClearPendingIRQ(res_.irqn);
    DL_UART_disableLINCounterCompareMatch(res_.instance);
    DL_UART_setLINCounterValue(res_.instance, 0U);
    DL_UART_setLINCounterCompareValue(res_.instance, lin_compare_window);
    DL_UART_enableLINCounterCompareMatch(res_.instance);
  }

  size_t MSPM0UART::ConsumeTimedOutReadData(bool in_isr, bool copy_to_buffer,
                                            size_t size_limit)
  {
    const size_t AVAILABLE = read_port_->queue_data_->Size();
    if (AVAILABLE == 0U)
    {
      return 0U;
    }

    size_t copy_size = AVAILABLE;
    if (copy_to_buffer)
    {
      const size_t REQUESTED = read_port_->info_.data.size_;
      copy_size = (AVAILABLE < REQUESTED) ? AVAILABLE : REQUESTED;
    }
    if ((size_limit != static_cast<size_t>(-1)) && (copy_size > size_limit))
    {
      copy_size = size_limit;
    }
    if (copy_size == 0U)
    {
      return 0U;
    }
    uint8_t* dst = nullptr;
    if (copy_to_buffer)
    {
      dst = reinterpret_cast<uint8_t*>(read_port_->info_.data.addr_);
    }

    const ErrorCode POP_ANS = read_port_->queue_data_->PopBatch(dst, copy_size);
    if (POP_ANS != ErrorCode::OK)
    {
      return 0U;
    }

    last_timeout_consumed_size_.fetch_add(static_cast<uint32_t>(copy_size),
                                          std::memory_order_acq_rel);
    UNUSED(in_isr);
    return copy_size;
  }

  void MSPM0UART::ResetLinCounter()
  {
    if (rx_timeout_mode_.load(std::memory_order_acquire) == RxTimeoutMode::LIN_COMPARE)
    {
      DL_UART_setLINCounterValue(res_.instance, 0);
    }
  }

  ErrorCode MSPM0UART::ApplyRxTimeoutMode()
  {
    rx_timeout_mode_.store(ResolveRxTimeoutMode(), std::memory_order_release);

    // timeout 配置只修改 timeout 相关寄存器，不覆盖通信模式/地址类配置 /
    // Touch timeout-related registers only; do not override communication mode
    // or address settings configured elsewhere.

    if (rx_timeout_mode_.load(std::memory_order_acquire) == RxTimeoutMode::LIN_COMPARE)
    {
      const uint16_t CURRENT_WINDOW = lin_compare_window_.load(std::memory_order_acquire);
      const uint16_t RESOLVED_WINDOW =
          (CURRENT_WINDOW > 0U) ? CURRENT_WINDOW : ResolveLinCompareWindow();
      if (RESOLVED_WINDOW != 0U)
      {
        // [LIN路径 / LIN path] 仅在有明确 compare 窗口时启用 LIN timeout，避免
        // compare=0 被强制收敛成 1 / Enable LIN timeout only with a valid compare
        // window to avoid forcing compare=0 into window=1.
        if (!DL_UART_isLINCounterEnabled(res_.instance))
        {
          DL_UART_enableLINCounter(res_.instance);
        }
        if (!DL_UART_isLINCounterCompareMatchEnabled(res_.instance))
        {
          DL_UART_enableLINCounterCompareMatch(res_.instance);
        }
        lin_compare_window_.store(RESOLVED_WINDOW, std::memory_order_release);
        DL_UART_setLINCounterCompareValue(res_.instance, RESOLVED_WINDOW);
        DL_UART_disableLINCountWhileLow(res_.instance);
        ResetLinCounter();
        return ErrorCode::OK;
      }
      return ErrorCode::ARG_ERR;
    }

    // [BYTE路径 / BYTE path] 仅保留按字节 RX 中断，不依赖超时中断 / Keep plain
    // per-byte RX interrupt; no timeout IRQ is used.
    lin_compare_window_.store(0U, std::memory_order_release);
    DL_UART_disableLINCounterCompareMatch(res_.instance);
    DL_UART_disableLINCounter(res_.instance);
    DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_LINC0_MATCH);
    DL_UART_setRXFIFOThreshold(res_.instance, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    DL_UART_setRXInterruptTimeout(res_.instance, 0U);
    return ErrorCode::OK;
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
    Timer::Start(byte_mode_block_timeout_task_);
  }

  void MSPM0UART::ArmByteModeBlockTimeout(uint32_t timeout_ms)
  {
    if ((rx_timeout_mode_.load(std::memory_order_acquire) !=
         RxTimeoutMode::BYTE_INTERRUPT) ||
        (timeout_ms == UINT32_MAX))
    {
      return;
    }

    EnsureByteModeBlockTimeoutTask();

    const uint32_t CYCLE = NormalizeByteModeBlockTimeout(timeout_ms);
    const uint32_t NOW = static_cast<uint32_t>(Timebase::GetMilliseconds());
    byte_mode_block_timeout_start_ms_.store(NOW, std::memory_order_release);
    byte_mode_block_timeout_cycle_ms_.store(CYCLE, std::memory_order_release);
    // 使用 epoch 识别 timeout 回调代际，避免旧回调误处理新一轮 Arm。/ Use an epoch
    // token to distinguish timeout generations and prevent stale callbacks from
    // touching a newly armed read.
    uint32_t epoch =
        byte_mode_block_timeout_epoch_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    if (epoch == 0U)
    {
      // 0 作为“未装载”哨兵，发生回绕时重新对齐到 1。/ Keep 0 reserved as the
      // disarmed sentinel and realign on wrap-around.
      epoch = 1U;
      byte_mode_block_timeout_epoch_.store(epoch, std::memory_order_release);
    }
  }

  void MSPM0UART::CancelByteModeBlockTimeout()
  {
    byte_mode_block_timeout_epoch_.store(0U, std::memory_order_release);
    byte_mode_block_timeout_cycle_ms_.store(0U, std::memory_order_release);
  }

  void MSPM0UART::OnByteModeBlockTimeout(MSPM0UART * uart)
  {
    if (uart == nullptr)
    {
      return;
    }

    AtomicCounterGuard timeout_guard(uart->timeout_cb_inflight_);
    if (uart->reconfig_in_progress_.load(std::memory_order_acquire))
    {
      return;
    }

    const uint32_t ARMED_EPOCH =
        uart->byte_mode_block_timeout_epoch_.load(std::memory_order_acquire);
    if (ARMED_EPOCH == 0U)
    {
      return;
    }

    const uint32_t NOW = static_cast<uint32_t>(Timebase::GetMilliseconds());
    const uint32_t CYCLE =
        uart->byte_mode_block_timeout_cycle_ms_.load(std::memory_order_acquire);
    if (CYCLE == 0U)
    {
      return;
    }

    const uint32_t START =
        uart->byte_mode_block_timeout_start_ms_.load(std::memory_order_acquire);
    const uint32_t ELAPSED = static_cast<uint32_t>(NOW - START);
    if (ELAPSED < CYCLE)
    {
      return;
    }

    uint32_t expected_epoch = ARMED_EPOCH;
    if (!uart->byte_mode_block_timeout_epoch_.compare_exchange_strong(
            expected_epoch, 0U, std::memory_order_acq_rel, std::memory_order_acquire))
    {
      return;
    }

    if (uart->rx_timeout_mode_.load(std::memory_order_acquire) !=
        RxTimeoutMode::BYTE_INTERRUPT)
    {
      return;
    }

    if (uart->read_port_->busy_.load(std::memory_order_acquire) !=
        ReadPort::BusyState::PENDING)
    {
      return;
    }

    bool expected_completion = false;
    if (!uart->read_completion_inflight_.compare_exchange_strong(
            expected_completion, true, std::memory_order_acq_rel,
            std::memory_order_acquire))
    {
      return;
    }
    AtomicBoolGuard completion_guard(uart->read_completion_inflight_);

    uart->CompletePendingReadOnTimeout(false);
  }

  uint32_t MSPM0UART::NormalizeByteModeBlockTimeout(uint32_t timeout_ms) const
  {
    return (timeout_ms == 0U) ? 1U : timeout_ms;
  }

  void MSPM0UART::HandleInterrupt()
  {
    AtomicCounterGuard io_guard(io_handler_inflight_);

    if (reconfig_in_progress_.load(std::memory_order_acquire))
    {
      const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
      const uint32_t IRQ_MASK = MSPM0_UART_BASE_INTERRUPT_MASK | TIMEOUT_MASK;
      uint32_t pending_during_reconfig =
          DL_UART_getEnabledInterruptStatus(res_.instance, IRQ_MASK);
      if (TIMEOUT_MASK != 0U)
      {
        pending_during_reconfig |=
            DL_UART_getRawInterruptStatus(res_.instance, TIMEOUT_MASK);
      }
      if (pending_during_reconfig != 0U)
      {
        DL_UART_clearInterruptStatus(res_.instance, pending_during_reconfig);
      }
      NVIC_ClearPendingIRQ(res_.irqn);
      return;
    }

    const uint32_t TIMEOUT_MASK = GetTimeoutInterruptMask();
    const uint32_t IRQ_MASK = MSPM0_UART_BASE_INTERRUPT_MASK | TIMEOUT_MASK;

    uint32_t pending = DL_UART_getEnabledInterruptStatus(res_.instance, IRQ_MASK);
    if ((TIMEOUT_MASK != 0U) &&
        ((DL_UART_getEnabledInterrupts(res_.instance, TIMEOUT_MASK) & TIMEOUT_MASK) !=
         0U))
    {
      // LIN compare 在部分路径需要读取 RAW 位，避免漏掉超时事件 / LIN compare
      // needs raw status on some paths to avoid missing timeout events.
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
      const uint32_t ENABLED_TIMEOUT =
          DL_UART_getEnabledInterrupts(res_.instance, TIMEOUT_MASK) & TIMEOUT_MASK;
      if (ENABLED_TIMEOUT != 0U)
      {
        timeout_pending = DL_UART_getEnabledInterruptStatus(res_.instance, TIMEOUT_MASK);
        timeout_pending |= DL_UART_getRawInterruptStatus(res_.instance, TIMEOUT_MASK);
      }
      HandleRxTimeoutInterrupt(timeout_pending, TIMEOUT_MASK);
    }

    const uint32_t ERROR_PENDING = PENDING & MSPM0_UART_RX_ERROR_INTERRUPT_MASK;
    if (ERROR_PENDING != 0U)
    {
      HandleErrorInterrupt(ERROR_PENDING);
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
      // [RX路径 / RX path] FIFO 已空但 RX 仍报 pending 时，清一次 RX 状态位以终止
      // 空转 IRQ / If FIFO is already empty while RX still reports pending,
      // clear RX once to stop a spin IRQ.
      DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_RX);
    }

    if (received &&
        (rx_timeout_mode_.load(std::memory_order_acquire) == RxTimeoutMode::LIN_COMPARE))
    {
      // [LIN路径 / LIN path] 连续接收时重置 LIN 计数器，避免帧内误超时 / Reset the
      // LIN counter on data reception to avoid in-frame timeout.
      ResetLinCounter();

      // [LIN路径 / LIN path] 同一 IRQ 内若已收到新字节，则清掉本次 timeout 状态并
      // 继续按新窗口计时 / If new data arrives in the same IRQ, clear this
      // timeout state and continue with the refreshed window.
      if (timeout_mask != 0U)
      {
        DL_UART_clearInterruptStatus(res_.instance, timeout_mask);
      }
    }

    if (pushed)
    {
      bool expected_completion = false;
      if (read_completion_inflight_.compare_exchange_strong(expected_completion, true,
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire))
      {
        AtomicBoolGuard completion_guard(read_completion_inflight_);
        if (IsZeroTimeoutPendingBlockRead())
        {
          ReadPort::BusyState expected = ReadPort::BusyState::PENDING;
          UNUSED(read_port_->busy_.compare_exchange_strong(
              expected, ReadPort::BusyState::IDLE, std::memory_order_acq_rel,
              std::memory_order_acquire));
        }
        else
        {
          read_port_->ProcessPendingReads(true);
        }
      }
    }

    if ((rx_timeout_mode_.load(std::memory_order_acquire) ==
         RxTimeoutMode::BYTE_INTERRUPT) &&
        (read_port_->busy_.load(std::memory_order_acquire) !=
         ReadPort::BusyState::PENDING))
    {
      CancelByteModeBlockTimeout();
    }

    if (timeout_mask != 0U &&
        read_port_->busy_.load(std::memory_order_acquire) != ReadPort::BusyState::PENDING)
    {
      // 无挂起读请求时关闭 timeout 中断，减少无意义 IRQ / Disable timeout IRQ when
      // no pending read remains.
      DL_UART_disableInterrupt(res_.instance, timeout_mask);
      DL_UART_clearInterruptStatus(res_.instance, timeout_mask);
    }
  }

void MSPM0UART::DrainRxFIFO(bool& received, bool& pushed)
{
  const bool DROP_DETACHED_BYTE_MODE_RX =
      (rx_timeout_mode_.load(std::memory_order_acquire) ==
       RxTimeoutMode::BYTE_INTERRUPT) &&
      IsInDetachedDropWindow(
          active_read_request_epoch_.load(std::memory_order_acquire),
          byte_mode_drop_detached_rx_epoch_.load(std::memory_order_acquire)) &&
      byte_mode_drop_detached_rx_.load(std::memory_order_acquire);

  while (!DL_UART_isRXFIFOEmpty(res_.instance))
  {
    const uint8_t RX_BYTE = DL_UART_receiveData(res_.instance);
    received = true;

    if (DROP_DETACHED_BYTE_MODE_RX)
    {
      UNUSED(RX_BYTE);
      last_timeout_consumed_size_.fetch_add(1U, std::memory_order_acq_rel);
      continue;
    }

    if (read_port_->queue_data_->Push(RX_BYTE) == ErrorCode::OK)
    {
      pushed = true;
    }
    else
    {
      rx_drop_count_.fetch_add(1U, std::memory_order_acq_rel);
    }
  }
}

  void MSPM0UART::HandleRxTimeoutInterrupt(uint32_t pending, uint32_t timeout_mask)
  {
    // [BYTE路径 / BYTE path] timeout_mask=0 时直接返回，因此本函数实际只在 LIN
    // 路径生效 / In BYTE path, timeout_mask is 0, so this function is
    // effectively LIN-only.
    if ((timeout_mask == 0U) || ((pending & timeout_mask) == 0U))
    {
      return;
    }

    const ReadPort::BusyState BUSY_BEFORE_TIMEOUT =
        read_port_->busy_.load(std::memory_order_acquire);

    // FULL 阈值模式下短帧可能滞留在 HW FIFO，直到超时中断到来 / In FULL-threshold
    // modes, short frames may remain in HW FIFO until the timeout IRQ arrives.
    // 这里先拉取 FIFO，再按超时语义完成挂起读请求 / Drain FIFO first so timeout
    // can complete pending reads with actual data.
    bool pushed = false;
    bool received = false;

    DrainRxFIFO(received, pushed);

    if (BUSY_BEFORE_TIMEOUT != ReadPort::BusyState::PENDING)
    {
      DL_UART_clearInterruptStatus(res_.instance, pending & timeout_mask);
      if (BUSY_BEFORE_TIMEOUT == ReadPort::BusyState::IDLE)
      {
        read_port_->busy_.store(ReadPort::BusyState::EVENT, std::memory_order_release);
      }
      DL_UART_disableInterrupt(res_.instance, timeout_mask);
      NVIC_ClearPendingIRQ(res_.irqn);
      return;
    }

    bool expected_completion = false;
    if (!read_completion_inflight_.compare_exchange_strong(expected_completion, true,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_acquire))
    {
      DL_UART_clearInterruptStatus(res_.instance, pending & timeout_mask);
      return;
    }
    AtomicBoolGuard completion_guard(read_completion_inflight_);

    if (IsZeroTimeoutPendingBlockRead())
    {
      ReadPort::BusyState expected = ReadPort::BusyState::PENDING;
      UNUSED(read_port_->busy_.compare_exchange_strong(
          expected, ReadPort::BusyState::IDLE, std::memory_order_acq_rel,
          std::memory_order_acquire));
      DL_UART_clearInterruptStatus(res_.instance, pending & timeout_mask);
      DL_UART_disableInterrupt(res_.instance, timeout_mask);
      NVIC_ClearPendingIRQ(res_.irqn);
      return;
    }

    if (pushed)
    {
      read_port_->ProcessPendingReads(true);
    }

    rx_timeout_count_.fetch_add(1U, std::memory_order_acq_rel);
    DL_UART_clearInterruptStatus(res_.instance, pending & timeout_mask);

    if (rx_timeout_mode_.load(std::memory_order_acquire) == RxTimeoutMode::LIN_COMPARE)
    {
      ResetLinCounter();
    }

    if (read_port_->busy_.load(std::memory_order_acquire) != ReadPort::BusyState::PENDING)
    {
      DL_UART_disableInterrupt(res_.instance, timeout_mask);
      return;
    }

    CompletePendingReadOnTimeout(true);

    if (read_port_->busy_.load(std::memory_order_acquire) != ReadPort::BusyState::PENDING)
    {
      DL_UART_disableInterrupt(res_.instance, timeout_mask);
    }
  }

  void MSPM0UART::CompletePendingReadOnTimeout(bool in_isr)
  {
    if (read_port_->busy_.load(std::memory_order_acquire) != ReadPort::BusyState::PENDING)
    {
      return;
    }

    if (IsZeroTimeoutPendingBlockRead())
    {
      ReadPort::BusyState expected = ReadPort::BusyState::PENDING;
      UNUSED(read_port_->busy_.compare_exchange_strong(
          expected, ReadPort::BusyState::IDLE, std::memory_order_acq_rel,
          std::memory_order_acquire));
      read_port_->read_size_ = 0U;
      return;
    }

    read_port_->ProcessPendingReads(in_isr);
    if (read_port_->busy_.load(std::memory_order_acquire) != ReadPort::BusyState::PENDING)
    {
      return;
    }

    if (read_port_->info_.op.type == ReadOperation::OperationType::BLOCK)
    {
      if (read_port_->info_.op.data.sem_info.timeout == UINT32_MAX)
      {
        // [无限等待阻塞读 / infinite-wait blocking read] 对 timeout 事件不做完成，
        // 统一 BYTE/LIN 语义：仅在读满目标长度时由正常路径完成 / Do not complete on
        // timeout for infinite-wait block reads. Keep BYTE/LIN semantics aligned:
        // completion happens only when the requested length is satisfied.
        last_timeout_consumed_size_.store(0U, std::memory_order_release);
        return;
      }

      // [BLOCK路径 / BLOCK path] 有限超时阻塞读不再主动 Finish/Post，避免在
      // Wait(timeout) 已返回后产生陈旧信号量令牌；这里只做状态回收和残留字节处理 /
      // For finite-timeout blocking reads, do not Finish/Post from timeout path to
      // avoid stale semaphore tokens after Wait(timeout) returns. Only reclaim
      // state and handle residual bytes here.
      ReadPort::BusyState expected = ReadPort::BusyState::PENDING;
      if (read_port_->busy_.compare_exchange_strong(expected, ReadPort::BusyState::IDLE,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
      {
        if (rx_timeout_mode_.load(std::memory_order_acquire) ==
            RxTimeoutMode::BYTE_INTERRUPT)
        {
          byte_mode_drop_detached_rx_epoch_.store(
              active_read_request_epoch_.load(std::memory_order_acquire),
              std::memory_order_release);
          byte_mode_drop_detached_rx_.store(true, std::memory_order_release);
        }
        UNUSED(ConsumeTimedOutReadData(in_isr, false));
        read_port_->read_size_ = 0U;
      }
      return;
    }

    // [非阻塞路径 / non-blocking path] 非阻塞读超时时，保留软件 RX 队列中的字节，
    // 留给下一次 Read() 消费 / On non-blocking timeout, leave buffered bytes in
    // the software RX queue for the next Read().
    ReadPort::BusyState expected = ReadPort::BusyState::PENDING;
    if (read_port_->busy_.compare_exchange_strong(expected, ReadPort::BusyState::EVENT,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
    {
      last_timeout_consumed_size_.store(0U, std::memory_order_release);
      read_port_->read_size_ = 0U;
      read_port_->busy_.store(ReadPort::BusyState::IDLE, std::memory_order_release);
      read_port_->info_.op.UpdateStatus(in_isr, ErrorCode::TIMEOUT);
    }
  }

  void MSPM0UART::HandleTxInterrupt(bool in_isr)
  {
    // 发送状态机：取一个写请求并尽量填满 TX FIFO，直到该请求完成 / TX state
    // machine: fetch one write request and keep filling TX FIFO until it
    // completes.
    while (true)
    {
      if (!tx_active_valid_.load(std::memory_order_acquire))
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
        tx_active_valid_.store(true, std::memory_order_release);
      }

      while (tx_active_remaining_ > 0 && !DL_UART_isTXFIFOFull(res_.instance))
      {
        uint8_t tx_byte = 0;
        if (write_port_->queue_data_->Pop(tx_byte) != ErrorCode::OK)
        {
          const uint32_t SENT =
              static_cast<uint32_t>(tx_active_total_ - tx_active_remaining_);
          write_port_->Finish(in_isr, ErrorCode::FAILED, tx_active_info_, SENT);
          tx_active_valid_.store(false, std::memory_order_release);
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

      write_port_->Finish(in_isr, ErrorCode::OK, tx_active_info_,
                          static_cast<uint32_t>(tx_active_total_));
      tx_active_valid_.store(false, std::memory_order_release);
      tx_active_remaining_ = 0;
      tx_active_total_ = 0;
    }
  }

  void MSPM0UART::AbortTx(bool in_isr)
  {
    DisableTxInterrupt();

    if (tx_active_valid_.exchange(false, std::memory_order_acq_rel))
    {
      write_port_->Finish(in_isr, ErrorCode::FAILED, tx_active_info_, 0U);
    }
    tx_active_remaining_ = 0U;
    tx_active_total_ = 0U;

    WriteInfoBlock info;
    while (write_port_->queue_info_->Pop(info) == ErrorCode::OK)
    {
      write_port_->Finish(in_isr, ErrorCode::FAILED, info, 0U);
    }

    if (write_port_->queue_data_ != nullptr)
    {
      write_port_->queue_data_->Reset();
    }
    write_port_->lock_.store(WritePort::LockState::UNLOCKED, std::memory_order_release);
  }

  void MSPM0UART::AbortRx(bool in_isr)
  {
    bool received = false;
    bool pushed = false;
    DrainRxFIFO(received, pushed);
    UNUSED(received);
    UNUSED(pushed);

    if (read_port_->queue_data_ != nullptr)
    {
      read_port_->queue_data_->Reset();
    }

    ReadPort::BusyState expected = ReadPort::BusyState::PENDING;
    if (read_port_->busy_.compare_exchange_strong(expected, ReadPort::BusyState::IDLE,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
    {
      read_port_->read_size_ = 0U;
      read_port_->info_.op.UpdateStatus(in_isr, ErrorCode::FAILED);
    }
    else
    {
      read_port_->read_size_ = 0U;
      read_port_->busy_.store(ReadPort::BusyState::IDLE, std::memory_order_release);
    }

    last_timeout_consumed_size_.store(0U, std::memory_order_release);
  }

  void MSPM0UART::HandleErrorInterrupt(uint32_t pending_error_mask)
  {
    const uint32_t CLEAR_MASK = pending_error_mask & MSPM0_UART_RX_ERROR_INTERRUPT_MASK;
    if (CLEAR_MASK == 0U)
    {
      return;
    }

    DL_UART_clearInterruptStatus(res_.instance, CLEAR_MASK);
    Abort(true);
  }

  void MSPM0UART::DisableTxInterrupt()
  {
    DL_UART_disableInterrupt(res_.instance, DL_UART_INTERRUPT_TX);
    DL_UART_clearInterruptStatus(res_.instance, DL_UART_INTERRUPT_TX);
    NVIC_ClearPendingIRQ(res_.irqn);
  }

#ifdef UART0_BASE
extern "C" void UART0_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(0);
}
#endif

#ifdef UART1_BASE
extern "C" void UART1_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(1);
}
#endif

#ifdef UART2_BASE
extern "C" void UART2_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(2);
}
#endif

#ifdef UART3_BASE
extern "C" void UART3_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(3);
}
#endif

#ifdef UART4_BASE
extern "C" void UART4_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(4); }
#endif

#ifdef UART5_BASE
extern "C" void UART5_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(5); }
#endif

#ifdef UART6_BASE
extern "C" void UART6_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(6); }
#endif

#ifdef UART7_BASE
extern "C" void UART7_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(7); }
#endif
