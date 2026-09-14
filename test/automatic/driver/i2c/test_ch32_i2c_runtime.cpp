#include "i2c_runtime_fixture.hpp"

constexpr int RESET = 0, SET = 1, DISABLE = 0, ENABLE = 1;
constexpr uint32_t I2C_FLAG_BUSY = 1, I2C_FLAG_BTF = 2;
constexpr uint32_t I2C_IT_ERR = 1, I2C_IT_BERR = 2, I2C_IT_ARLO = 3, I2C_IT_AF = 4,
                   I2C_IT_OVR = 5, I2C_IT_PECERR = 6, I2C_IT_TIMEOUT = 7,
                   I2C_IT_SMBALERT = 8;
constexpr int I2C_Mode_I2C = 0, I2C_DutyCycle_2 = 0, I2C_Ack_Enable = 1,
              I2C_AcknowledgedAddress_10bit = 1, I2C_AcknowledgedAddress_7bit = 0,
              I2C_NACKPosition_Current = 0, I2C_Direction_Transmitter = 0,
              I2C_Direction_Receiver = 1, I2C_EVENT_MASTER_MODE_SELECT = 1,
              I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED = 2;
constexpr int CH32_I2C_TX_DMA_IT_MAP[1] = {0}, CH32_I2C_RX_DMA_IT_MAP[1] = {1};
struct Dev
{
  bool busy = false;
  unsigned resets = 0, inits = 0;
  uint32_t clock = 0;
  bool err_irq = false;
};
struct DMACh
{
  uint32_t CNTR = 0;
};
struct RCC_ClocksTypeDef
{
  uint32_t PCLK1_Frequency;
};
struct I2C_InitTypeDef
{
  uint32_t I2C_ClockSpeed, I2C_Mode, I2C_DutyCycle, I2C_OwnAddress1, I2C_Ack,
      I2C_AcknowledgedAddress;
};
static uint32_t pclk = 36000000;
static bool stuck_after_reset = false, immediate = true, complete_error = false;
void RCC_GetClocksFreq(RCC_ClocksTypeDef* c) { c->PCLK1_Frequency = pclk; }
int I2C_GetFlagStatus(Dev* d, uint32_t) { return d->busy; }
void I2C_Cmd(Dev*, int) {}
void I2C_DeInit(Dev* d)
{
  ++d->resets;
  d->busy = stuck_after_reset;
  d->err_irq = false;
}
void I2C_Init(Dev* d, I2C_InitTypeDef* c)
{
  ++d->inits;
  d->clock = c->I2C_ClockSpeed;
}
void I2C_AcknowledgeConfig(Dev*, int) {}
void I2C_NACKPositionConfig(Dev*, int) {}
void I2C_ITConfig(Dev* d, int, int enabled) { d->err_irq = enabled != 0; }
void I2C_DMACmd(Dev*, int) {}
void I2C_DMALastTransferCmd(Dev*, int) {}
void DMA_Cmd(DMACh* d, int on)
{
  if (!on) d->CNTR = 0;
}
void DMA_ClearITPendingBit(int) {}
int DMA_GetITStatus(int) { return SET; }
void I2C_ClearITPendingBit(Dev*, uint32_t) {}
void I2C_GenerateSTOP(Dev*, int) {}
void I2C_GenerateSTART(Dev*, int) {}
void I2C_Send7bitAddress(Dev*, uint8_t, int) {}
struct CH32I2C : I2C
{
  Dev* instance_;
  DMACh tx{}, rx{};
  DMACh* dma_tx_channel_ = &tx;
  DMACh* dma_rx_channel_ = &rx;
  bool recovering_ = false, busy_ = false, read_ = false, ten_bit_addr_ = false;
  int id_ = 0;
  uint32_t dma_enable_min_size_ = 0;
  ReadOperation read_op_;
  WriteOperation write_op_;
  RawData read_buff_{}, dma_buff_{};
  TestWaiter block_wait_;
  Configuration cfg_{100000};
  static constexpr uint32_t K_DEFAULT_TIMEOUT_US = 20000;
  bool DmaBusy() const { return tx.CNTR != 0 || rx.CNTR != 0 || busy_; }
  bool WaitFlag(uint32_t f, int, uint32_t)
  {
    return f == I2C_FLAG_BTF || !instance_->busy;
  }
  bool WaitEvent(uint32_t) { return true; }
  void ClearAddrFlag() {}
  static uint8_t Addr7ToAddr8(uint16_t a) { return a << 1; }
  static uint16_t Addr10Clamp(uint16_t a) { return a; }
  ErrorCode MasterStartAndAddress(uint16_t, int) { return ErrorCode::OK; }
  ErrorCode SendMemAddr(uint16_t, MemAddrLength) { return ErrorCode::OK; }
  ErrorCode PollingReadBytes(uint8_t*, uint32_t) { return ErrorCode::OK; }
  ErrorCode PollingWriteBytes(const uint8_t*, uint32_t) { return ErrorCode::OK; }
  void StartTxDma(uint32_t)
  {
    if (immediate)
    {
      if (complete_error)
        AbortTransfer(ErrorCode::FAILED);
      else
        TxDmaIRQHandler();
    }
  }
  void StartRxDma(uint32_t n)
  {
    std::memset(dma_buff_.addr_, 0xE5, n);
    if (immediate)
    {
      if (complete_error)
        AbortTransfer(ErrorCode::FAILED);
      else
        RxDmaIRQHandler();
    }
  }
  ErrorCode SetConfig(Configuration);
  void ApplyConfig(Configuration);
  void RecoverAfterImmediateFailure();
  void AbortTransfer(ErrorCode);
  void TxDmaIRQHandler();
  void RxDmaIRQHandler();
  ErrorCode Read(uint16_t, RawData, ReadOperation&, bool);
  ErrorCode Write(uint16_t, ConstRawData, WriteOperation&, bool);
  ErrorCode MemRead(uint16_t, uint16_t, RawData, ReadOperation&, MemAddrLength, bool);
  ErrorCode MemWrite(uint16_t, uint16_t, ConstRawData, WriteOperation&, MemAddrLength,
                     bool);
};
#include "ch32_i2c_runtime.inc"

int main()
{
  Dev d;
  CH32I2C b;
  b.instance_ = &d;
  uint8_t stage[16]{}, data[8]{};
  b.dma_buff_ = {stage, 16};
  CHECK(b.SetConfig({100000}) == ErrorCode::OK && d.inits == 1 && d.err_irq);
  const auto initial = d.resets;
  for (int guard = 0; guard < 4; ++guard)
  {
    b.recovering_ = guard == 0;
    b.busy_ = guard == 1;
    b.tx.CNTR = guard == 2 ? 3 : 0;
    d.busy = guard == 3;
    CHECK(b.SetConfig({400000}) == ErrorCode::BUSY && d.resets == initial &&
          b.cfg_.clock_speed == 100000);
  }
  b.recovering_ = false;
  b.busy_ = false;
  b.tx.CNTR = 0;
  d.busy = false;
  CHECK(b.SetConfig({0}) == ErrorCode::ARG_ERR);
  CHECK(b.SetConfig({1000000}) == ErrorCode::NOT_SUPPORT);
  CHECK(b.SetConfig({1}) == ErrorCode::NOT_SUPPORT && d.resets == initial);
  b.RecoverAfterImmediateFailure();
  CHECK(d.resets == initial);
  d.busy = true;
  b.tx.CNTR = 8;
  b.busy_ = true;
  b.RecoverAfterImmediateFailure();
  CHECK(d.resets == initial + 1 && !d.busy && d.clock == 100000 && d.err_irq &&
        !b.recovering_ && !b.DmaBusy());
  stuck_after_reset = true;
  d.busy = true;
  b.RecoverAfterImmediateFailure();
  CHECK(d.resets == initial + 2 && d.busy && b.SetConfig({100000}) == ErrorCode::BUSY);
  stuck_after_reset = false;
  d.busy = false;
  unsigned cases = 0;
  for (int method = 0; method < 4; ++method)
    for (bool fast : {false, true})
      for (bool error : {false, true})
      {
        immediate = fast;
        complete_error = error;
        b.busy_ = false;
        std::atomic<PollStatus> status{PollStatus::READY};
        ReadOperation op(status);
        ErrorCode ec;
        switch (method)
        {
          case 0:
            ec = b.Read(0x53, {data, 8}, op, false);
            break;
          case 1:
            ec = b.Write(0x53, {data, 8}, op, false);
            break;
          case 2:
            ec = b.MemRead(0x53, 0, {data, 8}, op, I2C::MemAddrLength::BYTE_8, false);
            break;
          default:
            ec = b.MemWrite(0x53, 0, {data, 8}, op, I2C::MemAddrLength::BYTE_8, false);
        }
        CHECK(ec == ErrorCode::OK);
        if (!fast)
        {
          CHECK(status.load() == PollStatus::RUNNING);
          if (error)
            b.AbortTransfer(ErrorCode::FAILED);
          else if (method == 0 || method == 2)
            b.RxDmaIRQHandler();
          else
            b.TxDmaIRQHandler();
        }
        CHECK(status.load() == (error ? PollStatus::ERROR : PollStatus::DONE));
        ++cases;
      }
  std::printf("CH32 runtime: recovery/admission plus %u completion cases PASS\n", cases);
}
