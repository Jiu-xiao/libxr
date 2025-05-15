#include "stm32_usbx.hpp"

#include <cstdlib>

namespace LibXR
{

STM32VirtualUART *STM32VirtualUART::instance_ = nullptr;

STM32VirtualUART::STM32VirtualUART(ULONG tx_stack_size, UINT tx_priority,
                                   ULONG rx_stack_size, UINT rx_priority,
                                   uint32_t rx_queue_size, uint32_t tx_queue_size,
                                   size_t buffer_size)
    : UART(rx_queue_size, buffer_size, tx_queue_size, buffer_size),
      tx_stack_size_(tx_stack_size),
      rx_stack_size_(rx_stack_size),
      tx_priority_(tx_priority),
      rx_priority_(rx_priority),
      buffer_size_(buffer_size),
      rx_buff_(new uint8_t[buffer_size]),
      tx_buff_(new uint8_t[buffer_size]),
      write_sem_(0)
{
  tx_stack_mem_ = malloc(tx_stack_size_);
  rx_stack_mem_ = malloc(rx_stack_size_);

  ASSERT(tx_stack_mem_ && rx_stack_mem_);

  read_port_ = ReadFun;
  write_port_ = WriteFun;

  static CHAR thread_name_tx_[8];
  static CHAR thread_name_rx_[8];

  tx_thread_create(&tx_thread_, thread_name_tx_, &STM32VirtualUART::TxThreadEntry,
                   ULONG(this), tx_stack_mem_, tx_stack_size_, tx_priority_, tx_priority_,
                   TX_NO_TIME_SLICE, TX_AUTO_START);

  tx_thread_create(&rx_thread_, thread_name_rx_, &STM32VirtualUART::RxThreadEntry,
                   ULONG(this), rx_stack_mem_, rx_stack_size_, rx_priority_, rx_priority_,
                   TX_NO_TIME_SLICE, TX_AUTO_START);

  STM32VirtualUART::instance_ = this;
}

STM32VirtualUART::~STM32VirtualUART()
{
  tx_thread_terminate(&tx_thread_);
  tx_thread_delete(&tx_thread_);
  tx_thread_terminate(&rx_thread_);
  tx_thread_delete(&rx_thread_);

  free(tx_stack_mem_);
  free(rx_stack_mem_);
  delete[] rx_buff_;
  delete[] tx_buff_;
}

ErrorCode STM32VirtualUART::SetConfig(UART::Configuration config)
{
  UNUSED(config);
  return ErrorCode::OK;
}

ErrorCode STM32VirtualUART::ReadFun(ReadPort &port)
{
  auto *uart = CONTAINER_OF(&port, STM32VirtualUART, read_port_);
  Mutex::LockGuard guard(uart->read_mutex_);
  port.ProcessPendingReads();
  return ErrorCode::OK;
}

ErrorCode STM32VirtualUART::WriteFun(WritePort &port)
{
  auto *uart = CONTAINER_OF(&port, STM32VirtualUART, write_port_);
  WritePort::WriteInfo info;
  if (port.queue_info_->Peek(info) != ErrorCode::OK)
  {
    return ErrorCode::EMPTY;
  }
  port.UpdateStatus(info.op);
  uart->write_sem_.Post();
  return ErrorCode::OK;
}

void STM32VirtualUART::RxLoop()
{
  while (true)
  {
    if (!cdc_acm_)
    {
      tx_thread_sleep(1);
      continue;
    }
    ULONG actual_len = 0;
    UINT status =
        _ux_device_class_cdc_acm_read(cdc_acm_, rx_buff_, buffer_size_, &actual_len);
    if (status == UX_SUCCESS && actual_len > 0)
    {
      read_port_.queue_data_->PushBatch(rx_buff_, actual_len);
      Mutex::LockGuard guard(read_mutex_);
      read_port_.ProcessPendingReads();
    }
    else
    {
      tx_thread_sleep(1);
    }
  }
}

void STM32VirtualUART::TxLoop()
{
  WritePort::WriteInfo info;
  while (true)
  {
    if (!cdc_acm_)
    {
      tx_thread_sleep(1);
      continue;
    }

    if (write_sem_.Wait() != ErrorCode::OK)
    {
      continue;
    }

    if (write_port_.queue_info_->Pop(info) == ErrorCode::OK)
    {
      if (write_port_.queue_data_->PopBatch(tx_buff_, info.size) == ErrorCode::OK)
      {
        ULONG actual = 0;
        UINT status =
            _ux_device_class_cdc_acm_write(cdc_acm_, tx_buff_, info.size, &actual);
        info.op.UpdateStatus(false, (status == UX_SUCCESS && actual == info.size)
                                        ? ErrorCode::OK
                                        : ErrorCode::FAILED);
      }
      else
      {
        ASSERT(false);
        info.op.UpdateStatus(false, ErrorCode::FAILED);
      }
    }
  }
}

void STM32VirtualUART::RxThreadEntry(ULONG arg)
{
  static_cast<STM32VirtualUART *>(reinterpret_cast<void *>(arg))->RxLoop();
}

void STM32VirtualUART::TxThreadEntry(ULONG arg)
{
  static_cast<STM32VirtualUART *>(reinterpret_cast<void *>(arg))->TxLoop();
}

extern "C" UINT _ux_device_class_cdc_acm_instance_activate(void *cdc_instance)
{
  using namespace LibXR;
  if (STM32VirtualUART::instance_)
  {
    STM32VirtualUART::instance_->cdc_acm_ =
        static_cast<UX_SLAVE_CLASS_CDC_ACM *>(cdc_instance);
  }
  return UX_SUCCESS;
}

}  // namespace LibXR
