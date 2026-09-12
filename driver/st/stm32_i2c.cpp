#include "stm32_i2c.hpp"

#include "stm32_dcache.hpp"
#include "stm32_i2c_timing.hpp"
#if defined(STM32C0)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32c0xx_ll_rcc.h"
#endif
#if defined(STM32F0)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32f0xx_ll_rcc.h"
#endif
#if defined(STM32F3)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32f3xx_ll_rcc.h"
#endif
#if defined(STM32F7)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32f7xx_ll_rcc.h"
#endif
#if defined(STM32G0)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32g0xx_ll_rcc.h"
#endif
#if defined(STM32G4)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32g4xx_ll_rcc.h"
#endif
#if defined(STM32H5)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32h5xx_ll_rcc.h"
#endif
#if defined(STM32H7)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32h7xx_ll_rcc.h"
#endif
#if defined(STM32H7RS)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32h7rsxx_ll_rcc.h"
#endif
#if defined(STM32L0)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32l0xx_ll_rcc.h"
#endif
#if defined(STM32L4)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32l4xx_ll_rcc.h"
#endif
#if defined(STM32L5)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32l5xx_ll_rcc.h"
#endif
#if defined(STM32U0)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32u0xx_ll_rcc.h"
#endif
#if defined(STM32U3)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32u3xx_ll_rcc.h"
#endif
#if defined(STM32U5)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32u5xx_ll_rcc.h"
#endif
#if defined(STM32N6)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32n6xx_ll_rcc.h"
#endif
#if defined(STM32WB)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32wbxx_ll_rcc.h"
#endif
#if defined(STM32WBA)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32wbaxx_ll_rcc.h"
#endif
#if defined(STM32WL)
#define LIBXR_STM32_HAS_LL_RCC 1
#include "stm32wlxx_ll_rcc.h"
#endif
#ifdef HAL_I2C_MODULE_ENABLED

using namespace LibXR;

STM32I2C* STM32I2C::map[STM32_I2C_NUMBER] = {nullptr};

stm32_i2c_id_t STM32_I2C_GetID(I2C_TypeDef* hi2c)
{  // NOLINT
  if (hi2c == nullptr)
  {
    return stm32_i2c_id_t::STM32_I2C_ID_ERROR;
  }
#ifdef I2C1
  else if (hi2c == I2C1)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C1;
  }
#endif
#ifdef I2C2
  else if (hi2c == I2C2)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C2;
  }
#endif
#ifdef I2C3
  else if (hi2c == I2C3)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C3;
  }
#endif
#ifdef I2C4
  else if (hi2c == I2C4)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C4;
  }
#endif
#ifdef I2C5
  else if (hi2c == I2C5)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C5;
  }
#endif
#ifdef I2C6
  else if (hi2c == I2C6)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C6;
  }
#endif
#ifdef I2C7
  else if (hi2c == I2C7)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C7;
  }
#endif
#ifdef I2C8
  else if (hi2c == I2C8)
  {  // NOLINT
    return stm32_i2c_id_t::STM32_I2C8;
  }
#endif
  return stm32_i2c_id_t::STM32_I2C_ID_ERROR;
}

namespace
{

// 统一输入：
// - 7-bit 模式：传 0x00~0x7F（不带 R/W 位），内部左移 1 给 HAL
// - 10-bit 模式：传 0x000~0x3FF（不带 R/W 位），内部直接给 HAL
static inline uint16_t EncodeHalDevAddress(const I2C_HandleTypeDef* hi2c,
                                           uint16_t slave_addr)
{
  ASSERT(hi2c != nullptr);

#if defined(I2C_ADDRESSINGMODE_10BIT)
  if (hi2c->Init.AddressingMode == I2C_ADDRESSINGMODE_10BIT)
  {
    ASSERT(slave_addr <= 0x03FF);
    return static_cast<uint16_t>(slave_addr & 0x03FF);
  }
#endif

  // 默认按 7-bit
  ASSERT(slave_addr <= 0x007F);
  return static_cast<uint16_t>((slave_addr & 0x007F) << 1);
}

static inline ErrorCode MapHalStartFailure(const I2C_HandleTypeDef* hi2c,
                                           HAL_StatusTypeDef st)
{
  // Preserve HAL's immediate start-failure signal without forcing a full controller
  // reset.
#ifdef HAL_I2C_ERROR_NONE
  const uint32_t err = hi2c->ErrorCode;
#ifdef HAL_I2C_WRONG_START
  if ((err & (HAL_I2C_ERROR_TIMEOUT | HAL_I2C_WRONG_START)) != 0U)
#else
  if ((err & HAL_I2C_ERROR_TIMEOUT) != 0U)
#endif
  {
    return ErrorCode::TIMEOUT;
  }
  if (err != HAL_I2C_ERROR_NONE)
  {
    return ErrorCode::FAILED;
  }
#else
  UNUSED(hi2c);
#endif
  return (st == HAL_BUSY) ? ErrorCode::BUSY : ErrorCode::FAILED;
}

static void RecoverAfterBlockTimeout(STM32I2C* i2c)
{
  ASSERT(i2c != nullptr);

  auto* hi2c = i2c->i2c_handle_;
  i2c->recovering_ = true;
  if (hi2c->hdmarx != nullptr)
  {
    (void)HAL_DMA_Abort(hi2c->hdmarx);
  }
  if (hi2c->hdmatx != nullptr)
  {
    (void)HAL_DMA_Abort(hi2c->hdmatx);
  }

  // Re-open the HAL handle after a detached BLOCK timeout without touching
  // the larger software-side callback/semaphore semantics.
  (void)HAL_I2C_DeInit(hi2c);
  (void)HAL_I2C_Init(hi2c);

  i2c->read_ = false;
  i2c->read_op_ = {};
  i2c->write_op_ = {};
  i2c->read_buff_ = {nullptr, 0};
  i2c->recovering_ = false;
}

static inline ErrorCode WaitBlockResultAndRecoverTimeout(STM32I2C* i2c, uint32_t timeout)
{
  ASSERT(i2c != nullptr);
  const ErrorCode ans = i2c->block_wait_.Wait(timeout);
  if (ans == ErrorCode::TIMEOUT)
  {
    RecoverAfterBlockTimeout(i2c);
  }
  return ans;
}

static bool ResetI2CPeripheral(I2C_TypeDef* instance)
{
#if defined(I2C1) && defined(__HAL_RCC_I2C1_FORCE_RESET) && \
    defined(__HAL_RCC_I2C1_RELEASE_RESET)
  if (instance == I2C1)
  {
    __HAL_RCC_I2C1_FORCE_RESET();
    __NOP();
    __HAL_RCC_I2C1_RELEASE_RESET();
    return true;
  }
#endif

#if defined(I2C2) && defined(__HAL_RCC_I2C2_FORCE_RESET) && \
    defined(__HAL_RCC_I2C2_RELEASE_RESET)
  if (instance == I2C2)
  {
    __HAL_RCC_I2C2_FORCE_RESET();
    __NOP();
    __HAL_RCC_I2C2_RELEASE_RESET();
    return true;
  }
#endif

#if defined(I2C3) && defined(__HAL_RCC_I2C3_FORCE_RESET) && \
    defined(__HAL_RCC_I2C3_RELEASE_RESET)
  if (instance == I2C3)
  {
    __HAL_RCC_I2C3_FORCE_RESET();
    __NOP();
    __HAL_RCC_I2C3_RELEASE_RESET();
    return true;
  }
#endif

#if defined(I2C4) && defined(__HAL_RCC_I2C4_FORCE_RESET) && \
    defined(__HAL_RCC_I2C4_RELEASE_RESET)
  if (instance == I2C4)
  {
    __HAL_RCC_I2C4_FORCE_RESET();
    __NOP();
    __HAL_RCC_I2C4_RELEASE_RESET();
    return true;
  }
#endif

#if defined(I2C5) && defined(__HAL_RCC_I2C5_FORCE_RESET) && \
    defined(__HAL_RCC_I2C5_RELEASE_RESET)
  if (instance == I2C5)
  {
    __HAL_RCC_I2C5_FORCE_RESET();
    __NOP();
    __HAL_RCC_I2C5_RELEASE_RESET();
    return true;
  }
#endif

#if defined(I2C6) && defined(__HAL_RCC_I2C6_FORCE_RESET) && \
    defined(__HAL_RCC_I2C6_RELEASE_RESET)
  if (instance == I2C6)
  {
    __HAL_RCC_I2C6_FORCE_RESET();
    __NOP();
    __HAL_RCC_I2C6_RELEASE_RESET();
    return true;
  }
#endif

#if defined(I2C7) && defined(__HAL_RCC_I2C7_FORCE_RESET) && \
    defined(__HAL_RCC_I2C7_RELEASE_RESET)
  if (instance == I2C7)
  {
    __HAL_RCC_I2C7_FORCE_RESET();
    __NOP();
    __HAL_RCC_I2C7_RELEASE_RESET();
    return true;
  }
#endif

#if defined(I2C8) && defined(__HAL_RCC_I2C8_FORCE_RESET) && \
    defined(__HAL_RCC_I2C8_RELEASE_RESET)
  if (instance == I2C8)
  {
    __HAL_RCC_I2C8_FORCE_RESET();
    __NOP();
    __HAL_RCC_I2C8_RELEASE_RESET();
    return true;
  }
#endif

  return false;
}

static uint32_t NormalizeI2CClock(uint32_t frequency)
{
  if (frequency != 0U)
  {
    return frequency;
  }
#if defined(RCC_D2CCIP2R_I2C123SEL) && defined(RCC_I2C123CLKSOURCE_D2PCLK1)
  if ((RCC->D2CCIP2R & RCC_D2CCIP2R_I2C123SEL) == RCC_I2C123CLKSOURCE_D2PCLK1)
  {
    return HAL_RCC_GetPCLK1Freq();
  }
#endif
#if defined(RCC_D3CCIPR_I2C4SEL) && defined(RCC_I2C4CLKSOURCE_D3PCLK1)
  if ((RCC->D3CCIPR & RCC_D3CCIPR_I2C4SEL) == RCC_I2C4CLKSOURCE_D3PCLK1)
  {
    return HAL_RCC_GetPCLK1Freq();
  }
#endif
  return 0U;
}

static uint32_t GetI2CClock(I2C_TypeDef* instance)
{
#if defined(LIBXR_STM32_HAS_LL_RCC)
#if defined(I2C1) && defined(LL_RCC_I2C1_CLKSOURCE)
  if (instance == I2C1)
  {
    return LL_RCC_GetI2CClockFreq(LL_RCC_I2C1_CLKSOURCE);
  }
#endif
#if defined(I2C2) && defined(LL_RCC_I2C2_CLKSOURCE)
  if (instance == I2C2)
  {
    return LL_RCC_GetI2CClockFreq(LL_RCC_I2C2_CLKSOURCE);
  }
#endif
#if defined(I2C3) && defined(LL_RCC_I2C3_CLKSOURCE)
  if (instance == I2C3)
  {
    return LL_RCC_GetI2CClockFreq(LL_RCC_I2C3_CLKSOURCE);
  }
#endif
#if defined(I2C4) && defined(LL_RCC_I2C4_CLKSOURCE)
  if (instance == I2C4)
  {
    return LL_RCC_GetI2CClockFreq(LL_RCC_I2C4_CLKSOURCE);
  }
#endif
#if defined(I2C5) && defined(LL_RCC_I2C5_CLKSOURCE)
  if (instance == I2C5)
  {
    return LL_RCC_GetI2CClockFreq(LL_RCC_I2C5_CLKSOURCE);
  }
#endif
#if defined(I2C6) && defined(LL_RCC_I2C6_CLKSOURCE)
  if (instance == I2C6)
  {
    return LL_RCC_GetI2CClockFreq(LL_RCC_I2C6_CLKSOURCE);
  }
#endif
#endif
  return HAL_RCC_GetPCLK1Freq();
}

static bool ComputeTiming(I2C_HandleTypeDef* handle, uint32_t speed_hz, uint32_t& timing)
{
  // RCC reset restores the Timing-mode peripheral defaults: analog filter on,
  // digital filter disabled. HAL Timing structures on supported families do not
  // expose these filter fields.
  return STM32I2CTiming::Compute(GetI2CClock(handle->Instance), speed_hz, true, 0,
                                 timing);
}
}  // namespace

STM32I2C::STM32I2C(I2C_HandleTypeDef* hi2c, RawData dma_buff,
                   uint32_t dma_enable_min_size)
    : I2C(),
      id_(STM32_I2C_GetID(hi2c->Instance)),
      i2c_handle_(hi2c),
      dma_enable_min_size_(dma_enable_min_size),
      dma_buff_(dma_buff)
{
  ASSERT(id_ != STM32_I2C_ID_ERROR);
  ASSERT(id_ < STM32_I2C_NUMBER);
  if (id_ == STM32_I2C_ID_ERROR || id_ >= STM32_I2C_NUMBER)
  {
    return;
  }
  map[id_] = this;
}

ErrorCode STM32I2C::Read(uint16_t slave_addr, RawData read_data, ReadOperation& op,
                         bool in_isr)
{
  if (i2c_handle_->State != HAL_I2C_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  read_ = true;

  const uint16_t dev_addr = EncodeHalDevAddress(i2c_handle_, slave_addr);

  if (read_data.size_ > dma_enable_min_size_)
  {
    read_op_ = op;
    read_buff_ = read_data;
    if (op.type == ReadOperation::OperationType::BLOCK)
    {
      // Arm the BLOCK waiter before HAL exposes completion to IRQ context.
      block_wait_.Start(*op.data.sem_info.sem);
    }
    const HAL_StatusTypeDef st = HAL_I2C_Master_Receive_DMA(
        i2c_handle_, dev_addr, reinterpret_cast<uint8_t*>(dma_buff_.addr_),
        read_data.size_);
    if (st != HAL_OK)
    {
      if (op.type == ReadOperation::OperationType::BLOCK)
      {
        block_wait_.Cancel();
        return MapHalStartFailure(i2c_handle_, st);
      }
      return ErrorCode::BUSY;
    }
    op.MarkAsRunning();
    if (op.type == ReadOperation::OperationType::BLOCK)
    {
      return WaitBlockResultAndRecoverTimeout(this, op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    auto ans = HAL_I2C_Master_Receive(i2c_handle_, dev_addr,
                                      reinterpret_cast<uint8_t*>(read_data.addr_),
                                      read_data.size_, 20) == HAL_OK
                   ? ErrorCode::OK
                   : ErrorCode::BUSY;
    // BLOCK 模式下沿用统一状态更新路径。
    // Reuse the same status update path for BLOCK mode.
    if (op.type != ReadOperation::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, ans);
    }
    return ans;
  }
}

ErrorCode STM32I2C::Write(uint16_t slave_addr, ConstRawData write_data,
                          WriteOperation& op, bool in_isr)
{
  if (i2c_handle_->State != HAL_I2C_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  read_ = false;

  const uint16_t dev_addr = EncodeHalDevAddress(i2c_handle_, slave_addr);

  Memory::FastCopy(dma_buff_.addr_, write_data.addr_, write_data.size_);

  if (write_data.size_ > dma_enable_min_size_)
  {
    write_op_ = op;
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      // Arm the BLOCK waiter before HAL exposes completion to IRQ context.
      block_wait_.Start(*op.data.sem_info.sem);
    }
    STM32_CleanDCacheByAddr(dma_buff_.addr_, write_data.size_);
    const HAL_StatusTypeDef st = HAL_I2C_Master_Transmit_DMA(
        i2c_handle_, dev_addr, reinterpret_cast<uint8_t*>(dma_buff_.addr_),
        write_data.size_);
    if (st != HAL_OK)
    {
      if (op.type == WriteOperation::OperationType::BLOCK)
      {
        block_wait_.Cancel();
        return MapHalStartFailure(i2c_handle_, st);
      }
      return ErrorCode::BUSY;
    }
    op.MarkAsRunning();
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      return WaitBlockResultAndRecoverTimeout(this, op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    auto ans = HAL_I2C_Master_Transmit(i2c_handle_, dev_addr,
                                       reinterpret_cast<uint8_t*>(dma_buff_.addr_),
                                       write_data.size_, 20) == HAL_OK
                   ? ErrorCode::OK
                   : ErrorCode::BUSY;
    if (op.type != WriteOperation::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, ans);
    }
    return ans;
  }
}

ErrorCode STM32I2C::MemRead(uint16_t slave_addr, uint16_t mem_addr, RawData read_data,
                            ReadOperation& op, MemAddrLength mem_addr_size, bool in_isr)
{
  ASSERT(read_data.size_ <= dma_buff_.size_);

  if (i2c_handle_->State != HAL_I2C_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  read_ = true;

  const uint16_t dev_addr = EncodeHalDevAddress(i2c_handle_, slave_addr);

  if (read_data.size_ > dma_enable_min_size_)
  {
    read_op_ = op;
    read_buff_ = read_data;
    if (op.type == ReadOperation::OperationType::BLOCK)
    {
      // Arm the BLOCK waiter before HAL exposes completion to IRQ context.
      block_wait_.Start(*op.data.sem_info.sem);
    }
    const HAL_StatusTypeDef st = HAL_I2C_Mem_Read_DMA(
        i2c_handle_, dev_addr, mem_addr,
        mem_addr_size == MemAddrLength::BYTE_8 ? I2C_MEMADD_SIZE_8BIT
                                               : I2C_MEMADD_SIZE_16BIT,
        reinterpret_cast<uint8_t*>(dma_buff_.addr_), read_data.size_);
    if (st != HAL_OK)
    {
      if (op.type == ReadOperation::OperationType::BLOCK)
      {
        block_wait_.Cancel();
        return MapHalStartFailure(i2c_handle_, st);
      }
      return ErrorCode::BUSY;
    }
    op.MarkAsRunning();
    if (op.type == ReadOperation::OperationType::BLOCK)
    {
      return WaitBlockResultAndRecoverTimeout(this, op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    auto ans =
        HAL_I2C_Mem_Read(i2c_handle_, dev_addr, mem_addr,
                         mem_addr_size == MemAddrLength::BYTE_8 ? I2C_MEMADD_SIZE_8BIT
                                                                : I2C_MEMADD_SIZE_16BIT,
                         reinterpret_cast<uint8_t*>(read_data.addr_), read_data.size_,
                         20) == HAL_OK
            ? ErrorCode::OK
            : ErrorCode::BUSY;

    if (op.type != ReadOperation::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, ans);
    }
    return ans;
  }
}

ErrorCode STM32I2C::MemWrite(uint16_t slave_addr, uint16_t mem_addr,
                             ConstRawData write_data, WriteOperation& op,
                             MemAddrLength mem_addr_size, bool in_isr)
{
  ASSERT(write_data.size_ <= dma_buff_.size_);

  if (i2c_handle_->State != HAL_I2C_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  read_ = false;

  const uint16_t dev_addr = EncodeHalDevAddress(i2c_handle_, slave_addr);

  Memory::FastCopy(dma_buff_.addr_, write_data.addr_, write_data.size_);

  if (write_data.size_ > dma_enable_min_size_)
  {
    write_op_ = op;
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      // Arm the BLOCK waiter before HAL exposes completion to IRQ context.
      block_wait_.Start(*op.data.sem_info.sem);
    }
    STM32_CleanDCacheByAddr(dma_buff_.addr_, write_data.size_);
    const HAL_StatusTypeDef st = HAL_I2C_Mem_Write_DMA(
        i2c_handle_, dev_addr, mem_addr,
        mem_addr_size == MemAddrLength::BYTE_8 ? I2C_MEMADD_SIZE_8BIT
                                               : I2C_MEMADD_SIZE_16BIT,
        reinterpret_cast<uint8_t*>(dma_buff_.addr_), write_data.size_);
    if (st != HAL_OK)
    {
      if (op.type == WriteOperation::OperationType::BLOCK)
      {
        block_wait_.Cancel();
        return MapHalStartFailure(i2c_handle_, st);
      }
      return ErrorCode::BUSY;
    }
    op.MarkAsRunning();
    if (op.type == WriteOperation::OperationType::BLOCK)
    {
      return WaitBlockResultAndRecoverTimeout(this, op.data.sem_info.timeout);
    }
    return ErrorCode::OK;
  }
  else
  {
    auto ans =
        HAL_I2C_Mem_Write(i2c_handle_, dev_addr, mem_addr,
                          mem_addr_size == MemAddrLength::BYTE_8 ? I2C_MEMADD_SIZE_8BIT
                                                                 : I2C_MEMADD_SIZE_16BIT,
                          reinterpret_cast<uint8_t*>(dma_buff_.addr_), write_data.size_,
                          20) == HAL_OK
            ? ErrorCode::OK
            : ErrorCode::BUSY;

    if (op.type != WriteOperation::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, ans);
    }
    return ans;
  }
}

ErrorCode STM32I2C::SetConfig(Configuration config)
{
  if (i2c_handle_->State != HAL_I2C_STATE_READY)
  {
    return ErrorCode::BUSY;
  }

  const auto old_init = i2c_handle_->Init;
  if constexpr (HasClockSpeed<decltype(i2c_handle_)>::value)
  {
    SetClockSpeed<decltype(i2c_handle_)>(i2c_handle_, config);
  }
  else if constexpr (HasTiming<decltype(i2c_handle_)>::value)
  {
    uint32_t timing = 0U;
    if (!ComputeTiming(i2c_handle_, config.clock_speed, timing))
    {
      return ErrorCode::NOT_SUPPORT;
    }
    SetTiming<decltype(i2c_handle_)>(i2c_handle_, timing);
  }
  else
  {
    return ErrorCode::NOT_SUPPORT;
  }
  if (!ResetI2CPeripheral(i2c_handle_->Instance))
  {
    i2c_handle_->Init = old_init;
    return ErrorCode::NOT_SUPPORT;
  }
  if (HAL_I2C_Init(i2c_handle_) != HAL_OK)
  {
    i2c_handle_->Init = old_init;
    if (ResetI2CPeripheral(i2c_handle_->Instance))
    {
      (void)HAL_I2C_Init(i2c_handle_);
    }
    return ErrorCode::INIT_ERR;
  }
  return ErrorCode::OK;
}

extern "C" void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef* hi2c)
{
  STM32I2C* i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];
  if (i2c && !i2c->recovering_ &&
      (i2c->read_op_.type != ReadOperation::OperationType::NONE))
  {
    ErrorCode ec = ErrorCode::OK;
#ifdef HAL_I2C_ERROR_NONE
    ec = (hi2c->ErrorCode == HAL_I2C_ERROR_NONE) ? ErrorCode::OK : ErrorCode::FAILED;
#endif
    STM32_InvalidateDCacheByAddr(i2c->dma_buff_.addr_, i2c->read_buff_.size_);
    if (ec == ErrorCode::OK)
    {
      Memory::FastCopy(i2c->read_buff_.addr_, i2c->dma_buff_.addr_,
                       i2c->read_buff_.size_);
    }
    if (i2c->read_op_.type == ReadOperation::OperationType::BLOCK)
    {
      (void)i2c->block_wait_.TryPost(true, ec);
    }
    else
    {
      i2c->read_op_.UpdateStatus(true, ec);
    }
  }
}

extern "C" void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef* hi2c)
{
  STM32I2C* i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];
  if (i2c && !i2c->recovering_ &&
      (i2c->write_op_.type != WriteOperation::OperationType::NONE))
  {
    ErrorCode ec = ErrorCode::OK;
#ifdef HAL_I2C_ERROR_NONE
    ec = (hi2c->ErrorCode == HAL_I2C_ERROR_NONE) ? ErrorCode::OK : ErrorCode::FAILED;
#endif
    if (i2c->write_op_.type == WriteOperation::OperationType::BLOCK)
    {
      (void)i2c->block_wait_.TryPost(true, ec);
    }
    else
    {
      i2c->write_op_.UpdateStatus(true, ec);
    }
  }
}

extern "C" void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef* hi2c)
{
  STM32I2C* i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];
  if (i2c && !i2c->recovering_ &&
      (i2c->write_op_.type != WriteOperation::OperationType::NONE))
  {
    ErrorCode ec = ErrorCode::OK;
#ifdef HAL_I2C_ERROR_NONE
    ec = (hi2c->ErrorCode == HAL_I2C_ERROR_NONE) ? ErrorCode::OK : ErrorCode::FAILED;
#endif
    if (i2c->write_op_.type == WriteOperation::OperationType::BLOCK)
    {
      (void)i2c->block_wait_.TryPost(true, ec);
    }
    else
    {
      i2c->write_op_.UpdateStatus(true, ec);
    }
  }
}

extern "C" void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef* hi2c)
{
  STM32I2C* i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];
  if (i2c && !i2c->recovering_ &&
      (i2c->read_op_.type != ReadOperation::OperationType::NONE))
  {
    ErrorCode ec = ErrorCode::OK;
#ifdef HAL_I2C_ERROR_NONE
    ec = (hi2c->ErrorCode == HAL_I2C_ERROR_NONE) ? ErrorCode::OK : ErrorCode::FAILED;
#endif
    STM32_InvalidateDCacheByAddr(i2c->dma_buff_.addr_, i2c->read_buff_.size_);
    if (ec == ErrorCode::OK)
    {
      Memory::FastCopy(i2c->read_buff_.addr_, i2c->dma_buff_.addr_,
                       i2c->read_buff_.size_);
    }
    if (i2c->read_op_.type == ReadOperation::OperationType::BLOCK)
    {
      (void)i2c->block_wait_.TryPost(true, ec);
    }
    else
    {
      i2c->read_op_.UpdateStatus(true, ec);
    }
  }
}

extern "C" void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* hi2c)
{
  STM32I2C* i2c = STM32I2C::map[STM32_I2C_GetID(hi2c->Instance)];

  if (i2c && !i2c->recovering_)
  {
    if (i2c->read_)
    {
      if (i2c->read_op_.type == ReadOperation::OperationType::BLOCK)
      {
        (void)i2c->block_wait_.TryPost(true, ErrorCode::FAILED);
      }
      else
      {
        i2c->read_op_.UpdateStatus(true, ErrorCode::FAILED);
      }
    }
    else
    {
      if (i2c->write_op_.type == WriteOperation::OperationType::BLOCK)
      {
        (void)i2c->block_wait_.TryPost(true, ErrorCode::FAILED);
      }
      else
      {
        i2c->write_op_.UpdateStatus(true, ErrorCode::FAILED);
      }
    }
  }
}

#endif
