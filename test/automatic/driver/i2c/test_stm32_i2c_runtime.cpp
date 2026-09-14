#include "i2c_runtime_fixture.hpp"
#include "stm32_i2c_timing.hpp"

// HAL model: init preserves Timing but clears filter configuration (H5 CR1 / F4 FLTR).
#define HAL_I2C_ERROR_NONE 0U
constexpr uint32_t I2C_CR1_PE = 1U;
#if defined(TEST_LEGACY_FILTER)
#define I2C_FLTR_ANOFF 0x10U
#define I2C_FLTR_DNF 0xFU
#elif !defined(TEST_NO_FILTER)
#define I2C_CR1_ANFOFF 0x1000U
#define I2C_CR1_DNF 0xF00U
#define I2C_CR1_DNF_Pos 8U
#endif
#define CLEAR_BIT(reg, mask) ((reg) &= ~(mask))
#define SET_BIT(reg, mask) ((reg) |= (mask))
#define MODIFY_REG(reg, mask, value) ((reg) = ((reg) & ~(mask)) | (value))
constexpr int HAL_I2C_STATE_READY = 0, HAL_I2C_STATE_BUSY = 1;
constexpr int HAL_OK = 0, HAL_BUSY = 1, HAL_ERROR = 2;
constexpr int I2C_MEMADD_SIZE_8BIT = 1, I2C_MEMADD_SIZE_16BIT = 2;
using HAL_StatusTypeDef = int;
struct I2C_TypeDef
{
  uint32_t CR1 = 1, FLTR = 0, TIMINGR = 0;
};
struct I2C_InitTypeDef
{
  uint32_t Timing = 0;
};
struct I2C_HandleTypeDef
{
  I2C_TypeDef* Instance;
  I2C_InitTypeDef Init{};
  int State = HAL_I2C_STATE_READY;
  uint32_t ErrorCode = 0;
  void* hdmarx = nullptr;
  void* hdmatx = nullptr;
};
struct STM32I2C : I2C
{
  I2C_HandleTypeDef* i2c_handle_;
  bool recovering_ = false, read_ = false;
  uint32_t dma_enable_min_size_ = 0;
  RawData dma_buff_{}, read_buff_{};
  ReadOperation read_op_{};
  WriteOperation write_op_{};
  TestWaiter block_wait_;
  ErrorCode Read(uint16_t, RawData, ReadOperation&, bool);
  ErrorCode Write(uint16_t, ConstRawData, WriteOperation&, bool);
  ErrorCode MemRead(uint16_t, uint16_t, RawData, ReadOperation&, MemAddrLength, bool);
  ErrorCode MemWrite(uint16_t, uint16_t, ConstRawData, WriteOperation&, MemAddrLength,
                     bool);
};
static STM32I2C* active;
static int launch_mode = 0, completions = 0, aborts = 0;
static bool init_fails = false;
static void (*pending)(I2C_HandleTypeDef*) = nullptr;
STM32I2C* FindI2C(I2C_HandleTypeDef*) { return active; }
uint16_t EncodeHalDevAddress(const I2C_HandleTypeDef*, uint16_t a) { return a << 1; }
ErrorCode MapHalStartFailure(const I2C_HandleTypeDef*, int s)
{
  return s == HAL_BUSY ? ErrorCode::BUSY : ErrorCode::FAILED;
}
ErrorCode WaitBlockResultAndRecoverTimeout(STM32I2C* bus, uint32_t t)
{
  return bus->block_wait_.Wait(t);
}
void STM32_InvalidateDCacheByAddr(void*, size_t) {}
void STM32_CleanDCacheByAddr(void*, size_t) {}
int HAL_DMA_Abort(void*)
{
  ++aborts;
  return HAL_OK;
}
int HAL_I2C_DeInit(I2C_HandleTypeDef* h)
{
  h->Instance->CR1 = 0;
  h->Instance->FLTR = 0;
  return HAL_OK;
}
int HAL_I2C_Init(I2C_HandleTypeDef* h)
{
  h->State = HAL_I2C_STATE_BUSY;
  h->Instance->TIMINGR = h->Init.Timing;
  h->Instance->CR1 = 1;
  h->Instance->FLTR = 0;
  if (init_fails) return HAL_ERROR;
  h->State = HAL_I2C_STATE_READY;
  return HAL_OK;
}
extern "C" void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef*);
extern "C" void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef*);
extern "C" void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef*);
extern "C" void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef*);
extern "C" void HAL_I2C_ErrorCallback(I2C_HandleTypeDef*);
int Launch(I2C_HandleTypeDef* h, uint8_t* data, size_t n, bool read,
           void (*complete)(I2C_HandleTypeDef*))
{
  if (launch_mode == 2) return HAL_BUSY;
  if (launch_mode == 3) return HAL_ERROR;
  if (read) std::memset(data, 0xE5, n);
  if (launch_mode == 1)
  {
    pending = complete;
    return HAL_OK;
  }
  h->ErrorCode = launch_mode == 4 ? 1U : 0U;
  ++completions;
  if (h->ErrorCode)
    HAL_I2C_ErrorCallback(h);
  else
    complete(h);
  return HAL_OK;
}
int HAL_I2C_Master_Receive_DMA(I2C_HandleTypeDef* h, uint16_t, uint8_t* p, size_t n)
{
  return Launch(h, p, n, true, HAL_I2C_MasterRxCpltCallback);
}
int HAL_I2C_Master_Transmit_DMA(I2C_HandleTypeDef* h, uint16_t, uint8_t* p, size_t n)
{
  return Launch(h, p, n, false, HAL_I2C_MasterTxCpltCallback);
}
int HAL_I2C_Mem_Read_DMA(I2C_HandleTypeDef* h, uint16_t, uint16_t, int, uint8_t* p,
                         size_t n)
{
  return Launch(h, p, n, true, HAL_I2C_MemRxCpltCallback);
}
int HAL_I2C_Mem_Write_DMA(I2C_HandleTypeDef* h, uint16_t, uint16_t, int, uint8_t* p,
                          size_t n)
{
  return Launch(h, p, n, false, HAL_I2C_MemTxCpltCallback);
}
int HAL_I2C_Master_Receive(I2C_HandleTypeDef*, uint16_t, uint8_t*, size_t, int)
{
  return HAL_OK;
}
int HAL_I2C_Master_Transmit(I2C_HandleTypeDef*, uint16_t, uint8_t*, size_t, int)
{
  return HAL_OK;
}
int HAL_I2C_Mem_Read(I2C_HandleTypeDef*, uint16_t, uint16_t, int, uint8_t*, size_t, int)
{
  return HAL_OK;
}
int HAL_I2C_Mem_Write(I2C_HandleTypeDef*, uint16_t, uint16_t, int, uint8_t*, size_t, int)
{
  return HAL_OK;
}
#include "stm32_i2c_runtime.inc"

int main()
{
  I2C_TypeDef regs;
  I2C_HandleTypeDef h{&regs};
  STM32I2C b;
  b.i2c_handle_ = &h;
  active = &b;
  uint8_t dma[8]{}, payload[4]{};
  b.dma_buff_ = {dma, sizeof(dma)};
  unsigned cases = 0;
  for (int method = 0; method < 4; ++method)
    for (launch_mode = 0; launch_mode < 5; ++launch_mode)
    {
      h.State = HAL_I2C_STATE_READY;
      h.ErrorCode = 0;
      pending = nullptr;
      completions = 0;
      std::atomic<PollStatus> state{PollStatus::READY};
      ReadOperation op(state);
      auto invoke = [&](ReadOperation& operation)
      {
        switch (method)
        {
          case 0:
            return b.Read(0x53, {payload, 4}, operation, false);
          case 1:
            return b.Write(0x53, {payload, 4}, operation, false);
          case 2:
            return b.MemRead(0x53, 0, {payload, 4}, operation, I2C::MemAddrLength::BYTE_8,
                             false);
          default:
            return b.MemWrite(0x53, 0, {payload, 4}, operation,
                              I2C::MemAddrLength::BYTE_8, false);
        }
      };
      const auto result = invoke(op);
      if (launch_mode == 1)
      {
        CHECK(result == ErrorCode::OK && state.load() == PollStatus::RUNNING && pending);
        pending(&h);
        CHECK(state.load() == PollStatus::DONE);
      }
      else if (launch_mode == 2 || launch_mode == 3)
      {
        CHECK(result == ErrorCode::BUSY && state.load() == PollStatus::ERROR &&
              completions == 0);
      }
      else
      {
        CHECK(result == ErrorCode::OK && completions == 1);
        CHECK(state.load() == (launch_mode == 4 ? PollStatus::ERROR : PollStatus::DONE));
      }
      if (launch_mode != 1)
      {
        ReadOperation::Callback callback;
        ReadOperation cbop(callback);
        completions = 0;
        const auto cbresult = invoke(cbop);
        CHECK(callback.calls == (launch_mode == 2 || launch_mode == 3 ? 0U : 1U));
        CHECK(cbresult == result);
        Semaphore sem;
        ReadOperation block(sem, 100);
        invoke(block);
        CHECK(b.block_wait_.posts == (launch_mode == 2 || launch_mode == 3 ? 0U : 1U));
      }
      ++cases;
    }
  h.hdmarx = &h;
  h.hdmatx = &h;
  for (bool analog : {false, true})
    for (uint32_t dnf = 0; dnf < 16; ++dnf)
    {
      regs.CR1 = 1;
      regs.FLTR = 0;
      uint32_t timing = 0;
      CHECK(STM32I2CTiming::Compute(64000000, 100000, analog, dnf, timing));
      h.Init.Timing = timing;
      regs.TIMINGR = timing;
      RestoreI2CFilterState(&h, {analog, dnf});
      const auto before = CaptureI2CFilterState(&h);
      aborts = 0;
      RecoverAfterBlockTimeout(&b);
      const auto after = CaptureI2CFilterState(&h);
      CHECK(before.analog_filter == after.analog_filter &&
            before.digital_filter == after.digital_filter);
      CHECK(regs.TIMINGR == timing && aborts == 2 && !b.recovering_ &&
            b.read_buff_.size_ == 0);
      ++cases;
    }
  init_fails = true;
  RestoreI2CFilterState(&h, {false, 15});
  RecoverAfterBlockTimeout(&b);
  CHECK(h.State == HAL_I2C_STATE_BUSY && !b.recovering_ && regs.CR1 == 1 &&
        regs.FLTR == 0);
  std::printf("STM32 runtime: %u transfer/filter cases and init-failure PASS\n", cases);
}
