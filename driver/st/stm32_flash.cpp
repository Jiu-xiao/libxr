#include "stm32_flash.hpp"

#ifdef HAL_FLASH_MODULE_ENABLED

using namespace LibXR;

namespace
{
/** @brief Flash 擦写期间的缓存状态与写锁管理 / Cache state and write lock for Flash
 * operations. */
class FlashOperationGuard
{
 public:
  FlashOperationGuard()
  {
#if defined(ICACHE) && defined(ICACHE_CR_EN) && defined(ICACHE_SR_BUSYF)
    standalone_enabled_ = (ICACHE->CR & ICACHE_CR_EN) != 0U;
    if (standalone_enabled_)
    {
      // 关闭独立 ICACHE 会自动失效；等硬件空闲，不消费共享的完成标志。
      // Disabling standalone ICACHE invalidates it; wait for idle without consuming
      // the shared completion flag.
      CLEAR_BIT(ICACHE->CR, ICACHE_CR_EN);
      __DSB();
      __ISB();
      const uint32_t start = HAL_GetTick();
      while ((ICACHE->CR & ICACHE_CR_EN) != 0U || (ICACHE->SR & ICACHE_SR_BUSYF) != 0U)
      {
        if (static_cast<uint32_t>(HAL_GetTick() - start) > 1U)
        {
          // 抢占期间可能已完成，超时报错前重新确认。
          // Completion may occur during preemption; recheck before failing.
          REQUIRE((ICACHE->CR & ICACHE_CR_EN) == 0U &&
                  (ICACHE->SR & ICACHE_SR_BUSYF) == 0U);
        }
      }
    }
#endif
#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
    i_cache_enabled_ = (SCB->CCR & SCB_CCR_IC_Msk) != 0U;
    if (i_cache_enabled_) SCB_DisableICache();
#endif
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    d_cache_enabled_ = (SCB->CCR & SCB_CCR_DC_Msk) != 0U;
    if (d_cache_enabled_) SCB_DisableDCache();
#endif
    HAL_FLASH_Unlock();
  }

  ~FlashOperationGuard()
  {
    HAL_FLASH_Lock();
#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
    if (i_cache_enabled_) SCB_EnableICache();
#endif
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (d_cache_enabled_) SCB_EnableDCache();
#endif
#if defined(ICACHE) && defined(ICACHE_CR_EN) && defined(ICACHE_SR_BUSYF)
    if (standalone_enabled_)
    {
      __DSB();
      SET_BIT(ICACHE->CR, ICACHE_CR_EN);
      __DSB();
      __ISB();
    }
#endif
  }

  FlashOperationGuard(const FlashOperationGuard&) = delete;
  FlashOperationGuard& operator=(const FlashOperationGuard&) = delete;

 private:
#if defined(ICACHE) && defined(ICACHE_CR_EN) && defined(ICACHE_SR_BUSYF)
  bool standalone_enabled_ = false;
#endif
#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
  bool i_cache_enabled_ = false;
#endif
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  bool d_cache_enabled_ = false;
#endif
};
}  // namespace

STM32Flash::STM32Flash(const FlashSector* sectors, size_t sector_count,
                       size_t start_sector)
    : Flash(sectors[start_sector - 1].size, DetermineMinWriteSize(),
            {reinterpret_cast<void*>(sectors[start_sector - 1].address),
             sectors[sector_count - 1].address - sectors[start_sector - 1].address +
                 sectors[sector_count - 1].size}),
      sectors_(sectors),
      base_address_(sectors[start_sector - 1].address),
      program_type_(DetermineProgramType()),
      sector_count_(sector_count)
{
}

ErrorCode STM32Flash::Erase(size_t offset, size_t size)
{
  if (size == 0)
  {
    return ErrorCode::ARG_ERR;
  }

  uint32_t start_addr = base_address_ + offset;
  uint32_t end_addr = start_addr + size;

  FlashOperationGuard operation;

  for (size_t i = 0; i < sector_count_; ++i)
  {
    const auto& sector = sectors_[i];
    if (sector.address + sector.size <= start_addr)
    {
      continue;
    }
    if (sector.address >= end_addr)
    {
      break;
    }
    FLASH_EraseInitTypeDef erase_init = {};

#if defined(FLASH_TYPEERASE_PAGES) && defined(FLASH_PAGE_SIZE)  // STM32F1/G4... series
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    SetNbPages(erase_init, sector.address, i);
    erase_init.NbPages = 1;
    SetBanks(erase_init, sector.address);
#elif defined(FLASH_TYPEERASE_SECTORS)  // STM32F4/F7/H7... series
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
#if defined(FLASH_SECTOR_TOTAL)
    erase_init.Sector = static_cast<uint32_t>(i) % FLASH_SECTOR_TOTAL;
#elif defined(FLASH_SECTOR_NB)
    erase_init.Sector = static_cast<uint32_t>(i) % FLASH_SECTOR_NB;
#else
#error "No supported Flash sector count defined"
#endif
    erase_init.NbSectors = 1;
#if defined(FLASH_BANK_1)
    erase_init.Banks = STM32FlashBankOf(sector.address);
#endif
#if defined(FLASH_CR_PSIZE)
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_1;
#endif
#else
    return ErrorCode::NOT_SUPPORT;
#endif

    uint32_t error = 0;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &error);
    if (status != HAL_OK || error != 0xFFFFFFFFU)
    {
      return ErrorCode::FAILED;
    }
  }

  return ErrorCode::OK;
}

ErrorCode STM32Flash::Write(size_t offset, ConstRawData data)
{
  if (!data.addr_ || data.size_ == 0)
  {
    return ErrorCode::ARG_ERR;
  }

  uint32_t addr = base_address_ + offset;
  if (!IsInRange(addr, data.size_))
  {
    return ErrorCode::OUT_OF_RANGE;
  }

  FlashOperationGuard operation;

  const uint8_t* src = reinterpret_cast<const uint8_t*>(data.addr_);
  size_t written = 0;

#if defined(FLASH_TYPEPROGRAM_FLASHWORD) || defined(FLASH_TYPEPROGRAM_QUADWORD)
  alignas(LibXR::HW_CACHE_LINE_SIZE)
      uint32_t flash_word_buffer[DetermineMinWriteSize() / sizeof(uint32_t)];
  while (written < data.size_)
  {
    size_t chunk_size = LibXR::min<size_t>(MinWriteSize(), data.size_ - written);

    Memory::FastSet(flash_word_buffer, 0xFF, sizeof(flash_word_buffer));
    Memory::FastCopy(flash_word_buffer, src + written, chunk_size);

    if (Memory::FastCmp(reinterpret_cast<const uint8_t*>(addr + written), src + written,
                        chunk_size) == 0)
    {
      written += chunk_size;
      continue;
    }

    if (HAL_FLASH_Program(program_type_, addr + written,
                          reinterpret_cast<uint32_t>(flash_word_buffer)) != HAL_OK)
    {
      return ErrorCode::FAILED;
    }

    written += chunk_size;
  }

#else
  while (written < data.size_)
  {
    size_t chunk_size = LibXR::min<size_t>(MinWriteSize(), data.size_ - written);

    if (Memory::FastCmp(reinterpret_cast<const uint8_t*>(addr + written), src + written,
                        chunk_size) == 0)
    {
      written += chunk_size;
      continue;
    }

    uint64_t word = 0xFFFFFFFFFFFFFFFF;
    Memory::FastCopy(&word, src + written, chunk_size);

    if (HAL_FLASH_Program(program_type_, addr + written, word) != HAL_OK)
    {
      return ErrorCode::FAILED;
    }

    written += chunk_size;
  }
#endif

  return ErrorCode::OK;
}

bool STM32Flash::IsInRange(uint32_t addr, size_t size) const
{
  const uint32_t BEGIN = base_address_;
  const uint32_t LIMIT =
      sectors_[sector_count_ - 1].address + sectors_[sector_count_ - 1].size;
  const uint32_t END = addr + size;
  return (addr >= BEGIN) && (END <= LIMIT) && (END >= addr);  // 最后一项防溢出
}

#endif
