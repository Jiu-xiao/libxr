#include "model/uart_rx_config_gate.hpp"
#include "test.hpp"

void test_uart_rx_config_gate()
{
  {
    LibXR::UartRxConfigGate gate;
    gate.RequestConfig();
    ASSERT(gate.ConfigRequested());
    ASSERT(!gate.TryEnterRx());
    ASSERT(gate.TryEnterConfig());
    ASSERT(gate.ConfigRequested());
    gate.LeaveConfig();
    ASSERT(!gate.ConfigRequested());
    ASSERT(gate.TryEnterRx());
    ASSERT(!gate.LeaveRx());
  }

  {
    LibXR::UartRxConfigGate gate;
    ASSERT(gate.TryEnterRx());
    gate.RequestConfig();
    ASSERT(!gate.TryEnterConfig());
    ASSERT(gate.LeaveRx());
    ASSERT(gate.TryEnterConfig());
    gate.LeaveConfig();
    ASSERT(!gate.ConfigRequested());
  }

  {
    LibXR::UartRxConfigGate gate;
    gate.RequestConfig();
    ASSERT(gate.TryEnterConfig());
    gate.RequestConfig();
    gate.LeaveConfig();
    ASSERT(gate.ConfigRequested());
    ASSERT(gate.TryEnterConfig());
    gate.LeaveConfig();
    ASSERT(!gate.ConfigRequested());
  }
}
