#pragma once
#include <cstring>

#include "cdc_base.hpp"

namespace LibXR::USB
{

/**
 * @brief USB CDC ACM 写测试类
 *        USB CDC ACM write test class
 *
 * 用于测试设备向主机的数据发送通道。
 * - 忽略主机发来的 OUT 数据；
 * - 当 DTR 已置位时，不断通过 IN 端点回传数据，模拟持续写出场景。
 *
 * Used for testing the device-to-host (TX) path.
 * - Ignores host-to-device OUT data.
 * - If DTR is asserted, continuously transmits data via the IN endpoint.
 */
class CDCWriteTest : public CDCBase
{
 public:
  CDCWriteTest(Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
               Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
               Endpoint::EPNumber comm_ep_num = Endpoint::EPNumber::EP_AUTO)
      : CDCBase(data_in_ep_num, data_out_ep_num, comm_ep_num)
  {
  }

  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num,
                     bool in_isr) override
  {
    CDCBase::BindEndpoints(endpoint_pool, start_itf_num, in_isr);
  }

  /**
   * @brief OUT 端点完成回调（写测试无实际消费）
   *        Data OUT complete callback (no-op for write test)
   *
   * 写测试场景下不关心主机->设备数据；若 DTR 置位，则继续安排下一次 IN 传输。
   * In write test we ignore host-to-device payload; if DTR is asserted, arm next IN
   * transfer.
   */
  void OnDataOutComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(in_isr);
    UNUSED(data);
    if (IsDtrSet())
    {
      auto ep_data_in = GetDataInEndpoint();
      ep_data_in->Transfer(ep_data_in->MaxTransferSize());
    }
  }

  /**
   * @brief IN 端点完成回调（写测试：再次触发发送）
   *        Data IN complete callback (write test: trigger next send)
   *
   * 当 DTR 已置位时，直接安排下一次 IN 传输以维持持续发送。
   * If DTR is asserted, immediately arm another IN transfer for continuous sending.
   */
  void OnDataInComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(in_isr);
    UNUSED(data);

    if (IsDtrSet())
    {
      auto ep_data_in = GetDataInEndpoint();
      ep_data_in->Transfer(ep_data_in->MaxTransferSize());
    }
  }
};

/**
 * @brief USB CDC ACM 读测试类
 *        USB CDC ACM read test class
 *
 * 用于测试主机向设备的数据接收通道。
 * - 初始化时即预装 OUT 端点接收；
 * - 每次接收完成后立即重新启动，以实现持续读入。
 *
 * Used for testing the host-to-device (RX) path.
 * - Arms OUT endpoint at initialization.
 * - On each OUT completion, re-arms immediately for continuous receiving.
 */
class CDCReadTest : public CDCBase
{
 public:
  CDCReadTest(Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
              Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
              Endpoint::EPNumber comm_ep_num = Endpoint::EPNumber::EP_AUTO)
      : CDCBase(data_in_ep_num, data_out_ep_num, comm_ep_num)
  {
  }

  /**
   * @brief 初始化 CDC 读测试类：预装 OUT 端点接收
   *        Initialize CDC read test: pre-arm OUT endpoint
   *
   * 在初始化时即调用一次 Transfer()，保证主机数据可以立刻被接收。
   * Arms the OUT endpoint immediately so host data can be received right away.
   */
  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num,
                     bool in_isr) override
  {
    CDCBase::BindEndpoints(endpoint_pool, start_itf_num, in_isr);
    auto ep_data_out = GetDataOutEndpoint();
    ep_data_out->Transfer(ep_data_out->MaxTransferSize());
  }

  /**
   * @brief OUT 端点完成回调（读测试：持续接收）
   *        Data OUT complete callback (read test: continuous receive)
   *
   * 每次 OUT 完成后立即重新启动接收，以实现不间断的数据吞吐。
   * Each OUT completion re-arms the transfer for continuous throughput.
   */
  void OnDataOutComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(in_isr);
    UNUSED(data);

    auto ep_data_out = GetDataOutEndpoint();
    ep_data_out->Transfer(ep_data_out->MaxTransferSize());
  }

  /**
   * @brief IN 端点完成回调（读测试：无操作）
   *        Data IN complete callback (read test: no-op)
   *
   * 读测试场景下不产生设备->主机数据，因此此处为空实现。
   * In read test, no device-to-host data is generated; this is a no-op.
   */
  void OnDataInComplete(bool in_isr, ConstRawData& data) override
  {
    UNUSED(in_isr);
    UNUSED(data);
  }
};

}  // namespace LibXR::USB
