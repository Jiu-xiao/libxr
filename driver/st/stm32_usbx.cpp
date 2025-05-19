#include "stm32_usbx.hpp"

#if defined(HAL_PCD_MODULE_ENABLED) && defined(LIBXR_SYSTEM_ThreadX)

#include "app_azure_rtos_config.h"
#include "ux_device_descriptors.h"

namespace LibXR
{
static uint8_t usbx_memory[UX_DEVICE_APP_MEM_POOL_SIZE];
static UX_SLAVE_CLASS_CDC_ACM_PARAMETER cdc_acm_param;
STM32VirtualUART *STM32VirtualUART::instance_ = nullptr;
extern "C" void usbx_dcd_stm32_initialize(ULONG dcd_io, ULONG parameter);

extern "C" void USBD_CDC_ACM_Activate(void *cdc_instance)
{
  if (STM32VirtualUART::instance_)
  {
    STM32VirtualUART::instance_->cdc_acm_ =
        static_cast<UX_SLAVE_CLASS_CDC_ACM *>(cdc_instance);
  }
}

extern "C" void USBD_CDC_ACM_Deactivate(void *cdc_instance)
{
  UNUSED(cdc_instance);
  if (STM32VirtualUART::instance_)
  {
    STM32VirtualUART::instance_->cdc_acm_ = nullptr;
  }
}

STM32VirtualUART::STM32VirtualUART(PCD_HandleTypeDef *hpcd, ULONG tx_stack_size,
                                   UINT tx_priority, ULONG rx_stack_size,
                                   UINT rx_priority, uint32_t tx_queue_size,
                                   size_t buffer_size)
    : UART(&_read_port, &_write_port),
      tx_stack_size_(tx_stack_size),
      rx_stack_size_(rx_stack_size),
      tx_priority_(tx_priority),
      rx_priority_(rx_priority),
      buffer_size_(buffer_size),
      rx_buff_(new uint8_t[buffer_size]),
      tx_buff_(new uint8_t[buffer_size]),
      write_sem_(0),
      _read_port(buffer_size),
      _write_port(tx_queue_size, buffer_size)
{
  // 初始化 USBX 协议栈
  ux_system_initialize(usbx_memory, UX_DEVICE_APP_MEM_POOL_SIZE, UX_NULL, 0);

  // 获取设备描述符/字符串/语言ID
  ULONG fs_length, str_length, lang_length;
  UCHAR *fs_desc = USBD_Get_Device_Framework_Speed(USBD_FULL_SPEED, &fs_length);
  UCHAR *str_desc = USBD_Get_String_Framework(&str_length);
  UCHAR *lang_desc = USBD_Get_Language_Id_Framework(&lang_length);

  // 初始化 USB 设备栈
  ux_device_stack_initialize(NULL, 0, fs_desc, fs_length, str_desc, str_length, lang_desc,
                             lang_length, NULL);

  // 设置 CDC ACM 参数
  cdc_acm_param.ux_slave_class_cdc_acm_instance_activate = USBD_CDC_ACM_Activate;
  cdc_acm_param.ux_slave_class_cdc_acm_instance_deactivate = USBD_CDC_ACM_Deactivate;

  // 注册 CDC ACM 类（interface 0, config 1）
  ux_device_stack_class_register(_ux_system_slave_class_cdc_acm_name,
                                 ux_device_class_cdc_acm_entry, 1, 0, &cdc_acm_param);

  usbx_dcd_stm32_initialize(reinterpret_cast<ULONG>(hpcd->Instance),
                            reinterpret_cast<ULONG>(hpcd));

  tx_stack_mem_ = new uint8_t[tx_stack_size_];
  rx_stack_mem_ = new uint8_t[rx_stack_size_];

  ASSERT(tx_stack_mem_ && rx_stack_mem_);

  _read_port = ReadFun;
  _write_port = WriteFun;

  STM32VirtualUART::instance_ = this;

  static CHAR thread_name_tx_[8] = "usbx_tx";
  static CHAR thread_name_rx_[8] = "usbx_rx";

  tx_thread_create(&tx_thread_, thread_name_tx_, &STM32VirtualUART::TxThreadEntry,
                   ULONG(this), tx_stack_mem_, tx_stack_size_, tx_priority_, tx_priority_,
                   TX_NO_TIME_SLICE, TX_AUTO_START);

  tx_thread_create(&rx_thread_, thread_name_rx_, &STM32VirtualUART::RxThreadEntry,
                   ULONG(this), rx_stack_mem_, rx_stack_size_, rx_priority_, rx_priority_,
                   TX_NO_TIME_SLICE, TX_AUTO_START);

  HAL_PCD_Start(hpcd);
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
  UNUSED(port);

  return ErrorCode::EMPTY;
}

ErrorCode STM32VirtualUART::WriteFun(WritePort &port)
{
  auto *uart = CONTAINER_OF(&port, STM32VirtualUART, _write_port);

  uart->write_sem_.Post();
  return ErrorCode::EMPTY;
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
      Mutex::LockGuard guard(read_mutex_);
      read_port_->queue_data_->PushBatch(rx_buff_, actual_len);
      read_port_->ProcessPendingReads(false);
    }
  }
}

void STM32VirtualUART::TxLoop()
{
  WriteInfoBlock info;
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

    if (write_port_->queue_info_->Pop(info) == ErrorCode::OK)
    {
      if (write_port_->queue_data_->PopBatch(tx_buff_, info.data.size_) == ErrorCode::OK)
      {
        ULONG actual = 0;
        UINT status =
            _ux_device_class_cdc_acm_write(cdc_acm_, tx_buff_, info.data.size_, &actual);
        write_port_->Finish(false,
                            (status == UX_SUCCESS && actual == info.data.size_)
                                ? ErrorCode::OK
                                : ErrorCode::FAILED,
                            info, actual);
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

}  // namespace LibXR

#endif
