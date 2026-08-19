#include "test.hpp"
#include "uart.hpp"

namespace
{

class ContextAwareUart final : public LibXR::UART
{
 public:
  ContextAwareUart()
      : UART(static_cast<LibXR::ReadPort*>(nullptr),
             static_cast<LibXR::WritePort*>(nullptr))
  {
  }

  LibXR::ErrorCode SetConfig(Configuration config, bool in_isr = false) override
  {
    config_ = config;
    last_in_isr_ = in_isr;
    return LibXR::ErrorCode::OK;
  }

  [[nodiscard]] bool LastInIsr() const { return last_in_isr_; }

 private:
  Configuration config_{};
  bool last_in_isr_ = false;
};

}  // namespace

void test_uart_context()
{
  constexpr LibXR::UART::Configuration CONFIG{115200U, LibXR::UART::Parity::NO_PARITY, 8U,
                                              1U};

  ContextAwareUart concrete_uart;
  LibXR::UART& uart = concrete_uart;

  ASSERT(uart.SetConfig(CONFIG) == LibXR::ErrorCode::OK);
  ASSERT(!concrete_uart.LastInIsr());

  ASSERT(uart.SetConfig(CONFIG, true) == LibXR::ErrorCode::OK);
  ASSERT(concrete_uart.LastInIsr());

  ASSERT(concrete_uart.SetConfig(CONFIG) == LibXR::ErrorCode::OK);
  ASSERT(!concrete_uart.LastInIsr());
}
