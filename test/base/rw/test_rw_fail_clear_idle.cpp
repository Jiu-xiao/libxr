/**
 * @file test_rw_fail_clear_idle.cpp
 * @brief Quiescent FailAndClearAll queue cleanup.
 */
#include "rw_test_common.hpp"

namespace
{

void test_rw_write_port_fail_and_clear_all_clears_idle_queue()
{
  using namespace LibXR;

  WritePort port(2, 16);
  port = PendingWriteFun;

  static const uint8_t TX[] = {0x21, 0x22, 0x23};
  WriteOperation operation;
  ASSERT(port(ConstRawData{TX, sizeof(TX)}, operation) == ErrorCode::OK);
  ASSERT(port.Size() == sizeof(TX));
  ASSERT(port.queue_info_->Size() == 1);

  port.FailAndClearAll(ErrorCode::INIT_ERR, false);

  ASSERT(port.Size() == 0);
  ASSERT(port.queue_info_->Size() == 0);
}

}  // namespace

void RunBaseRwFailAndClearIdleTests()
{
  test_rw_write_port_fail_and_clear_all_clears_idle_queue();
}
