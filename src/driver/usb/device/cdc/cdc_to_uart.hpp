#pragma once

#include "cdc_uart.hpp"
#include "libxr_cb.hpp"
#include "libxr_rw.hpp"
#include "uart.hpp"

namespace LibXR::USB
{

/**
 * @brief CDC 与 UART 双向桥接器（CDC<->UART） /
 *        Bidirectional bridge between USB CDC and UART
 *
 * @details 数据流 / Data flow:
 *          - CDC RX -> UART TX：CDC 收到数据后写入 UART
 *            CDC RX data is forwarded to UART TX.
 *          - UART RX -> CDC TX：UART 收到数据后写入 CDC
 *            UART RX data is forwarded to CDC TX.
 *
 *          触发方式 / Triggering model:
 *          - 通过回调链在每次写完成后启动下一次读，实现持续搬运
 *            Callbacks chain the next read after each write completes for continuous
 * pumping.
 */
class CDCToUart : public CDCUart
{
 public:
  /**
   * @brief 构造函数 / Constructor
   *
   * @param uart 被桥接的 UART 实例 / UART instance to bridge with CDC
   * @param rx_buffer_size CDC->UART 方向临时缓存大小 / Temp buffer size for CDC->UART
   * @param tx_buffer_size UART->CDC 方向临时缓存大小 / Temp buffer size for UART->CDC
   * @param tx_queue_size CDC TX 队列深度 / CDC TX queue depth
   * @param data_in_ep_num CDC Data IN 端点号 / CDC Data IN endpoint number
   * @param data_out_ep_num CDC Data OUT 端点号 / CDC Data OUT endpoint number
   * @param comm_ep_num CDC Comm 端点号 / CDC Comm endpoint number
   *
   * @note 本构造函数包含动态内存分配 / This constructor performs dynamic memory
   * allocation
   */
  CDCToUart(LibXR::UART& uart, size_t rx_buffer_size = 128, size_t tx_buffer_size = 128,
            size_t tx_queue_size = 5,
            Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
            Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
            Endpoint::EPNumber comm_ep_num = Endpoint::EPNumber::EP_AUTO)
      : CDCUart(rx_buffer_size, tx_buffer_size, tx_queue_size, data_in_ep_num,
                data_out_ep_num, comm_ep_num),
        rx_buffer_(new uint8_t[rx_buffer_size], rx_buffer_size),
        tx_buffer_(new uint8_t[tx_buffer_size], tx_buffer_size),
        uart_(uart)
  {
    // CDC->UART：CDC RX 缓冲必须能写入 UART 的 data 队列
    // CDC->UART: UART TX data queue must accept at least rx_buffer_size bytes
    ASSERT(uart_.write_port_->queue_data_->MaxSize() >= rx_buffer_size);

    // 1) CDC 读完成回调：从 CDC 读一段数据并写入 UART
    // CDC read callback: read from CDC and write into UART
    cb_read_cdc_ = Callback<ErrorCode>::Create(
        [](bool in_isr, CDCToUart* cdc_to_uart, ErrorCode)
        {
          auto size =
              LibXR::min(cdc_to_uart->read_port_->Size(), cdc_to_uart->rx_buffer_.size_);

          static ReadOperation op_read_cdc_noblock;
          auto ans = cdc_to_uart->Read({cdc_to_uart->rx_buffer_.addr_, size},
                                       op_read_cdc_noblock, in_isr);
          ASSERT(ans == ErrorCode::OK);

          ans = cdc_to_uart->uart_.Write({cdc_to_uart->rx_buffer_.addr_, size},
                                         cdc_to_uart->op_write_uart_, in_isr);
          ASSERT(ans == ErrorCode::OK);
        },
        this);

    op_read_cdc_ = ReadOperation(cb_read_cdc_);

    // 2) UART 写完成回调：继续触发下一次 CDC 读
    // UART write complete callback: trigger the next CDC read
    cb_uart_write_ = Callback<ErrorCode>::Create(
        [](bool in_isr, CDCToUart* cdc_to_uart, ErrorCode)
        {
          auto ans = cdc_to_uart->Read({nullptr, 0}, cdc_to_uart->op_read_cdc_, in_isr);
          ASSERT(ans == ErrorCode::OK);
        },
        this);

    op_write_uart_ = WriteOperation(cb_uart_write_);

    // 3) UART 读完成回调：从 UART 读一段数据并写入 CDC
    // UART read callback: read from UART and write into CDC
    cb_uart_read_ = Callback<ErrorCode>::Create(
        [](bool in_isr, CDCToUart* cdc_to_uart, ErrorCode)
        {
          auto size = LibXR::min(cdc_to_uart->uart_.read_port_->Size(),
                                 cdc_to_uart->tx_buffer_.size_);
          static ReadOperation op_read_uart_noblock;

          auto ans = cdc_to_uart->uart_.Read({cdc_to_uart->tx_buffer_.addr_, size},
                                             op_read_uart_noblock, in_isr);
          ASSERT(ans == ErrorCode::OK);

          ans = cdc_to_uart->Write({cdc_to_uart->tx_buffer_.addr_, size},
                                   cdc_to_uart->op_write_cdc_, in_isr);
          ASSERT(ans == ErrorCode::OK);
        },
        this);

    op_read_uart_ = ReadOperation(cb_uart_read_);

    // 4) CDC 写完成回调：继续触发下一次 UART 读
    // CDC write complete callback: trigger the next UART read
    cb_cdc_write_ = Callback<ErrorCode>::Create(
        [](bool in_isr, CDCToUart* cdc_to_uart, ErrorCode)
        {
          auto ans_uart_read =
              cdc_to_uart->uart_.Read({nullptr, 0}, cdc_to_uart->op_read_uart_, in_isr);
          ASSERT(ans_uart_read == ErrorCode::OK);
        },
        this);

    op_write_cdc_ = WriteOperation(cb_cdc_write_);

    // 5) LineCoding 回调：USB CDC 侧配置变化同步到 UART
    // LineCoding callback: forward CDC line coding changes to UART configuration
    set_line_coding_cb_ = LibXR::Callback<LibXR::UART::Configuration>::Create(
        [](bool, CDCToUart* self, LibXR::UART::Configuration cfg)
        { self->uart_.SetConfig(cfg); }, this);

    SetOnSetLineCodingCallback(set_line_coding_cb_);

    // 启动搬运：挂起一次 CDC 读与 UART 读，以进入回调链
    // Kick the pump: schedule one CDC read and one UART read to enter the callback chain
    this->Read({nullptr, 0}, op_read_cdc_, false);
    uart_.Read({nullptr, 0}, op_read_uart_, false);
  }

  RawData rx_buffer_;  ///< CDC->UART 临时缓存 / Temp buffer for CDC->UART
  RawData tx_buffer_;  ///< UART->CDC 临时缓存 / Temp buffer for UART->CDC

  LibXR::Callback<ErrorCode> cb_read_cdc_;  ///< CDC 读完成回调 / CDC read callback
  LibXR::Callback<ErrorCode>
      cb_uart_write_;  ///< UART 写完成回调 / UART write-complete callback
  LibXR::Callback<ErrorCode> cb_uart_read_;  ///< UART 读完成回调 / UART read callback
  LibXR::Callback<ErrorCode>
      cb_cdc_write_;  ///< CDC 写完成回调 / CDC write-complete callback

  LibXR::Callback<LibXR::UART::Configuration>
      set_line_coding_cb_;  ///< LineCoding 同步回调 / Line coding sync callback

  LibXR::WriteOperation op_write_cdc_;   ///< CDC 写操作句柄 / CDC write operation
  LibXR::WriteOperation op_write_uart_;  ///< UART 写操作句柄 / UART write operation
  LibXR::ReadOperation op_read_cdc_;     ///< CDC 读操作句柄 / CDC read operation
  LibXR::ReadOperation op_read_uart_;    ///< UART 读操作句柄 / UART read operation

  LibXR::UART& uart_;  ///< 被桥接的 UART 引用 / Bridged UART reference
};

}  // namespace LibXR::USB
