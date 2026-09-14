#include "i2c_runtime_fixture.hpp"
#define LIBXR_HPM_I2C_HAS_DMA_MGR 1
using hpm_stat_t = int;
constexpr int status_success = 0, status_fail = 1, status_timeout = 2,
              status_i2c_no_addr_hit = 3;
constexpr uint32_t I2C_SOC_TRANSFER_COUNT_MAX = 4095, I2C_STATUS_CMPL_MASK = 1,
                   I2C_STATUS_ADDRHIT_MASK = 2;
constexpr uint16_t kI2CFlagWriteCheckAck = 1, kI2CFlagRead = 2, kI2CFlagNoStop = 4,
                   kI2CFlagAddr10Bit = 8;
constexpr int I2C_DIR_MASTER_READ = 1;
struct I2C_Type
{
  uint32_t status = I2C_STATUS_CMPL_MASK | I2C_STATUS_ADDRHIT_MASK;
};
static int launch_mode = 0;
static unsigned completions = 0;
struct HPMI2C : I2C
{
  enum class AsyncTransferKind
  {
    NONE,
    READ,
    WRITE,
    MEM_READ
  };
  enum class AddressMode
  {
    ADDR_7BIT,
    ADDR_10BIT
  };
  enum class SequenceFrame
  {
    FIRST,
    LAST
  };
  struct AsyncTransferContext
  {
    AsyncTransferKind kind = AsyncTransferKind::NONE;
    uint16_t slave_addr = 0, mem_addr = 0, flags = 0;
    MemAddrLength mem_addr_size = MemAddrLength::BYTE_8;
    uint32_t mem_addr_size_in_byte = 0;
    uint8_t mem_addr_bytes[2]{};
    RawData read_data{};
    ConstRawData write_data{};
    ReadOperation read_op;
    WriteOperation write_op;
  } async_ctx_;
  struct AsyncCompletionStateMachine
  {
    static void MarkFailure(AsyncTransferContext&, hpm_stat_t, bool) {}
  };
  std::atomic<uint32_t> async_busy_{0}, async_completion_claim_{0};
  I2C_Type* i2c_;
  AddressMode address_mode_ = AddressMode::ADDR_7BIT;
  struct
  {
    uint32_t transfer_timeout_us = 1000, addr_hit_timeout_us = 1000;
  } wait_policy_;
  bool AsyncTransferActive() const { return async_busy_.load() != 0; }
  void ClearAsyncContext() { async_ctx_ = {}; }
  void ResetAsyncState()
  {
    ClearAsyncContext();
    async_busy_ = 0;
  }
  void CompleteNow()
  {
    auto op = async_ctx_.kind == AsyncTransferKind::WRITE ? async_ctx_.write_op
                                                          : async_ctx_.read_op;
    ResetAsyncState();
    ++completions;
    op.UpdateStatus(true, launch_mode == 2 ? ErrorCode::FAILED : ErrorCode::OK);
  }
  ErrorCode EnsureControllerReady() { return ErrorCode::OK; }
  uint16_t BuildTransferFlags(uint16_t f) { return f; }
  void StartAsyncBlockWaitIfNeeded(ReadOperation&) {}
  void CancelAsyncBlockWaitIfNeeded(ReadOperation&) {}
  ErrorCode EnableAsyncI2cIrq()
  {
    return launch_mode == 4 ? ErrorCode::NOT_SUPPORT : ErrorCode::OK;
  }
  void DisableAsyncI2cIrq() {}
  ErrorCode StartAsyncReadDma(void*, uint32_t)
  {
    return launch_mode == 3 ? ErrorCode::NOT_SUPPORT : ErrorCode::OK;
  }
  ErrorCode StartAsyncWriteDma(const void*, uint32_t)
  {
    return launch_mode == 3 ? ErrorCode::NOT_SUPPORT : ErrorCode::OK;
  }
  void AbortAsyncStart(bool, bool, bool) { ResetAsyncState(); }
  void StopAndReleaseAsyncBus() {}
  ErrorCode WaitForAsyncBlockResult(uint32_t) { return ErrorCode::OK; }
  bool ShouldRecover(hpm_stat_t s) { return s != status_success; }
  void RecoverController() {}
  ErrorCode ConvertStatus(hpm_stat_t s)
  {
    return s == status_success ? ErrorCode::OK : ErrorCode::FAILED;
  }
  ErrorCode ResolveMemAddressSize(MemAddrLength, uint32_t& size)
  {
    size = 1;
    return ErrorCode::OK;
  }
  void FillMemAddress(uint16_t addr, MemAddrLength, uint8_t* out)
  {
    out[0] = static_cast<uint8_t>(addr);
  }
  ErrorCode PrepareAsyncTransfer(uint16_t, uint16_t, uint32_t, bool, bool)
  {
    return ErrorCode::OK;
  }
  template <typename T>
  ErrorCode ValidateTransferArgs(uint16_t, T, bool)
  {
    return ErrorCode::OK;
  }
  ErrorCode DoSequenceWrite(uint16_t, ConstRawData, SequenceFrame, bool)
  {
    return ErrorCode::OK;
  }
  ErrorCode DoSequenceRead(uint16_t, RawData, SequenceFrame) { return ErrorCode::OK; }
  ErrorCode StartWriteAsync(uint16_t, ConstRawData, WriteOperation&);
  ErrorCode StartReadAsync(uint16_t, RawData, ReadOperation&);
  ErrorCode StartMemReadAsync(uint16_t, uint16_t, RawData, ReadOperation&, MemAddrLength);
  ErrorCode Read(uint16_t, RawData, ReadOperation&, bool);
  ErrorCode Write(uint16_t, ConstRawData, WriteOperation&, bool);
  ErrorCode MemRead(uint16_t, uint16_t, RawData, ReadOperation&, MemAddrLength, bool);
};
static HPMI2C* active;
template <typename P>
bool WaitUntil(P p, uint32_t)
{
  return p();
}
template <typename O>
ErrorCode FinishOperation(O& op, bool isr, ErrorCode ec)
{
  if (op.type != O::OperationType::BLOCK) op.UpdateStatus(isr, ec);
  return ec;
}
void i2c_clear_status(I2C_Type*, uint32_t) {}
void i2c_clear_fifo(I2C_Type*) {}
uint32_t i2c_get_status(I2C_Type* d) { return d->status; }
void i2c_write_byte(I2C_Type*, uint8_t) {}
void i2c_master_set_slave_address(I2C_Type*, uint16_t) {}
void i2c_set_direction(I2C_Type*, int) {}
void i2c_master_enable_start_phase(I2C_Type*) {}
void i2c_master_enable_addr_phase(I2C_Type*) {}
void i2c_master_enable_stop_phase(I2C_Type*) {}
void i2c_master_enable_data_phase(I2C_Type*) {}
void i2c_set_data_count(I2C_Type*, uint32_t) {}
void i2c_dma_disable(I2C_Type*) {}
void i2c_enable_10bit_address_mode(I2C_Type*, bool) {}
void i2c_master_issue_data_transmission(I2C_Type*)
{
  if (launch_mode != 1) active->CompleteNow();
}
int i2c_master_start_dma_read(I2C_Type* d, uint16_t, uint32_t)
{
  i2c_master_issue_data_transmission(d);
  return status_success;
}
int i2c_master_start_dma_write(I2C_Type* d, uint16_t, uint32_t)
{
  i2c_master_issue_data_transmission(d);
  return status_success;
}
int i2c_master_read(I2C_Type*, uint16_t, uint8_t*, uint32_t) { return status_success; }
int i2c_master_write(I2C_Type*, uint16_t, uint8_t*, uint32_t) { return status_success; }
int i2c_master_address_read(I2C_Type*, uint16_t, uint8_t*, uint32_t, uint8_t*, uint32_t)
{
  return status_success;
}
#include "hpm_i2c_start.inc"

int main()
{
  I2C_Type regs;
  HPMI2C b;
  b.i2c_ = &regs;
  active = &b;
  uint8_t data[8]{};
  unsigned cases = 0;
  for (int method = 0; method < 3; ++method)
    for (launch_mode = 0; launch_mode < 5; ++launch_mode)
    {
      b.ResetAsyncState();
      completions = 0;
      std::atomic<PollStatus> status{PollStatus::READY};
      ReadOperation op(status);
      auto invoke = [&](ReadOperation& operation)
      {
        if (method == 0) return b.Read(0x53, {data, 8}, operation, false);
        if (method == 1) return b.Write(0x53, {data, 8}, operation, false);
        return b.MemRead(0x53, 0, {data, 8}, operation, I2C::MemAddrLength::BYTE_8,
                         false);
      };
      const auto result = invoke(op);
      if (launch_mode == 3 || launch_mode == 4)
      {
        CHECK(result == ErrorCode::NOT_SUPPORT && status.load() == PollStatus::ERROR &&
              completions == 0 && !b.AsyncTransferActive());
      }
      else
      {
        CHECK(result == ErrorCode::OK);
        if (launch_mode == 1)
        {
          CHECK(status.load() == PollStatus::RUNNING);
          b.CompleteNow();
        }
        CHECK(completions == 1 &&
              status.load() == (launch_mode == 2 ? PollStatus::ERROR : PollStatus::DONE));
      }
      ReadOperation::Callback callback;
      ReadOperation cbop(callback);
      CHECK(invoke(cbop) == result);
      if (launch_mode == 1) b.CompleteNow();
      CHECK(callback.calls == 1);
      ++cases;
    }
  std::printf("HPM runtime: %u completion/start-failure cases PASS\n", cases);
}
