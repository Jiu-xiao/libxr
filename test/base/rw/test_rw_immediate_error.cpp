/**
 * @file test_rw_immediate_error.cpp
 * @brief WritePort immediate-error scenarios.
 */
#include "rw_test_common.hpp"

namespace
{

void test_rw_immediate_write_error_propagates()
{
  using namespace LibXR;

  static const uint8_t TX[] = {0x55};
  for (auto mode : LibXRTest::ALL_MODES)
  {
    WritePort port(2, 16);
    port = FailWriteFun;

    LibXRTest::WriteHarness write(mode, 0);
    ASSERT(port(ConstRawData{TX, sizeof(TX)}, write.op) == ErrorCode::INIT_ERR);
    if (mode != LibXRTest::TestMode::NONE && mode != LibXRTest::TestMode::BLOCK)
    {
      write.ExpectFinal(ErrorCode::INIT_ERR);
    }
    ASSERT(port.Size() == 0);
    ASSERT(port.QueueInfo()->Size() == 0);
  }
}

}  // namespace

void RunBaseRwImmediateErrorTests() { test_rw_immediate_write_error_propagates(); }
