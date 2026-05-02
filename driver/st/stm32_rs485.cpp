#include "stm32_rs485.hpp"

#ifdef HAL_UART_MODULE_ENABLED

#include "libxr_def.hpp"
#include "stm32_dcache.hpp"
#include "stm32_gpio.hpp"
#include "timebase.hpp"

using namespace LibXR;

STM32RS485* STM32RS485::map[STM32_UART_NUMBER] = {nullptr};

STM32RS485::STM32RS485(UART_HandleTypeDef* uart_handle, RawData rx_buffer,
                       RawData tx_buffer, STM32GPIO* tx_enable)
    : uart_handle_(uart_handle),
      tx_enable_(tx_enable),
      rx_buffer_(rx_buffer),
      tx_buffer_(tx_buffer),
      id_(stm32_uart_get_id(uart_handle_->Instance))
{
  ASSERT(id_ != STM32_UART_ID_ERROR);
  ASSERT(tx_buffer_.addr_ != nullptr && tx_buffer_.size_ > 0);
  ASSERT(uart_handle_->hdmatx != nullptr);
  ASSERT(rx_buffer_.addr_ == nullptr || rx_buffer_.size_ == 0 ||
         uart_handle_->hdmarx != nullptr);
  ASSERT(tx_buffer_.size_ <= UINT16_MAX);
  ASSERT(rx_buffer_.size_ <= UINT16_MAX);
  map[id_] = this;
  SetReceiveDirection();
  ArmReceive();
}

ErrorCode STM32RS485::SetConfig(const Configuration& config)
{
  if (tx_busy_.IsSet())
  {
    return ErrorCode::BUSY;
  }
  if (config.data_bits < 5 || config.data_bits > 8)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  bool ok = true;
  uint32_t parity = UART_PARITY_NONE;
  uint32_t word_length = UART_WORDLENGTH_8B;
  uint32_t stop_bits = UART_STOPBITS_1;

  switch (config.parity)
  {
    case UART::Parity::NO_PARITY:
      parity = UART_PARITY_NONE;
      if (config.data_bits == 8)
      {
        word_length = UART_WORDLENGTH_8B;
      }
      else
      {
        ok = false;
      }
      break;
    case UART::Parity::EVEN:
      parity = UART_PARITY_EVEN;
      if (config.data_bits == 8)
      {
        word_length = UART_WORDLENGTH_9B;
      }
#ifdef UART_WORDLENGTH_8B
      else if (config.data_bits == 7)
      {
        word_length = UART_WORDLENGTH_8B;
      }
#endif
      else
      {
        ok = false;
      }
      break;
    case UART::Parity::ODD:
      parity = UART_PARITY_ODD;
      if (config.data_bits == 8)
      {
        word_length = UART_WORDLENGTH_9B;
      }
#ifdef UART_WORDLENGTH_8B
      else if (config.data_bits == 7)
      {
        word_length = UART_WORDLENGTH_8B;
      }
#endif
      else
      {
        ok = false;
      }
      break;
    default:
      ok = false;
      break;
  }

  switch (config.stop_bits)
  {
    case 1:
      stop_bits = UART_STOPBITS_1;
      break;
    case 2:
      stop_bits = UART_STOPBITS_2;
      break;
    default:
      ok = false;
      break;
  }

  if (!ok)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  const auto old_init = uart_handle_->Init;
  (void)HAL_UART_Abort(uart_handle_);
  HAL_UART_DeInit(uart_handle_);

  uart_handle_->Init.BaudRate = config.baudrate;
  uart_handle_->Init.Parity = parity;
  uart_handle_->Init.WordLength = word_length;
  uart_handle_->Init.StopBits = stop_bits;

  if (HAL_UART_Init(uart_handle_) != HAL_OK)
  {
    uart_handle_->Init = old_init;
    if (HAL_UART_Init(uart_handle_) == HAL_OK)
    {
      SetReceiveDirection();
      ArmReceive();
    }
    return ErrorCode::INIT_ERR;
  }

  config_ = config;
  SetReceiveDirection();
  ArmReceive();
  return ErrorCode::OK;
}

ErrorCode STM32RS485::Write(ConstRawData frame, WriteOperation& op, bool in_isr)
{
  if (in_isr && op.type == WriteOperation::OperationType::BLOCK)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  if (frame.size_ == 0)
  {
    if (op.type != WriteOperation::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, ErrorCode::OK);
    }
    return ErrorCode::OK;
  }

  if (frame.addr_ == nullptr || frame.size_ > tx_buffer_.size_ ||
      frame.size_ > UINT16_MAX)
  {
    return ErrorCode::SIZE_ERR;
  }

  if (config_.data_bits > 8)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  if (tx_busy_.TestAndSet())
  {
    return ErrorCode::BUSY;
  }

  write_op_ = op;
  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    block_wait_.Start(*op.data.sem_info.sem);
  }

  Memory::FastCopy(tx_buffer_.addr_, frame.addr_, frame.size_);
  STM32_CleanDCacheByAddr(tx_buffer_.addr_, frame.size_);

  if (rx_buffer_.addr_ != nullptr && rx_buffer_.size_ > 0)
  {
    (void)HAL_UART_AbortReceive(uart_handle_);
  }

  SetTransmitDirection();
  if (config_.assert_time_us != 0u)
  {
    Timebase::DelayMicroseconds(config_.assert_time_us);
  }

  op.MarkAsRunning();
  const HAL_StatusTypeDef st =
      HAL_UART_Transmit_DMA(uart_handle_, static_cast<uint8_t*>(tx_buffer_.addr_),
                            static_cast<uint16_t>(frame.size_));

  if (st != HAL_OK)
  {
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      block_wait_.Cancel();
    }
    tx_busy_.Clear();
    write_op_ = WriteOperation();
    SetReceiveDirection();
    ArmReceive();
    if (op.type != WriteOperation::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, ErrorCode::BUSY);
    }
    return ErrorCode::BUSY;
  }

  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    return block_wait_.Wait(op.data.sem_info.timeout);
  }
  return ErrorCode::OK;
}

void STM32RS485::Reset()
{
  const bool was_busy = tx_busy_.IsSet();
  (void)HAL_UART_Abort(uart_handle_);

  if (was_busy)
  {
    FinishWrite(false, ErrorCode::FAILED);
  }

  SetReceiveDirection();
  ArmReceive();
}

void STM32RS485::OnRxEvent(uint16_t size, bool in_isr)
{
  if (rx_buffer_.addr_ == nullptr || rx_buffer_.size_ == 0 || tx_busy_.IsSet())
  {
    return;
  }

  const size_t rx_size = (size <= rx_buffer_.size_) ? size : rx_buffer_.size_;
  STM32_InvalidateDCacheByAddr(rx_buffer_.addr_, rx_size);
  if (rx_size != 0)
  {
    OnFrame({rx_buffer_.addr_, rx_size}, in_isr);
  }
  ArmReceive();
}

void STM32RS485::OnTxComplete(bool in_isr)
{
  if (!tx_busy_.IsSet())
  {
    return;
  }

  if (config_.deassert_time_us != 0u)
  {
    Timebase::DelayMicroseconds(config_.deassert_time_us);
  }
  SetReceiveDirection();
  FinishWrite(in_isr, ErrorCode::OK);
  ArmReceive();
}

void STM32RS485::OnError(bool in_isr)
{
  const bool was_busy = tx_busy_.IsSet();
  if (was_busy)
  {
    FinishWrite(in_isr, ErrorCode::FAILED);
  }
  SetReceiveDirection();
}

void STM32RS485::OnAbortComplete()
{
  SetReceiveDirection();
  ArmReceive();
}

void STM32RS485::ArmReceive()
{
  if (rx_buffer_.addr_ == nullptr || rx_buffer_.size_ == 0 || tx_busy_.IsSet())
  {
    return;
  }

  STM32_InvalidateDCacheByAddr(rx_buffer_.addr_, rx_buffer_.size_);
  (void)HAL_UARTEx_ReceiveToIdle_DMA(uart_handle_,
                                     static_cast<uint8_t*>(rx_buffer_.addr_),
                                     static_cast<uint16_t>(rx_buffer_.size_));
}

void STM32RS485::SetTransmitDirection()
{
#ifdef HAL_GPIO_MODULE_ENABLED
  if (tx_enable_ != nullptr)
  {
    tx_enable_->Write(config_.tx_active_level);
  }
#endif
}

void STM32RS485::SetReceiveDirection()
{
#ifdef HAL_GPIO_MODULE_ENABLED
  if (tx_enable_ != nullptr)
  {
    tx_enable_->Write(!config_.tx_active_level);
  }
#endif
}

void STM32RS485::FinishWrite(bool in_isr, ErrorCode ec)
{
  tx_busy_.Clear();
  auto op = write_op_;
  write_op_ = WriteOperation();

  if (op.type == WriteOperation::OperationType::BLOCK)
  {
    (void)block_wait_.TryPost(in_isr, ec);
  }
  else if (op.type != WriteOperation::OperationType::NONE)
  {
    op.UpdateStatus(in_isr, ec);
  }
}

#endif
