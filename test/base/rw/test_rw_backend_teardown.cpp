/**
 * @file test_rw_backend_teardown.cpp
 * @brief Backend-only ReadPort and WritePort teardown scenarios.
 */
#include "rw_test_common.hpp"

namespace
{

void test_rw_backend_teardown_completes_async_pending_operations()
{
  for (auto mode : LibXRTest::ASYNC_MODES)
  {
    VerifyPendingReadBackendTeardownMode(mode, LibXR::ErrorCode::INIT_ERR);
    VerifyPendingWriteBackendTeardownMode(mode, LibXR::ErrorCode::INIT_ERR);
  }
}

void test_rw_backend_teardown_clears_idle_write_queue()
{
  using namespace LibXR;

  BackendTeardownWritePort port(2, 16);
  port = PendingWriteFun;
  static const uint8_t TX[] = {0x21, 0x22, 0x23};
  WriteOperation operation;

  ASSERT(port(ConstRawData{TX, sizeof(TX)}, operation) == ErrorCode::OK);
  ASSERT(port.Size() == sizeof(TX));
  ASSERT(port.queue_info_->Size() == 1);

  port.ResetForBackendTeardown(ErrorCode::INIT_ERR, false);

  ASSERT(port.Size() == 0);
  ASSERT(port.queue_info_->Size() == 0);
}

}  // namespace

void RunBaseRwBackendTeardownTests()
{
  test_rw_backend_teardown_completes_async_pending_operations();
  test_rw_backend_teardown_clears_idle_write_queue();
}
