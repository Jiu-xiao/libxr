#include <cstdio>
#include <cstdlib>

#include "hpm_i2c.hpp"
#include "hpm_test_state.hpp"

namespace
{

void Check(bool condition, const char* message)
{
  if (!condition)
  {
    std::fprintf(stderr, "CHECK failed: %s\n", message);
    std::exit(1);
  }
}

void TestAsyncWriteNackCompletesAsError()
{
  hpm_test_reset_state();

  LibXR::HPMI2C i2c(HPM_I2C0, clock_i2c0, false, {100000});
  uint8_t tx[2] = {0x6B, 0x00};
  LibXR::WriteOperation::OperationPollingStatus status =
      LibXR::WriteOperation::OperationPollingStatus::READY;
  LibXR::WriteOperation op(status);

  LibXR::ErrorCode ans = i2c.Write(0x68, LibXR::ConstRawData(tx, sizeof(tx)), op);
  Check(ans == LibXR::ErrorCode::OK, "async write should start");
  Check(status == LibXR::WriteOperation::OperationPollingStatus::RUNNING,
        "async write polling op should be running after start");

  hpm_test_trigger_dma_tc();
  HPM_I2C0->STATUS = I2C_STATUS_CMPL_MASK;
  libxr_hpm_i2c_process_interrupt(HPM_I2C0);

  Check(status == LibXR::WriteOperation::OperationPollingStatus::ERROR,
        "async write NACK should complete polling op as error");
  Check(hpm_test_state().dma_disable_count > 0U,
        "async write completion should disable DMA");
  Check(hpm_test_state().i2c_reset_count > 0U,
        "async write NACK should enter controller recovery");
}

void TestAsyncMemReadAddrMissIssuesStop()
{
  hpm_test_reset_state();

  LibXR::HPMI2C i2c(HPM_I2C0, clock_i2c0, false, {100000});
  Check(i2c.SetWaitPolicy({1U, 1U, 1U, 1U}) == LibXR::ErrorCode::OK,
        "short wait policy should be accepted");

  uint8_t rx = 0;
  LibXR::ReadOperation::OperationPollingStatus status =
      LibXR::ReadOperation::OperationPollingStatus::READY;
  LibXR::ReadOperation op(status);

  const LibXR::ErrorCode ans =
      i2c.MemRead(0x68, 0x75, LibXR::RawData(&rx, sizeof(rx)), op,
                  LibXR::I2C::MemAddrLength::BYTE_8);

  Check(ans == LibXR::ErrorCode::NO_RESPONSE,
        "async mem-read address miss should return NO_RESPONSE");
  Check(status == LibXR::ReadOperation::OperationPollingStatus::ERROR,
        "async mem-read address miss should update polling status as error");
  Check(hpm_test_state().stop_issue_count > 0U,
        "async mem-read address miss should explicitly issue STOP");
  Check((hpm_test_state().last_stop_ctrl & I2C_CTRL_PHASE_STOP_MASK) != 0U,
        "recorded cleanup transaction should contain STOP phase");
  Check(hpm_test_state().dma_enable_count == 0U,
        "address miss should fail before RX DMA starts");
}

}  // namespace

int main()
{
  TestAsyncWriteNackCompletesAsError();
  TestAsyncMemReadAddrMissIssuesStop();
  std::puts("test_hpm_i2c_stub passed");
  return 0;
}
