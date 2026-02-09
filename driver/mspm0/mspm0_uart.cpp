#include "mspm0_uart.hpp"

using namespace LibXR;

MSPM0UART* MSPM0UART::instance_map_[MAX_UART_INSTANCES] = {nullptr};

static constexpr uint32_t MSPM0_UART_RX_ERROR_INTERRUPT_MASK =
    DL_UART_MAIN_INTERRUPT_OVERRUN_ERROR | DL_UART_MAIN_INTERRUPT_BREAK_ERROR |
    DL_UART_MAIN_INTERRUPT_PARITY_ERROR | DL_UART_MAIN_INTERRUPT_FRAMING_ERROR |
    DL_UART_MAIN_INTERRUPT_NOISE_ERROR;

static constexpr uint32_t MSPM0_UART_DEFAULT_INTERRUPT_MASK =
    DL_UART_MAIN_INTERRUPT_RX | MSPM0_UART_RX_ERROR_INTERRUPT_MASK;

MSPM0UART::MSPM0UART(Resources res, RawData rx_stage_buffer, uint32_t tx_queue_size,
                     uint32_t tx_buffer_size, UART::Configuration config)
    : UART(&read_port_impl_, &write_port_impl_),
      read_port_impl_(rx_stage_buffer.size_),
      write_port_impl_(tx_queue_size, tx_buffer_size),
      res_(res)
{
  ASSERT(res_.instance != nullptr);
  ASSERT(res_.clock_freq > 0);
  ASSERT(rx_stage_buffer.addr_ != nullptr);
  ASSERT(rx_stage_buffer.size_ > 0);
  ASSERT(tx_queue_size > 0);
  ASSERT(tx_buffer_size > 0);

  read_port_impl_ = ReadFun;
  write_port_impl_ = WriteFun;

  ASSERT(res_.index < MAX_UART_INSTANCES);
  ASSERT(instance_map_[res_.index] == nullptr);
  instance_map_[res_.index] = this;

  NVIC_ClearPendingIRQ(res_.irqn);
  NVIC_EnableIRQ(res_.irqn);

  const ErrorCode SET_CFG_ANS = SetConfig(config);
  ASSERT(SET_CFG_ANS == ErrorCode::OK);
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

  DL_UART_Main_changeConfig(res_.instance);

  DL_UART_Main_setWordLength(res_.instance, word_length);
  DL_UART_Main_setParityMode(res_.instance, parity);
  DL_UART_Main_setStopBits(res_.instance, STOP_BITS);

  DL_UART_Main_enableFIFOs(res_.instance);
  DL_UART_Main_setRXFIFOThreshold(res_.instance, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
  DL_UART_Main_setTXFIFOThreshold(res_.instance, DL_UART_TX_FIFO_LEVEL_ONE_ENTRY);

  DL_UART_Main_configBaudRate(res_.instance, res_.clock_freq, config.baudrate);

  DL_UART_Main_clearInterruptStatus(res_.instance, 0xFFFFFFFF);
  DL_UART_Main_enableInterrupt(res_.instance, MSPM0_UART_DEFAULT_INTERRUPT_MASK);
  DL_UART_Main_disableInterrupt(res_.instance, DL_UART_MAIN_INTERRUPT_TX);

  DL_UART_Main_enable(res_.instance);

  return ErrorCode::OK;
}

ErrorCode MSPM0UART::WriteFun(WritePort& port)
{
  auto* uart = CONTAINER_OF(&port, MSPM0UART, write_port_impl_);
  if (port.queue_info_->Size() == 0)
  {
    return ErrorCode::OK;
  }

  DL_UART_Main_enableInterrupt(uart->res_.instance, DL_UART_MAIN_INTERRUPT_TX);
  uart->res_.instance->CPU_INT.ISET = DL_UART_MAIN_INTERRUPT_TX;
  return ErrorCode::OK;
}

ErrorCode MSPM0UART::ReadFun(ReadPort& port)
{
  auto* uart = CONTAINER_OF(&port, MSPM0UART, read_port_impl_);
  UNUSED(uart);
  return ErrorCode::EMPTY;
}

void MSPM0UART::OnInterrupt(UART_Regs* instance)
{
  auto* uart = FindByInstance(instance);
  if (uart == nullptr)
  {
    return;
  }

  uart->HandleInterrupt();
}

MSPM0UART* MSPM0UART::FindByInstance(UART_Regs* instance)
{
  for (uint8_t index = 0; index < MAX_UART_INSTANCES; ++index)
  {
    auto* uart = instance_map_[index];
    if (uart != nullptr && uart->res_.instance == instance)
    {
      return uart;
    }
  }

  return nullptr;
}

void MSPM0UART::HandleInterrupt()
{
  constexpr uint32_t IRQ_MASK = DL_UART_MAIN_INTERRUPT_RX | DL_UART_MAIN_INTERRUPT_TX |
                                MSPM0_UART_RX_ERROR_INTERRUPT_MASK;

  constexpr uint32_t MAX_IRQ_ROUNDS = 32;
  for (uint32_t round = 0; round < MAX_IRQ_ROUNDS; ++round)
  {
    const uint32_t PENDING =
        DL_UART_Main_getEnabledInterruptStatus(res_.instance, IRQ_MASK);
    if (PENDING == 0)
    {
      return;
    }

    if ((PENDING & DL_UART_MAIN_INTERRUPT_RX) != 0)
    {
      HandleRxInterrupt();
    }

    if ((PENDING & DL_UART_MAIN_INTERRUPT_OVERRUN_ERROR) != 0)
    {
      HandleErrorInterrupt(DL_UART_MAIN_IIDX_OVERRUN_ERROR);
    }
    if ((PENDING & DL_UART_MAIN_INTERRUPT_BREAK_ERROR) != 0)
    {
      HandleErrorInterrupt(DL_UART_MAIN_IIDX_BREAK_ERROR);
    }
    if ((PENDING & DL_UART_MAIN_INTERRUPT_PARITY_ERROR) != 0)
    {
      HandleErrorInterrupt(DL_UART_MAIN_IIDX_PARITY_ERROR);
    }
    if ((PENDING & DL_UART_MAIN_INTERRUPT_FRAMING_ERROR) != 0)
    {
      HandleErrorInterrupt(DL_UART_MAIN_IIDX_FRAMING_ERROR);
    }
    if ((PENDING & DL_UART_MAIN_INTERRUPT_NOISE_ERROR) != 0)
    {
      HandleErrorInterrupt(DL_UART_MAIN_IIDX_NOISE_ERROR);
    }

    if ((PENDING & DL_UART_MAIN_INTERRUPT_TX) != 0)
    {
      HandleTxInterrupt(true);
    }
  }

  DL_UART_Main_clearInterruptStatus(res_.instance, IRQ_MASK);
  NVIC_ClearPendingIRQ(res_.irqn);
}

void MSPM0UART::HandleRxInterrupt()
{
  bool pushed = false;

  while (!DL_UART_Main_isRXFIFOEmpty(res_.instance))
  {
    uint8_t rx_byte = DL_UART_Main_receiveData(res_.instance);
    if (read_port_->queue_data_->Push(rx_byte) == ErrorCode::OK)
    {
      pushed = true;
    }
    else
    {
      rx_drop_count_++;
    }
  }

  if (pushed)
  {
    read_port_->ProcessPendingReads(true);
  }

  DL_UART_Main_clearInterruptStatus(res_.instance, DL_UART_MAIN_INTERRUPT_RX);
}

void MSPM0UART::HandleTxInterrupt(bool in_isr)
{
  while (true)
  {
    if (!tx_active_valid_)
    {
      if (write_port_->queue_info_->Pop(tx_active_info_) != ErrorCode::OK)
      {
        DisableTxInterrupt();

        if (write_port_->queue_info_->Size() > 0)
        {
          DL_UART_Main_enableInterrupt(res_.instance, DL_UART_MAIN_INTERRUPT_TX);
          res_.instance->CPU_INT.ISET = DL_UART_MAIN_INTERRUPT_TX;
        }

        return;
      }

      tx_active_total_ = tx_active_info_.data.size_;
      tx_active_remaining_ = tx_active_total_;
      tx_active_valid_ = true;
    }

    while (tx_active_remaining_ > 0 && !DL_UART_Main_isTXFIFOFull(res_.instance))
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

      DL_UART_Main_transmitData(res_.instance, tx_byte);
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
    case DL_UART_MAIN_IIDX_OVERRUN_ERROR:
      clear_mask = DL_UART_MAIN_INTERRUPT_OVERRUN_ERROR;
      break;

    case DL_UART_MAIN_IIDX_BREAK_ERROR:
      clear_mask = DL_UART_MAIN_INTERRUPT_BREAK_ERROR;
      break;

    case DL_UART_MAIN_IIDX_PARITY_ERROR:
      clear_mask = DL_UART_MAIN_INTERRUPT_PARITY_ERROR;
      break;

    case DL_UART_MAIN_IIDX_FRAMING_ERROR:
      clear_mask = DL_UART_MAIN_INTERRUPT_FRAMING_ERROR;
      break;

    case DL_UART_MAIN_IIDX_NOISE_ERROR:
      clear_mask = DL_UART_MAIN_INTERRUPT_NOISE_ERROR;
      break;

    case DL_UART_MAIN_IIDX_RX_TIMEOUT_ERROR:
      clear_mask = DL_UART_MAIN_INTERRUPT_RX_TIMEOUT_ERROR;
      break;

    default:
      break;
  }

  if (clear_mask != 0)
  {
    DL_UART_Main_clearInterruptStatus(res_.instance, clear_mask);
  }
}

ErrorCode MSPM0UART::TryStartTx(bool in_isr)
{
  UNUSED(in_isr);
  DL_UART_Main_enableInterrupt(res_.instance, DL_UART_MAIN_INTERRUPT_TX);
  return ErrorCode::OK;
}

void MSPM0UART::DisableTxInterrupt()
{
  DL_UART_Main_disableInterrupt(res_.instance, DL_UART_MAIN_INTERRUPT_TX);
  DL_UART_Main_clearInterruptStatus(res_.instance, DL_UART_MAIN_INTERRUPT_TX);
  NVIC_ClearPendingIRQ(res_.irqn);
}

#if defined(UART0_BASE)
extern "C" void UART0_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(UART0);
}
#endif

#if defined(UART1_BASE)
extern "C" void UART1_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(UART1);
}
#endif

#if defined(UART2_BASE)
extern "C" void UART2_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(UART2);
}
#endif

#if defined(UART3_BASE)
extern "C" void UART3_IRQHandler(void)  // NOLINT
{
  LibXR::MSPM0UART::OnInterrupt(UART3);
}
#endif

#if defined(UART4_BASE)
extern "C" void UART4_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(UART4); }
#endif

#if defined(UART5_BASE)
extern "C" void UART5_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(UART5); }
#endif

#if defined(UART6_BASE)
extern "C" void UART6_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(UART6); }
#endif

#if defined(UART7_BASE)
extern "C" void UART7_IRQHandler(void) { LibXR::MSPM0UART::OnInterrupt(UART7); }
#endif
