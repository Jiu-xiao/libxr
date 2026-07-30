#include "uart_dma_tx_model_test_support.hpp"

void test_uart_dma_tx_model()
{
  LibXRTest::UartDmaTx::RunStateTests();
  LibXRTest::UartDmaTx::RunBoundaryTests();
  LibXRTest::UartDmaTx::RunOperationTests();
  LibXRTest::UartDmaTx::RunConcurrencyTests();
}
