#include "esp_cdc_jtag.hpp"

#if SOC_USB_SERIAL_JTAG_SUPPORTED &&                                              \
    ((defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) ||         \
     (defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6))

#include <algorithm>
#include <cstring>
#include <new>

#include "esp_attr.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/interrupts.h"

namespace
{
constexpr uint32_t kWriteSpinWait = 20000U;
constexpr uint32_t kWriteStallRetries = 16U;
constexpr size_t kTxChunkMax = 2048U;
constexpr uint32_t kTxIntrMask = USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;
}  // namespace

namespace LibXR
{

ESP32CDCJtag::ESP32CDCJtag(size_t rx_buffer_size, size_t tx_buffer_size,
                           uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      config_(config),
      tx_work_buffer_(new (std::nothrow) uint8_t[tx_buffer_size]),
      tx_work_buffer_size_(tx_buffer_size),
      tx_slot_storage_(new (std::nothrow) uint8_t[tx_buffer_size * 2U]),
      tx_slot_size_(tx_buffer_size),
      _read_port(rx_buffer_size),
      _write_port(tx_queue_size, tx_buffer_size)
{
  ASSERT(tx_work_buffer_ != nullptr);
  ASSERT(tx_slot_storage_ != nullptr);
  ASSERT(tx_work_buffer_size_ > 0);
  ASSERT(tx_slot_size_ > 0);

  tx_slot_a_ = tx_slot_storage_;
  tx_slot_b_ = tx_slot_storage_ + tx_slot_size_;

  _read_port = ReadFun;
  _write_port = WriteFun;
  _write_port.fast_write_fun_ = FastWriteFun;

  if (SetConfig(config_) != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

  if (InitHardware() != ErrorCode::OK)
  {
    ASSERT(false);
  }
}

ESP32CDCJtag::~ESP32CDCJtag()
{
  DeinitHardware();
  delete[] tx_slot_storage_;
  tx_slot_storage_ = nullptr;
  delete[] tx_work_buffer_;
  tx_work_buffer_ = nullptr;
}

ErrorCode ESP32CDCJtag::SetConfig(UART::Configuration config)
{
  if ((config.data_bits != 8) || (config.stop_bits != 1) ||
      (config.parity != UART::Parity::NO_PARITY))
  {
    return ErrorCode::ARG_ERR;
  }
  config_ = config;
  return ErrorCode::OK;
}

ErrorCode ESP32CDCJtag::InitHardware()
{
  if (hw_inited_)
  {
    return ErrorCode::OK;
  }

  ResetTxState(false);
  tx_source_done_.store(true, std::memory_order_release);

  const esp_err_t intr_ans = esp_intr_alloc(ETS_USB_SERIAL_JTAG_INTR_SOURCE,
                                            ESP_INTR_FLAG_IRAM, IsrEntry, this,
                                            &intr_handle_);
  intr_installed_ = (intr_ans == ESP_OK);
  if (!intr_installed_)
  {
    intr_handle_ = nullptr;
    return ErrorCode::INIT_ERR;
  }

  hw_inited_ = true;
  return ErrorCode::OK;
}

void ESP32CDCJtag::DeinitHardware()
{
  if (!hw_inited_)
  {
    return;
  }

  if (intr_installed_)
  {
    usb_serial_jtag_ll_disable_intr_mask(kTxIntrMask);
    if (intr_handle_ != nullptr)
    {
      (void)esp_intr_free(intr_handle_);
      intr_handle_ = nullptr;
    }
    intr_installed_ = false;
  }

  ResetTxState(false);
  tx_source_done_.store(true, std::memory_order_release);
  hw_inited_ = false;
}

void IRAM_ATTR ESP32CDCJtag::IsrEntry(void* arg)
{
  auto* cdc = static_cast<ESP32CDCJtag*>(arg);
  if (cdc != nullptr)
  {
    cdc->HandleInterrupt();
  }
}

ErrorCode IRAM_ATTR ESP32CDCJtag::WriteFun(WritePort& port, bool in_isr)
{
  auto* cdc = CONTAINER_OF(&port, ESP32CDCJtag, _write_port);

  WriteInfoBlock info = {};
  auto ans = port.queue_info_->Pop(info);
  if (ans != ErrorCode::OK)
  {
    return ErrorCode::PENDING;
  }

  if (info.data.size_ == 0)
  {
    port.Finish(in_isr, ErrorCode::OK, info);
    return ErrorCode::PENDING;
  }

  if (info.data.size_ > cdc->tx_work_buffer_size_)
  {
    (void)port.queue_data_->PopBatch(nullptr, info.data.size_);
    port.Finish(in_isr, ErrorCode::SIZE_ERR, info);
    return ErrorCode::PENDING;
  }

  ans = port.queue_data_->PopBatch(cdc->tx_work_buffer_, info.data.size_);
  if (ans != ErrorCode::OK)
  {
    port.Finish(in_isr, ErrorCode::FAILED, info);
    return ErrorCode::PENDING;
  }

  ans = cdc->WriteBlocking(cdc->tx_work_buffer_, info.data.size_, in_isr);
  port.Finish(in_isr, ans, info);
  return ErrorCode::PENDING;
}

ErrorCode ESP32CDCJtag::ReadFun(ReadPort&, bool)
{
  return ErrorCode::PENDING;
}

ErrorCode IRAM_ATTR ESP32CDCJtag::FastWriteFun(WritePort& port, ConstRawData data,
                                               WriteOperation& op, bool in_isr)
{
  auto* cdc = CONTAINER_OF(&port, ESP32CDCJtag, _write_port);
  if (cdc == nullptr)
  {
    return ErrorCode::FAILED;
  }

  if (op.type != WriteOperation::OperationType::BLOCK)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  if ((data.addr_ == nullptr) || (data.size_ == 0U))
  {
    return (data.size_ == 0U) ? ErrorCode::OK : ErrorCode::ARG_ERR;
  }

  WritePort::LockState expected = WritePort::LockState::UNLOCKED;
  if (!port.lock_.compare_exchange_strong(expected, WritePort::LockState::LOCKED))
  {
    return ErrorCode::BUSY;
  }

  const ErrorCode ans = cdc->WriteBlocking(
      reinterpret_cast<const uint8_t*>(data.addr_), data.size_, in_isr);

  port.lock_.store(WritePort::LockState::UNLOCKED, std::memory_order_release);
  return ans;
}

void IRAM_ATTR ESP32CDCJtag::ResetTxState(bool in_isr)
{
  if (in_isr)
  {
    portENTER_CRITICAL_ISR(&tx_lock_);
  }
  else
  {
    portENTER_CRITICAL(&tx_lock_);
  }

  tx_active_ptr_ = nullptr;
  tx_active_size_ = 0;
  tx_active_offset_ = 0;
  tx_pending_ptr_ = nullptr;
  tx_pending_size_ = 0;
  tx_pending_valid_ = false;
  tx_source_done_.store(true, std::memory_order_release);
  tx_busy_.store(false, std::memory_order_release);
  tx_event_seq_.fetch_add(1, std::memory_order_acq_rel);

  if (in_isr)
  {
    portEXIT_CRITICAL_ISR(&tx_lock_);
  }
  else
  {
    portEXIT_CRITICAL(&tx_lock_);
  }
}

bool IRAM_ATTR ESP32CDCJtag::PumpTx(bool in_isr)
{
  bool wrote_any = false;
  bool became_idle = false;

  if (in_isr)
  {
    portENTER_CRITICAL_ISR(&tx_lock_);
  }
  else
  {
    portENTER_CRITICAL(&tx_lock_);
  }

  while (tx_busy_.load(std::memory_order_acquire))
  {
    if ((tx_active_ptr_ == nullptr) || (tx_active_offset_ >= tx_active_size_))
    {
      if (tx_pending_valid_)
      {
        tx_active_ptr_ = tx_pending_ptr_;
        tx_active_size_ = tx_pending_size_;
        tx_active_offset_ = 0;
        tx_pending_ptr_ = nullptr;
        tx_pending_size_ = 0;
        tx_pending_valid_ = false;
        continue;
      }

      if (tx_source_done_.load(std::memory_order_acquire))
      {
        tx_active_ptr_ = nullptr;
        tx_active_size_ = 0;
        tx_active_offset_ = 0;
        tx_pending_ptr_ = nullptr;
        tx_pending_size_ = 0;
        tx_pending_valid_ = false;
        tx_busy_.store(false, std::memory_order_release);
        became_idle = true;
      }
      break;
    }

    const uint32_t remain =
        static_cast<uint32_t>(tx_active_size_ - tx_active_offset_);
    const int written =
        usb_serial_jtag_ll_write_txfifo(tx_active_ptr_ + tx_active_offset_, remain);
    if (written <= 0)
    {
      break;
    }

    tx_active_offset_ += static_cast<size_t>(written);
    wrote_any = true;
  }

  if (in_isr)
  {
    portEXIT_CRITICAL_ISR(&tx_lock_);
  }
  else
  {
    portEXIT_CRITICAL(&tx_lock_);
  }

  if (wrote_any || became_idle)
  {
    tx_event_seq_.fetch_add(1, std::memory_order_acq_rel);
  }

  if (became_idle)
  {
    usb_serial_jtag_ll_txfifo_flush();
  }

  if (became_idle)
  {
    usb_serial_jtag_ll_disable_intr_mask(kTxIntrMask);
  }

  return wrote_any || became_idle;
}

ErrorCode IRAM_ATTR ESP32CDCJtag::WriteBlocking(const uint8_t* data, size_t size,
                                                bool in_isr)
{
  if ((data == nullptr) || (size == 0U))
  {
    return (size == 0U) ? ErrorCode::OK : ErrorCode::ARG_ERR;
  }

  if (in_isr)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  if (!intr_installed_)
  {
    return ErrorCode::INIT_ERR;
  }

  const size_t chunk_size = std::max<size_t>(
      64U, std::min<size_t>(kTxChunkMax, tx_slot_size_));
  if (chunk_size == 0U)
  {
    return ErrorCode::SIZE_ERR;
  }

  const uint8_t* src = data;
  size_t remain = size;
  uint32_t last_seq = tx_event_seq_.load(std::memory_order_acquire);
  uint32_t stall_retries = 0;

  while (remain > 0U)
  {
    size_t produced = 0;
    bool queued = false;
    bool need_start = false;

    portENTER_CRITICAL(&tx_lock_);
    if (!tx_busy_.load(std::memory_order_acquire))
    {
      const size_t copy_size = std::min(chunk_size, remain);
      std::memcpy(tx_slot_a_, src, copy_size);
      tx_active_ptr_ = tx_slot_a_;
      tx_active_size_ = copy_size;
      tx_active_offset_ = 0;
      tx_pending_ptr_ = nullptr;
      tx_pending_size_ = 0;
      tx_pending_valid_ = false;
      tx_source_done_.store(true, std::memory_order_release);
      tx_busy_.store(true, std::memory_order_release);
      produced = copy_size;
      queued = true;
      need_start = true;
    }
    else if (!tx_pending_valid_)
    {
      const size_t copy_size = std::min(chunk_size, remain);
      uint8_t* pending_slot = tx_slot_b_;
      if (tx_active_ptr_ == tx_slot_b_)
      {
        pending_slot = tx_slot_a_;
      }
      std::memcpy(pending_slot, src, copy_size);
      tx_pending_ptr_ = pending_slot;
      tx_pending_size_ = copy_size;
      tx_pending_valid_ = true;
      produced = copy_size;
      queued = true;
    }

    if (queued)
    {
      tx_source_done_.store(true, std::memory_order_release);
    }
    portEXIT_CRITICAL(&tx_lock_);
    if (queued)
    {
      src += produced;
      remain -= produced;
      if (need_start)
      {
        usb_serial_jtag_ll_ena_intr_mask(kTxIntrMask);
      }
      if (PumpTx(false))
      {
        last_seq = tx_event_seq_.load(std::memory_order_acquire);
        stall_retries = 0;
      }
      continue;
    }

    if (PumpTx(false))
    {
      last_seq = tx_event_seq_.load(std::memory_order_acquire);
      stall_retries = 0;
      continue;
    }

    uint32_t spin = kWriteSpinWait;
    while ((spin > 0U) && tx_busy_.load(std::memory_order_acquire))
    {
      const uint32_t seq = tx_event_seq_.load(std::memory_order_acquire);
      if (seq != last_seq)
      {
        last_seq = seq;
        stall_retries = 0;
        break;
      }
      --spin;
    }

    if (!tx_busy_.load(std::memory_order_acquire))
    {
      break;
    }

    const uint32_t seq = tx_event_seq_.load(std::memory_order_acquire);
    if (seq == last_seq)
    {
      ++stall_retries;
      if (stall_retries > kWriteStallRetries)
      {
        ResetTxState(false);
        tx_source_done_.store(true, std::memory_order_release);
        usb_serial_jtag_ll_disable_intr_mask(kTxIntrMask);
        return ErrorCode::TIMEOUT;
      }
    }
    else
    {
      last_seq = seq;
      stall_retries = 0;
    }
  }

  return ErrorCode::OK;
}

void IRAM_ATTR ESP32CDCJtag::HandleInterrupt()
{
  const uint32_t status = usb_serial_jtag_ll_get_intsts_mask();
  const uint32_t tx_status = status & kTxIntrMask;
  if (tx_status != 0U)
  {
    usb_serial_jtag_ll_clr_intsts_mask(tx_status);
    (void)PumpTx(true);
  }
}

}  // namespace LibXR

#endif
