#include "cdc_uart.hpp"

#include <cstring>

namespace LibXR::USB
{

void CDCUart::BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num,
                            bool in_isr)
{
  CDCBase::BindEndpoints(endpoint_pool, start_itf_num, in_isr);
  RequestTx(in_isr);
}

void CDCUart::WriteFun(WritePort& port, bool in_isr)
{
  auto* cdc = LibXR::ContainerOf(&port, &CDCUart::write_port_cdc_);
  cdc->RequestTx(in_isr);
}

void CDCUart::OnDataInComplete(bool in_isr, ConstRawData& data)
{
  UNUSED(data);
  RequestTx(in_isr);
}

void CDCUart::RequestTx(bool in_isr)
{
  tx_service_.Invoke(1U, in_isr,
                     [this](uint32_t, bool owner_in_isr) { ServiceTx(owner_in_isr); });
}

void CDCUart::ServiceTx(bool in_isr)
{
  auto* ep = GetDataInEndpoint();
  if (ep == nullptr || !Inited() || ep->GetState() != Endpoint::State::IDLE)
  {
    return;
  }

  if (need_write_zlp_)
  {
    if (ep->GetActiveLength() == 0U && write_port_cdc_.Size() == 0U)
    {
      [[maybe_unused]] const auto ans = ep->TransferZLP();
      DEV_ASSERT_FROM_CALLBACK(ans == ErrorCode::OK, in_isr);
      need_write_zlp_ = false;
      return;
    }
    need_write_zlp_ = false;
  }
  ProgressTx(in_isr);
}

void CDCUart::StartTxBuffer(Endpoint& ep, [[maybe_unused]] bool in_isr)
{
  const size_t LENGTH = ep.GetActiveLength();
  DEV_ASSERT_FROM_CALLBACK(LENGTH != 0U, in_isr);
  ep.SetActiveLength(0U);
  [[maybe_unused]] const auto ans = ep.Transfer(LENGTH);
  REQUIRE_FROM_CALLBACK(ans == ErrorCode::OK, in_isr);
  const size_t MPS = ep.MaxPacketSize();
  need_write_zlp_ = MPS != 0U && LENGTH % MPS == 0U;
}

void CDCUart::FillTxBuffer(Endpoint& ep, bool in_isr)
{
  auto queue = write_port_cdc_.GetWriteQueue(in_isr);
  if (queue.Empty())
  {
    return;
  }

  const auto buffer = ep.GetBuffer();
  ASSERT(buffer.addr_ != nullptr && buffer.size_ != 0U);
  const size_t LENGTH =
      queue.PopWithWriter(buffer.size_,
                          [buffer](const uint8_t* first, size_t first_size,
                                   const uint8_t* second, size_t second_size) -> size_t
                          {
                            auto* dst = static_cast<uint8_t*>(buffer.addr_);
                            if (first_size != 0U)
                            {
                              std::memcpy(dst, first, first_size);
                            }
                            if (second_size != 0U)
                            {
                              std::memcpy(dst + first_size, second, second_size);
                            }
                            return first_size + second_size;
                          });
  DEV_ASSERT_FROM_CALLBACK(LENGTH != 0U && LENGTH <= UINT16_MAX, in_isr);
  ep.SetActiveLength(static_cast<uint16_t>(LENGTH));
  need_write_zlp_ = false;
  if (ep.GetState() == Endpoint::State::IDLE)
  {
    StartTxBuffer(ep, in_isr);
  }
}

void CDCUart::ProgressTx(bool in_isr)
{
  auto* ep = GetDataInEndpoint();
  if (ep == nullptr || !Inited() || ep->GetState() != Endpoint::State::IDLE)
  {
    return;
  }

  if (ep->GetActiveLength() != 0U)
  {
    StartTxBuffer(*ep, in_isr);
  }
  else
  {
    FillTxBuffer(*ep, in_isr);
  }

  if (ep->GetState() == Endpoint::State::BUSY && ep->UseDoubleBuffer() &&
      ep->GetActiveLength() == 0U)
  {
    FillTxBuffer(*ep, in_isr);
  }
}

}  // namespace LibXR::USB
