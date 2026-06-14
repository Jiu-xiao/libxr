#include "esp_cdc_jtag.hpp"

#if SOC_USB_SERIAL_JTAG_SUPPORTED &&                                      \
    ((defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) || \
     (defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6))

#include <algorithm>
#include <cstring>
#include <new>

#include "esp_attr.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/interrupts.h"

namespace
{
// USB Serial/JTAG TX interrupt source.
// USB Serial/JTAG TX 中断源。
constexpr uint32_t TX_INTR_MASK = USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;

// USB Serial/JTAG RX interrupt source.
// USB Serial/JTAG RX 中断源。
constexpr uint32_t RX_INTR_MASK = USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT;

// Full interrupt set used by this backend.
// 该后端使用的完整中断集合。
constexpr uint32_t ALL_INTR_MASK = TX_INTR_MASK | RX_INTR_MASK;
}  // namespace

namespace LibXR
{

// Construct queue bridges first, then bind the TX helper storage before
// touching the hardware interrupt source.
// 先构造队列桥，再绑定 TX helper storage，最后再触碰硬件中断源。
ESP32CDCJtag::ESP32CDCJtag(size_t rx_buffer_size, size_t tx_buffer_size,
                           uint32_t tx_queue_size, UART::Configuration config)
    : UART(&_read_port, &_write_port),
      config_(config),
      tx_slot_storage_(new (std::nothrow) uint8_t[tx_buffer_size * 2U]),
      _read_port(rx_buffer_size),
      _write_port(tx_queue_size, tx_buffer_size)
{
  ASSERT(tx_slot_storage_ != nullptr);
  ASSERT(tx_buffer_size > 0U);

  tx_double_buffer_.Init({tx_slot_storage_, tx_buffer_size * 2U});

  _read_port = ReadFun;
  _write_port = WriteFun;

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

// USB Serial/JTAG only supports the fixed 8N1 framing model exposed here.
// USB Serial/JTAG 这里只支持固定的 8N1 帧格式模型。
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

// Hardware bring-up installs the IRAM interrupt and leaves RX reception armed
// at all times. TX is enabled on demand by the TX state machine.
// 硬件初始化会安装 IRAM 中断，并让 RX 始终处于使能状态；TX 由发送状态机按需开启。
ErrorCode ESP32CDCJtag::InitHardware()
{
  if (hw_inited_)
  {
    return ErrorCode::OK;
  }

  ResetTxState(false);

  const esp_err_t intr_ans = esp_intr_alloc(
      ETS_USB_SERIAL_JTAG_INTR_SOURCE, ESP_INTR_FLAG_IRAM, IsrEntry, this, &intr_handle_);
  intr_installed_ = (intr_ans == ESP_OK);
  if (!intr_installed_)
  {
    intr_handle_ = nullptr;
    return ErrorCode::INIT_ERR;
  }

  usb_serial_jtag_ll_clr_intsts_mask(ALL_INTR_MASK);
  usb_serial_jtag_ll_ena_intr_mask(RX_INTR_MASK);

  hw_inited_ = true;
  return ErrorCode::OK;
}

// ISR entry only forwards control into the owning backend object.
// ISR 入口只负责把控制转发给所属后端对象。
void IRAM_ATTR ESP32CDCJtag::IsrEntry(void* arg)
{
  auto* cdc = static_cast<ESP32CDCJtag*>(arg);
  if (cdc != nullptr)
  {
    cdc->HandleInterrupt();
  }
}

// `WritePort` only needs a trampoline back into the backend instance.
// `WritePort` 只需要一个回跳到后端实例的跳板。
ErrorCode IRAM_ATTR ESP32CDCJtag::WriteFun(WritePort& port, bool in_isr)
{
  auto* cdc = LibXR::ContainerOf(&port, &ESP32CDCJtag::_write_port);
  return cdc->TryStartTx(in_isr);
}

// RX is interrupt-driven, so `ReadPort` does not actively kick hardware.
// RX 由中断驱动，因此 `ReadPort` 不主动启动硬件路径。
ErrorCode ESP32CDCJtag::ReadFun(ReadPort&, bool) { return ErrorCode::PENDING; }

// Active TX state is fully delegated to the TX helper.
// Active TX 状态完全委托给 TX helper。
void IRAM_ATTR ESP32CDCJtag::ClearActiveTx()
{
  tx_double_buffer_.ClearActive();
}

// Pending TX state is fully delegated to the TX helper.
// Pending TX 状态完全委托给 TX helper。
void IRAM_ATTR ESP32CDCJtag::ClearPendingTx()
{
  tx_double_buffer_.ClearPending();
}

// Reset clears both TX helper slots and the hardware-busy flag together.
// Reset 同时清空 TX helper 的两级槽位和硬件忙标志。
void IRAM_ATTR ESP32CDCJtag::ResetTxState(bool)
{
  tx_double_buffer_.Reset();
  tx_busy_.Clear();
}

// Queue-data and queue-info are still decoupled here, so payload bytes and
// request metadata are popped in two coordinated steps.
// 这里的 `queue_data_` 与 `queue_info_` 仍然解耦，因此 payload 字节与请求
// 元数据通过两步协同出队。
bool IRAM_ATTR ESP32CDCJtag::DequeueTxToSlot(uint8_t* slot, size_t& size,
                                             WriteInfoBlock& info, bool in_isr)
{
  (void)in_isr;

  WriteInfoBlock peek_info = {};
  if (write_port_->queue_info_->Peek(peek_info) != ErrorCode::OK)
  {
    return false;
  }

  if (peek_info.data.size_ > tx_double_buffer_.BufferSize())
  {
    ASSERT(false);
    return false;
  }

  if (write_port_->queue_data_->PopBatch(slot, peek_info.data.size_) != ErrorCode::OK)
  {
    return false;
  }

  if (write_port_->queue_info_->Pop(info) != ErrorCode::OK)
  {
    return false;
  }

  size = peek_info.data.size_;
  return true;
}

// Active TX load uses the helper's active slot as the payload destination.
// Active TX 装载使用 helper 的 active slot 作为 payload 目标。
bool IRAM_ATTR ESP32CDCJtag::LoadActiveTxFromQueue(bool in_isr)
{
  if (tx_double_buffer_.HasActive())
  {
    return true;
  }

  size_t active_size = 0U;
  WriteInfoBlock active_info = {};
  if (!DequeueTxToSlot(tx_double_buffer_.ActiveBuffer(), active_size, active_info,
                       in_isr))
  {
    return false;
  }

  tx_double_buffer_.LoadActive(active_size, active_info);
  return true;
}

// Pending TX preload uses the helper's pending slot as the payload destination.
// Pending TX 预装使用 helper 的 pending slot 作为 payload 目标。
bool IRAM_ATTR ESP32CDCJtag::LoadPendingTxFromQueue(bool in_isr)
{
  if (tx_double_buffer_.HasPending())
  {
    return false;
  }

  size_t pending_size = 0U;
  WriteInfoBlock pending_info = {};
  if (!DequeueTxToSlot(tx_double_buffer_.PendingBuffer(), pending_size, pending_info,
                       in_isr))
  {
    return false;
  }

  tx_double_buffer_.LoadPending(pending_size, pending_info);
  return true;
}

// Pending TX can only be promoted when hardware is idle and no active request
// is still owned by the backend.
// 只有当硬件空闲且后端不再持有 active 请求时，pending TX 才能被提升。
bool IRAM_ATTR ESP32CDCJtag::StartPendingTxIfIdle(bool in_isr)
{
  if (tx_busy_.IsSet() || tx_double_buffer_.HasActive() ||
      !tx_double_buffer_.HasPending())
  {
    return false;
  }

  if (!tx_double_buffer_.PromotePending())
  {
    return false;
  }

  return StartAndReportActive(in_isr);
}

// `PumpTx()` keeps feeding the USB Serial/JTAG TX FIFO until it either fills
// up again or the active payload is completely consumed.
// `PumpTx()` 会持续向 USB Serial/JTAG TX FIFO 补料，直到 FIFO 再次写满
// 或 active payload 被完全消费。
bool IRAM_ATTR ESP32CDCJtag::PumpTx(bool)
{
  while (tx_busy_.IsSet())
  {
    if (!tx_double_buffer_.HasActive() || (tx_double_buffer_.ActiveBuffer() == nullptr) ||
        (tx_double_buffer_.ActiveOffset() >= tx_double_buffer_.ActiveLength()))
    {
      tx_busy_.Clear();
      return true;
    }

    const size_t active_offset = tx_double_buffer_.ActiveOffset();
    const uint32_t remain =
        static_cast<uint32_t>(tx_double_buffer_.ActiveLength() - active_offset);
    const int written = usb_serial_jtag_ll_write_txfifo(
        tx_double_buffer_.ActiveBuffer() + active_offset, remain);
    if (written <= 0)
    {
      return false;
    }

    tx_double_buffer_.SetActiveOffset(active_offset + static_cast<size_t>(written));
    if (tx_double_buffer_.ActiveOffset() >= tx_double_buffer_.ActiveLength())
    {
      tx_busy_.Clear();
      return true;
    }
  }

  return false;
}

// RX bytes are pushed opportunistically until the software queue becomes full.
// RX 字节会尽量推进软件队列，直到软件队列装满为止。
void IRAM_ATTR ESP32CDCJtag::PushRxBytes(const uint8_t* data, size_t size, bool in_isr)
{
  size_t offset = 0U;
  bool pushed_any = false;

  while (offset < size)
  {
    const size_t free_space = read_port_->queue_data_->EmptySize();
    if (free_space == 0U)
    {
      break;
    }

    const size_t chunk = std::min(free_space, size - offset);
    if (read_port_->queue_data_->PushBatch(data + offset, chunk) != ErrorCode::OK)
    {
      break;
    }

    offset += chunk;
    pushed_any = true;
  }

  if (pushed_any)
  {
    read_port_->ProcessPendingReads(in_isr);
  }
}

// Starting active TX arms the TX interrupt and immediately tries one local
// pump so very short payloads can complete in the same context.
// 启动 active TX 时会先使能 TX 中断，并立即做一次本地补料，这样很短的
// payload 可以在同一上下文内直接完成。
bool IRAM_ATTR ESP32CDCJtag::StartActiveTransfer(bool in_isr)
{
  if (!tx_double_buffer_.HasActive() || (tx_double_buffer_.ActiveBuffer() == nullptr) ||
      (tx_double_buffer_.ActiveLength() == 0U))
  {
    return false;
  }

  if (tx_busy_.TestAndSet())
  {
    return true;
  }

  tx_double_buffer_.SetActiveOffset(0U);
  usb_serial_jtag_ll_ena_intr_mask(TX_INTR_MASK);
  (void)PumpTx(in_isr);
  return true;
}

// Once the next TX request is kicked to hardware, queue ownership is reported
// as transferred just like the STM/CH transmit backends.
// 一旦下一条 TX 请求被踢进硬件，就像 STM/CH 发送后端一样，把队列所有权
// 交接视为完成。
bool IRAM_ATTR ESP32CDCJtag::StartAndReportActive(bool in_isr)
{
  if (!StartActiveTransfer(in_isr))
  {
    write_port_->Finish(in_isr, ErrorCode::FAILED, tx_double_buffer_.ActiveInfo());
    ClearActiveTx();
    return false;
  }

  // Keep aligned with STM/CH: once next op is kicked to HW, report it finished.
  write_port_->Finish(in_isr, ErrorCode::OK, tx_double_buffer_.ActiveInfo());
  if (!tx_busy_.IsSet() && tx_double_buffer_.HasActive())
  {
    OnTxTransferDone(in_isr, ErrorCode::OK);
  }
  return true;
}

// Stopping TX flushes the hardware FIFO and disables further TX interrupts.
// 停止 TX 会清空硬件 FIFO，并关闭后续 TX 中断。
void IRAM_ATTR ESP32CDCJtag::StopTxTransfer()
{
  usb_serial_jtag_ll_txfifo_flush();
  usb_serial_jtag_ll_disable_intr_mask(TX_INTR_MASK);
}

// TX completion is handled as a two-stage model: finish active first, then try
// to promote or preload the next pending request.
// TX 完成采用两阶段模型：先收尾 active，再尝试提升或预装下一条 pending 请求。
void IRAM_ATTR ESP32CDCJtag::OnTxTransferDone(bool in_isr, ErrorCode result)
{
  Flag::ScopedRestore tx_flag(in_tx_isr_);
  tx_busy_.Clear();

  ClearActiveTx();

  if ((result != ErrorCode::OK) && tx_double_buffer_.HasPending())
  {
    write_port_->Finish(in_isr, ErrorCode::FAILED, tx_double_buffer_.PendingInfo());
    ClearPendingTx();
  }

  if (result != ErrorCode::OK)
  {
    StopTxTransfer();
    return;
  }

  if (StartPendingTxIfIdle(in_isr))
  {
    if (!tx_double_buffer_.HasPending())
    {
      (void)LoadPendingTxFromQueue(in_isr);
      (void)StartPendingTxIfIdle(in_isr);
    }
  }

  if (!tx_busy_.IsSet() && !tx_double_buffer_.HasActive() &&
      !tx_double_buffer_.HasPending())
  {
    StopTxTransfer();
  }
}

// TX start follows the same active/pending model used by the helper-backed CDC
// path: try promote pending first, otherwise load active, then optionally
// preload pending.
// TX 启动遵循 helper 驱动的 active/pending 模型：先尝试提升 pending，否则
// 装载 active，最后再按需预装 pending。
ErrorCode IRAM_ATTR ESP32CDCJtag::TryStartTx(bool in_isr)
{
  if (in_tx_isr_.IsSet())
  {
    return ErrorCode::PENDING;
  }

  if (StartPendingTxIfIdle(in_isr))
  {
    return ErrorCode::PENDING;
  }

  if (!tx_double_buffer_.HasActive())
  {
    (void)LoadActiveTxFromQueue(in_isr);
  }

  if (!tx_busy_.IsSet() && tx_double_buffer_.HasActive())
  {
    if (!StartActiveTransfer(in_isr))
    {
      ClearActiveTx();
      return ErrorCode::FAILED;
    }

    if (!tx_busy_.IsSet() && tx_double_buffer_.HasActive())
    {
      OnTxTransferDone(in_isr, ErrorCode::OK);
    }

    return ErrorCode::OK;
  }

  if (!tx_double_buffer_.HasPending())
  {
    (void)LoadPendingTxFromQueue(in_isr);
    (void)StartPendingTxIfIdle(in_isr);
  }

  return ErrorCode::PENDING;
}

// Interrupt dispatch is split into RX packet handling and TX empty handling.
// RX packets are drained immediately; TX empty events continue the helper-based
// transmit state machine.
// 中断分发拆成 RX packet 处理和 TX empty 处理：RX packet 立即排空；TX empty
// 事件则继续 helper 驱动的发送状态机。
void IRAM_ATTR ESP32CDCJtag::HandleInterrupt()
{
  const uint32_t status = usb_serial_jtag_ll_get_intsts_mask();

  const uint32_t rx_status = status & RX_INTR_MASK;
  if (rx_status != 0U)
  {
    usb_serial_jtag_ll_clr_intsts_mask(rx_status);

    uint8_t rx_tmp[64] = {};
    const int got = usb_serial_jtag_ll_read_rxfifo(rx_tmp, sizeof(rx_tmp));
    if (got > 0)
    {
      PushRxBytes(rx_tmp, static_cast<size_t>(got), true);
    }
  }

  const uint32_t tx_status = status & TX_INTR_MASK;
  if (tx_status == 0U)
  {
    return;
  }

  usb_serial_jtag_ll_clr_intsts_mask(tx_status);

  Flag::ScopedRestore tx_flag(in_tx_isr_);
  const bool was_busy = tx_busy_.IsSet();
  (void)PumpTx(true);
  if (was_busy && !tx_busy_.IsSet())
  {
    OnTxTransferDone(true, ErrorCode::OK);
  }
}

}  // namespace LibXR

#endif
