#include "mspm0_uart.hpp"

#include "driverlib.h"
#include "libxr_def.hpp"

using namespace LibXR;

MSPM0UART *MSPM0UART::map[4] = {nullptr};

// 简单的实例映射帮助函数
// 使用显式地址比较，适配MSPM0不连续的内存映射
mspm0_uart_id_t MSPM0_UART_GetID(UART_Regs *addr)
{
  if (addr == NULL)
  {
    return mspm0_uart_id_t::MSPM0_UART_ID_ERROR;
  }

  // 根据芯片型号包含的头文件定义来判断
#ifdef UART0_BASE
  if (reinterpret_cast<uintptr_t>(addr) == UART0_BASE) {
    return mspm0_uart_id_t::MSPM0_UART0;
  }
#endif
#ifdef UART1_BASE
  if (reinterpret_cast<uintptr_t>(addr) == UART1_BASE) {
    return mspm0_uart_id_t::MSPM0_UART1;
  }
#endif
#ifdef UART2_BASE
  if (reinterpret_cast<uintptr_t>(addr) == UART2_BASE) {
    return mspm0_uart_id_t::MSPM0_UART2;
  }
#endif
#ifdef UART3_BASE
  if (reinterpret_cast<uintptr_t>(addr) == UART3_BASE) {
    return mspm0_uart_id_t::MSPM0_UART3;
  }
#endif

  return mspm0_uart_id_t::MSPM0_UART_ID_ERROR;
}

ErrorCode MSPM0UART::WriteFun(WritePort &port)
{
  MSPM0UART *uart = CONTAINER_OF(&port, MSPM0UART, _write_port);

  if (!uart->dma_buff_tx_.HasPending())
  {
    WriteInfoBlock info;
    if (port.queue_info_->Peek(info) != ErrorCode::OK)
    {
      return ErrorCode::EMPTY;
    }

    uint8_t *buffer = nullptr;
    bool use_pending = false;

    // 检查 DMA 通道是否繁忙 (MSPM0 DMA 使能位)
    bool dma_busy = DL_DMA_isChannelEnabled(uart->dma_regs_, uart->dma_ch_tx_);

    if (!dma_busy)
    {
      buffer = reinterpret_cast<uint8_t *>(uart->dma_buff_tx_.ActiveBuffer());
    }
    else
    {
      buffer = reinterpret_cast<uint8_t *>(uart->dma_buff_tx_.PendingBuffer());
      use_pending = true;
    }

    if (port.queue_data_->PopBatch(reinterpret_cast<uint8_t *>(buffer),
                                   info.data.size_) != ErrorCode::OK)
    {
      ASSERT(false);
      return ErrorCode::EMPTY;
    }

    if (use_pending)
    {
      uart->dma_buff_tx_.SetPendingLength(info.data.size_);
      uart->dma_buff_tx_.EnablePending();

      // 再次检查，如果刚才忙现在闲了，且有 Pending 数据，尝试切换
      if (!DL_DMA_isChannelEnabled(uart->dma_regs_, uart->dma_ch_tx_) &&
          uart->dma_buff_tx_.HasPending())
      {
        uart->dma_buff_tx_.Switch();
      }
      else
      {
        // DMA 仍在忙，放入 Pending 等待 ISR 处理
        return ErrorCode::FAILED;  // 返回 FAILED 表示稍后处理，逻辑与 STM32 版一致
      }
    }

    port.queue_info_->Pop(uart->write_info_active_);

    // MSPM0 没有 Cache，无需 SCB_CleanDCache

    // 配置并启动 DMA 发送
    DL_DMA_setSrcAddr(uart->dma_regs_, uart->dma_ch_tx_,
                      (uint32_t)uart->dma_buff_tx_.ActiveBuffer());
    DL_DMA_setTransferSize(uart->dma_regs_, uart->dma_ch_tx_, info.data.size_);
    DL_DMA_enableChannel(uart->dma_regs_, uart->dma_ch_tx_);

    // 成功启动
    return ErrorCode::OK;
  }

  return ErrorCode::FAILED;
}

ErrorCode MSPM0UART::ReadFun(ReadPort &port)
{
  MSPM0UART *uart = CONTAINER_OF(&port, MSPM0UART, _read_port);
  UNUSED(uart);
  return ErrorCode::EMPTY;
}

MSPM0UART::MSPM0UART(UART_Regs *uart_handle, DMA_Regs *dma_handle, uint8_t rx_dma_ch,
                     uint8_t tx_dma_ch, RawData dma_buff_rx, RawData dma_buff_tx,
                     uint32_t tx_queue_size)
    : UART(&_read_port, &_write_port),
      _read_port(dma_buff_rx.size_),
      _write_port(tx_queue_size, dma_buff_tx.size_ / 2),
      dma_buff_rx_(dma_buff_rx),
      dma_buff_tx_(dma_buff_tx),
      uart_regs_(uart_handle),
      dma_regs_(dma_handle),
      dma_ch_rx_(rx_dma_ch),
      dma_ch_tx_(tx_dma_ch),
      id_(MSPM0_UART_GetID(uart_handle))
{
  ASSERT(id_ != mspm0_uart_id_t::MSPM0_UART_ID_ERROR);

  if (id_ < 4)
  {
    map[id_] = this;
  }

  // 假设 TX DMA 已经通过 SysConfig 配置了基本参数（Trigger, Width 等）
  // 这里只需绑定回调逻辑。
  _write_port = WriteFun;

  // 初始化 RX
  if (dma_buff_rx_.addr_ != nullptr)
  {
    // 配置 RX DMA 为 Circular 模式 (如果 SysConfig 未配置，需手动配置 DMAMode)
    // DL_DMA_setMode(dma_regs_, dma_ch_rx_, DL_DMA_MODE_CIRCULAR); // 通常 SysConfig 做

    DL_DMA_setDestAddr(dma_regs_, dma_ch_rx_, (uint32_t)dma_buff_rx_.addr_);
    DL_DMA_setTransferSize(dma_regs_, dma_ch_rx_, dma_buff_rx_.size_);

    // 【新增】设置 Early Interrupt 阈值为一半缓冲区大小，用于连续大数据流处理
    DL_DMA_Full_Ch_setEarlyInterruptThreshold(dma_regs_, dma_ch_rx_,
                                              DL_DMA_EARLY_INTERRUPT_THRESHOLD_HALF);

    // 【关键】开启 RX DMA 的通道中断（包含 Early Interrupt 和完成中断）
    // MSPM0 的 DMA 中断是基于通道号的，每个通道有独立的中断位
    uint32_t dma_interrupt_mask = 0;
    switch (dma_ch_rx_)
    {
      case 0:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL0;
        break;
      case 1:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL1;
        break;
      case 2:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL2;
        break;
      case 3:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL3;
        break;
      case 4:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL4;
        break;
      case 5:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL5;
        break;
      case 6:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL6;
        break;
      case 7:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL7;
        break;
      case 8:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL8;
        break;
      case 9:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL9;
        break;
      case 10:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL10;
        break;
      case 12:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL12;
        break;
      case 13:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL13;
        break;
      case 14:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL14;
        break;
      case 15:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL15;
        break;
      default:
        ASSERT(false);
        return;
    }
    DL_DMA_enableInterrupt(dma_regs_, dma_interrupt_mask);

    DL_DMA_enableChannel(dma_regs_, dma_ch_rx_);

    // 【重要】MSPM0 硬件差异处理：
    // UART0 通常是 Extended UART，不支持 RX Timeout
    // UART1/2/3 通常是 Main UART，支持 RX Timeout
    bool supports_rx_timeout = true;

#ifdef UART0
    if (uart_regs_ == UART0)
    {
      supports_rx_timeout = false;
      // UART0 (Extended) 不支持 Timeout，只能依赖其他机制
    }
#endif

    if (supports_rx_timeout)
    {
      // [Main UART 策略] - 优化性能
      // 1. 开启 Timeout：处理不定长小数据包
      DL_UART_setRXInterruptTimeout(uart_regs_, 20);
      DL_UART_enableInterrupt(uart_regs_, DL_UART_INTERRUPT_RX_TIMEOUT_ERROR);

      // 2. 关闭 RX FIFO 中断：避免与 DMA 竞争，节省 CPU
      // FIFO 中断会导致不必要的频繁唤醒
      DL_UART_disableInterrupt(uart_regs_, DL_UART_INTERRUPT_RX);
    }
    else
    {
      // [Extended UART 策略] (UART0)
      // 只能依赖 FIFO 中断来模拟 "有数据进入"
      // 虽然效率低，但是这是在没有 Timeout 硬件下的唯一实时手段
      DL_UART_enableInterrupt(uart_regs_, DL_UART_INTERRUPT_RX);
    }

    _read_port = ReadFun;
  }
}

ErrorCode MSPM0UART::SetConfig(UART::Configuration config)
{
  // 使用轻量级 API 直接修改 LCRH 寄存器位，避免时钟配置重置
  // 这样更安全且性能更好

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
      ASSERT(false);
  }

  DL_UART_STOP_BITS stop_bits = DL_UART_STOP_BITS_ONE;
  switch (config.stop_bits)
  {
    case 1:
      stop_bits = DL_UART_STOP_BITS_ONE;
      break;
    case 2:
      stop_bits = DL_UART_STOP_BITS_TWO;
      break;
    default:
      ASSERT(false);
  }

  // 等待 UART 空闲
  while (DL_UART_isBusy(uart_regs_))
  {
    // Wait
  }

  // 使用轻量级 API 直接设置协议参数
  // 这些函数只操作 LCRH 寄存器，不会影响波特率分频器
  DL_UART_setParityMode(uart_regs_, parity);
  DL_UART_setStopBits(uart_regs_, stop_bits);
  DL_UART_setWordLength(uart_regs_, DL_UART_WORD_LENGTH_8_BITS);

  return ErrorCode::OK;
}

// ================= ISR Handlers =================

// 核心接收逻辑：模拟 STM32 的 ReceiveToIdle + DMA Circular
static inline void MSPM0_UART_Process_RX(MSPM0UART *uart)
{
  auto rx_buf = static_cast<uint8_t *>(uart->dma_buff_rx_.addr_);
  size_t dma_total_size = uart->dma_buff_rx_.size_;

  // 获取 DMA 当前剩余传输量
  size_t remaining = DL_DMA_getTransferSize(uart->dma_regs_, uart->dma_ch_rx_);
  size_t curr_pos = dma_total_size - remaining;
  size_t last_pos = uart->last_rx_pos_;

  // MSPM0 无 D-Cache，无需 InvalidateDCache

  if (curr_pos != last_pos)
  {
    if (curr_pos > last_pos)
    {
      // 线性部分
      uart->read_port_->queue_data_->PushBatch(&rx_buf[last_pos], curr_pos - last_pos);
    }
    else
    {
      // 回卷部分 (Ring Buffer Wrapped)
      uart->read_port_->queue_data_->PushBatch(&rx_buf[last_pos],
                                               dma_total_size - last_pos);
      if (curr_pos > 0)
      {
        uart->read_port_->queue_data_->PushBatch(&rx_buf[0], curr_pos);
      }
    }

    uart->last_rx_pos_ = curr_pos;
    uart->read_port_->ProcessPendingReads(true);
  }
}

// 这个需要在 UART 的中断服务函数中调用 (SysConfig 生成的 UARTx_IRQHandler)
void MSPM0_UART_ISR_Handler_RX(UART_Regs *uart_base)
{
  mspm0_uart_id_t id = MSPM0_UART_GetID(uart_base);
  if (id >= 4) return;
  auto uart = MSPM0UART::map[id];

  // 动态构建中断掩码，只查询实际使能的中断
  uint32_t interrupt_mask = DL_UART_INTERRUPT_RX;  // 基础 RX 中断

  // 检查是否支持并使能了 RX Timeout（仅 Main UART 支持）
  bool supports_rx_timeout = true;
#ifdef UART0
  if (uart_base == UART0)
  {
    supports_rx_timeout = false;
  }
#endif

  if (supports_rx_timeout)
  {
    interrupt_mask |= DL_UART_INTERRUPT_RX_TIMEOUT_ERROR;
  }

  uint32_t status = DL_UART_getEnabledInterruptStatus(uart_base, interrupt_mask);

  // 无论是 RX 超时还是 RX (FIFO阈值)，都检查 DMA 进度
  if (status)
  {
    MSPM0_UART_Process_RX(uart);
    DL_UART_clearInterruptStatus(uart_base, status);
  }
}

// 这个需要在 DMA 的中断服务函数中调用 (SysConfig 生成的 DMA_IRQHandler)
// 对应 TX 完成中断
void MSPM0_UART_ISR_Handler_TX_DMA_Done(mspm0_uart_id_t id)
{
  if (id >= 4)
  {
    return;
  }
  auto uart = MSPM0UART::map[id];

  // 1. 切换 Buffer
  size_t pending_len = uart->dma_buff_tx_.GetPendingLength();
  if (pending_len == 0)
  {
    return;
  }

  uart->dma_buff_tx_.Switch();

  // 2. 启动下一笔传输 (Active Buffer)
  // MSPM0 无 D-Cache，无需 CleanDCache

  DL_DMA_setSrcAddr(uart->dma_regs_, uart->dma_ch_tx_,
                    (uint32_t)uart->dma_buff_tx_.ActiveBuffer());
  DL_DMA_setTransferSize(uart->dma_regs_, uart->dma_ch_tx_, pending_len);
  DL_DMA_enableChannel(uart->dma_regs_, uart->dma_ch_tx_);

  // 3. 通知上层当前包已完成
  WriteInfoBlock &current_info = uart->write_info_active_;
  if (uart->write_port_->queue_info_->Pop(current_info) != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

  uart->write_port_->Finish(true, ErrorCode::OK, current_info, current_info.data.size_);

  // 4. 准备下一笔 Pending 数据 (从队列取数据到 Pending Buffer)
  WriteInfoBlock next_info;
  if (uart->write_port_->queue_info_->Peek(next_info) != ErrorCode::OK)
  {
    return;
  }

  if (uart->write_port_->queue_data_->PopBatch(
          reinterpret_cast<uint8_t *>(uart->dma_buff_tx_.PendingBuffer()),
          next_info.data.size_) != ErrorCode::OK)
  {
    ASSERT(false);
    return;
  }

  uart->dma_buff_tx_.SetPendingLength(next_info.data.size_);
  uart->dma_buff_tx_.EnablePending();
}

// RX DMA 中断处理函数（处理 Early Interrupt 和完成中断）
void MSPM0_UART_ISR_Handler_RX_DMA_Done(mspm0_uart_id_t id)
{
  if (id >= 4)
  {
    return;
  }

  auto uart = MSPM0UART::map[id];
  if (!uart)
  {
    return;
  }

  // 获取待处理的 DMA 中断事件
  DL_DMA_EVENT_IIDX dma_event = DL_DMA_getPendingInterrupt(uart->dma_regs_);

  // 检查是否是当前 UART 的 RX DMA 通道触发的中断
  bool is_our_channel = false;
  switch (uart->dma_ch_rx_)
  {
    case 0:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH0);
      break;
    case 1:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH1);
      break;
    case 2:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH2);
      break;
    case 3:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH3);
      break;
    case 4:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH4);
      break;
    case 5:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH5);
      break;
    case 6:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH6);
      break;
    case 7:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH7);
      break;
    case 8:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH8);
      break;
    case 9:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH9);
      break;
    case 10:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH10);
      break;
    case 12:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH12);
      break;
    case 13:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH13);
      break;
    case 14:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH14);
      break;
    case 15:
      is_our_channel = (dma_event == DL_DMA_EVENT_IIDX_DMACH15);
      break;
    default:
      return;
  }

  // 如果是当前通道的中断，处理数据
  if (is_our_channel)
  {
    // 无论是 Early Interrupt（半满）还是完成中断（满/回卷），都处理数据
    MSPM0_UART_Process_RX(uart);

    // 清除中断状态
    uint32_t dma_interrupt_mask = 0;
    switch (uart->dma_ch_rx_)
    {
      case 0:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL0;
        break;
      case 1:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL1;
        break;
      case 2:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL2;
        break;
      case 3:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL3;
        break;
      case 4:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL4;
        break;
      case 5:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL5;
        break;
      case 6:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL6;
        break;
      case 7:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL7;
        break;
      case 8:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL8;
        break;
      case 9:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL9;
        break;
      case 10:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL10;
        break;
      case 12:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL12;
        break;
      case 13:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL13;
        break;
      case 14:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL14;
        break;
      case 15:
        dma_interrupt_mask = DL_DMA_INTERRUPT_CHANNEL15;
        break;
      default:
        return;
    }
    DL_DMA_clearInterruptStatus(uart->dma_regs_, dma_interrupt_mask);
  }
}

//
// 【重要】外部中断向量配置说明：
//
// 需要在系统的中断向量文件（如 ti_msp_dl_config.c 或类似文件）中，
// 针对 RX DMA 通道添加以下处理逻辑：
//
// void DMA_IRQHandler(void) {
//     // 检查 TX 通道
//     if (DL_DMA_getPendingInterrupt(DMA, DL_DMA_INTERRUPT_DMA_DONE_TX_CH)) {
//         LibXR::MSPM0_UART_ISR_Handler_TX_DMA_Done(uart_id);
//     }
//
//     // 【关键新增】检查 RX 通道 (处理半满、满/回卷中断)
//     if (DL_DMA_getPendingInterrupt(DMA, DL_DMA_INTERRUPT_CHANNEL_RX_CH)) {
//         LibXR::MSPM0_UART_ISR_Handler_RX_DMA_Done(uart_id);
//     }
// }
//
// 同时需要在 SysConfig 中为 RX DMA 通道启用中断，
// 或者手动调用：DL_DMA_enableInterrupt(dma_regs_, dma_ch_rx_, interrupt_mask);
//

// ==================== 内置 ISR 入口 ====================

// MSPM0 的 UART 中断是每个实例独立的，使用宏生成 ISR 入口
extern "C"
{
#ifdef UART0
  void UART0_IRQHandler(void) { LibXR::MSPM0_UART_ISR_Handler_RX(UART0); }
#endif

#ifdef UART1
  void UART1_IRQHandler(void) { LibXR::MSPM0_UART_ISR_Handler_RX(UART1); }
#endif

#ifdef UART2
  void UART2_IRQHandler(void) { LibXR::MSPM0_UART_ISR_Handler_RX(UART2); }
#endif

#ifdef UART3
  void UART3_IRQHandler(void) { LibXR::MSPM0_UART_ISR_Handler_RX(UART3); }
#endif
}

// ==================== DMA 中断服务程序 ====================
//
// MSPM0 的 DMA 中断通常是共享的，所有 DMA 通道共享一个 DMA_IRQHandler
// 这一点与 CH32 不同，CH32 的 DMA 通道中断是独立的。
//
// 需要在系统中实现类似如下的分发逻辑：
//
// extern "C" {
// void DMA_IRQHandler(void)
// {
//   // 获取中断事件
//   DL_DMA_EVENT_IIDX dma_event = DL_DMA_getPendingInterrupt(DMA);
//
//   // 根据事件分发到对应的 UART 实例
//   switch (dma_event) {
//     case DL_DMA_EVENT_IIDX_DMACH0:
//       // 假设通道 0 是 UART0 的 RX DMA
//       LibXR::MSPM0_UART_ISR_Handler_RX_DMA_Done(LibXR::MSPM0_UART0);
//       break;
//
//     case DL_DMA_EVENT_IIDX_DMACH1:
//       // 假设通道 1 是 UART0 的 TX DMA
//       LibXR::MSPM0_UART_ISR_Handler_TX_DMA_Done(LibXR::MSPM0_UART0);
//       break;
//
//     case DL_DMA_EVENT_IIDX_DMACH2:
//       // 假设通道 2 是 UART1 的 RX DMA
//       LibXR::MSPM0_UART_ISR_Handler_RX_DMA_Done(LibXR::MSPM0_UART1);
//       break;
//
//     // ... 其他通道映射
//
//     default:
//       // 处理错误中断或其他用途的 DMA 通道
//       break;
//   }
// }
// }
//
// 注意：具体的 DMA 通道到 UART 实例的映射需要根据实际的硬件配置来确定。
//