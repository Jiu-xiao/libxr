#pragma once
#include <cstdint>
#include <cstring>

#include "cdc_base.hpp"
#include "cdc_uart.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "uart.hpp"

namespace LibXR::USB
{

class CDCToUart : public CDCUart
{
 public:
  CDCToUart(LibXR::UART& uart, size_t rx_buffer_size = 128, size_t tx_buffer_size = 128,
            size_t tx_queue_size = 5,
            Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
            Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
            Endpoint::EPNumber comm_ep_num = Endpoint::EPNumber::EP_AUTO)
      : CDCUart(rx_buffer_size, tx_buffer_size, tx_queue_size, data_in_ep_num,
                data_out_ep_num, comm_ep_num),
        rx_buffer_(new uint8_t[rx_buffer_size]),
        tx_buffer_(new uint8_t[tx_buffer_size]),
        uart_(uart)
  {
    ASSERT(uart_.write_port_->queue_data_->MaxSize() >= rx_buffer_size);

    auto cb_read_cdc = Callback<ErrorCode>::Create(
        [](bool in_isr, CDCToUart* cdc_to_uart, ErrorCode)
        {
          auto size =
              LibXR::min(cdc_to_uart->read_port_->Size(), cdc_to_uart->rx_buffer_.size_);

          static ReadOperation op_read_cdc_noblock;
          auto ans = cdc_to_uart->Read({cdc_to_uart->rx_buffer_.addr_, size},
                                       op_read_cdc_noblock);
          ASSERT(ans == ErrorCode::OK);
          cdc_to_uart->uart_.Write({cdc_to_uart->rx_buffer_.addr_, size},
                                   cdc_to_uart->op_write_uart_, in_isr);
        },
        this);

    op_read_cdc_ = ReadOperation(cb_read_cdc);

    auto cb_uart_write = Callback<ErrorCode>::Create(
        [](bool in_isr, CDCToUart* cdc_to_uart, ErrorCode)
        { cdc_to_uart->Read({nullptr, 0}, cdc_to_uart->op_read_cdc_, in_isr); }, this);

    op_write_uart_ = WriteOperation(cb_uart_write);

    auto cb_uart_read = Callback<ErrorCode>::Create(
        [](bool in_isr, CDCToUart* cdc_to_uart, ErrorCode)
        {
          auto size = LibXR::min(cdc_to_uart->uart_.read_port_->Size(),
                                 cdc_to_uart->tx_buffer_.size_);

          static ReadOperation op_read_uart_noblock;
          auto ans = cdc_to_uart->uart_.Read({cdc_to_uart->tx_buffer_.addr_, size},
                                             op_read_uart_noblock, in_isr);
          ASSERT(ans == ErrorCode::OK);
          cdc_to_uart->Write({cdc_to_uart->tx_buffer_.addr_, size},
                             cdc_to_uart->op_write_cdc_, in_isr);
        },
        this);

    op_read_uart_ = ReadOperation(cb_uart_read);

    auto cb_cdc_write = Callback<ErrorCode>::Create(
        [](bool in_isr, CDCToUart* cdc_to_uart, ErrorCode)
        { cdc_to_uart->uart_.Read({nullptr, 0}, cdc_to_uart->op_read_uart_, in_isr); },
        this);

    op_write_cdc_ = WriteOperation(cb_cdc_write);

    this->Read({nullptr, 0}, op_read_cdc_, false);

    uart_.Read({nullptr, 0}, op_read_uart_, false);
  }

  RawData rx_buffer_, tx_buffer_;

  LibXR::WriteOperation op_write_cdc_, op_write_uart_;
  LibXR::ReadOperation op_read_cdc_, op_read_uart_;

  LibXR::UART& uart_;
};
}  // namespace LibXR::USB
