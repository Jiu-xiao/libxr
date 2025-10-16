#include "ch32_flash.hpp"

using namespace LibXR;

// 访问时钟切半
static void Flash_SetAccessClock_HalfSysclk(void)
{
  FLASH_Unlock();              // 解锁 FPEC/CTL
  FLASH->CTLR &= ~(1u << 25);  // SCKMOD=0 => SYSCLK/2
  FLASH_Lock();                // 可选：改完再上锁
}

// 访问时钟还原
static void Flash_SetAccessClock_Sysclk(void)
{
  FLASH_Unlock();
  FLASH->CTLR |= (1u << 25);  // SCKMOD=1 => 访问时钟=SYSCLK
  FLASH_Lock();
}

static inline void Flash_ExitEnhancedReadIfEnabled()
{
  if (FLASH->STATR & (1u << 7))
  {  // EHMODS=1?
    FLASH_Unlock();
    FLASH->CTLR &= ~(1u << 24);  // EHMOD=0
    FLASH->CTLR |= (1u << 22);   // RSENACT=1（WO，硬件自动清）
    FLASH_Lock();
  }
}

static inline void Flash_FastUnlock()
{
  FLASH_Unlock();  // 常规解锁：写 KEYR(两把钥匙) 由库函数完成
  // 快速模式解锁：向 MODEKEYR 依次写入 KEY1/KEY2
  FLASH->MODEKEYR = 0x45670123u;
  FLASH->MODEKEYR = 0xCDEF89ABu;
}

static inline void Flash_FastLock()
{
  FLASH_Unlock();
  FLASH->CTLR |= (1u << 15);  // FLOCK=1 复锁快速模式
  FLASH_Lock();
}

static bool Flash_WaitBusyClear(uint32_t spin = 1000000u)
{
  while (FLASH->STATR & 0x1u)
  {
    if (spin-- == 0) return false;
  }
  return true;
}

inline void CH32Flash::ClearFlashFlagsOnce()
{
#ifdef FLASH_FLAG_BSY
  FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
#else
  FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
#endif
}

CH32Flash::CH32Flash(const FlashSector* sectors, size_t sector_count, size_t start_sector)
    : Flash(sectors[start_sector - 1].size, MinWriteSize(),
            {reinterpret_cast<void*>(sectors[start_sector - 1].address),
             sectors[sector_count - 1].address - sectors[start_sector - 1].address +
                 sectors[sector_count - 1].size}),
      sectors_(sectors),
      base_address_(sectors[start_sector - 1].address),
      sector_count_(sector_count)
{
}

ErrorCode CH32Flash::Erase(size_t offset, size_t size)
{
  if (size == 0) return ErrorCode::ARG_ERR;

  ASSERT(SystemCoreClock <= 120000000);

  const uint32_t start_addr = base_address_ + static_cast<uint32_t>(offset);
  if (!IsInRange(start_addr, size)) return ErrorCode::OUT_OF_RANGE;
  const uint32_t end_addr = start_addr + static_cast<uint32_t>(size);

  // 1) 退出增强读模式
  Flash_ExitEnhancedReadIfEnabled();  // 进入擦写前必须退出 EHMOD

  // 2) 访问时钟降为 SYSCLK/2（SCKMOD=0）
  Flash_SetAccessClock_HalfSysclk();

  // 3) 解锁：常规 + 快速模式
  Flash_FastUnlock();
  ClearFlashFlagsOnce();

  // 4) 计算 256B 对齐区间
  const uint32_t FAST_SZ = 256u;
  const uint32_t erase_begin = start_addr & ~(FAST_SZ - 1u);
  const uint32_t erase_end = (end_addr + FAST_SZ - 1u) & ~(FAST_SZ - 1u);
  if (erase_end <= erase_begin)
  {
    Flash_FastLock();
    Flash_SetAccessClock_Sysclk();
    return ErrorCode::OK;
  }

  // 5) 开启快速页擦除：CTLR.FTER=1
  FLASH->CTLR |= (1u << 17);

  // 6) 逐页(256B)擦除
  for (uint32_t adr = erase_begin; adr < erase_end; adr += FAST_SZ)
  {
    // 写入页首地址
    FLASH->ADDR = adr;

    // 触发 STRT
    FLASH->CTLR |= (1u << 6);

    // 等待 BSY=0
    if (!Flash_WaitBusyClear())
    {
      // 关闭 FTER & 复锁 & 还原时钟后返回
      FLASH->CTLR &= ~(1u << 17);
      Flash_FastLock();
      Flash_SetAccessClock_Sysclk();
      return ErrorCode::FAILED;
    }

    // 检查错误并清标志
    if (FLASH->STATR & (1u << 4))
    {  // WRPRTERR
      FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
      FLASH->CTLR &= ~(1u << 17);
      Flash_FastLock();
      Flash_SetAccessClock_Sysclk();
      return ErrorCode::FAILED;
    }

    // 成功：清 EOP
    FLASH_ClearFlag(FLASH_FLAG_EOP);
  }

  // 7) 关闭快速页擦除位，复锁快速模式与常规锁，并恢复访问时钟
  FLASH->CTLR &= ~(1u << 17);  // FTER=0
  Flash_FastLock();
  Flash_SetAccessClock_Sysclk();

  return ErrorCode::OK;
}

ErrorCode CH32Flash::Write(size_t offset, ConstRawData data)
{
  ASSERT(SystemCoreClock <= 120000000);

  // 退出增强读模式 → 减少失败风险（手册要求在编程/擦除前退出）
  Flash_ExitEnhancedReadIfEnabled();

  // 访问时钟切半（SCKMOD=0）
  Flash_SetAccessClock_HalfSysclk();

  if (!data.addr_ || data.size_ == 0)
  {
    Flash_SetAccessClock_Sysclk();
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  const uint32_t start_addr = base_address_ + static_cast<uint32_t>(offset);
  if (!IsInRange(start_addr, data.size_))
  {
    Flash_SetAccessClock_Sysclk();
    ASSERT(false);
    return ErrorCode::OUT_OF_RANGE;
  }

  const uint8_t* src = reinterpret_cast<const uint8_t*>(data.addr_);
  const uint32_t end_addr = start_addr + static_cast<uint32_t>(data.size_);

  FLASH_Unlock();
  ClearFlashFlagsOnce();

  const uint32_t hw_begin = start_addr & ~1u;
  const uint32_t hw_end = (end_addr + 1u) & ~1u;  // 向上取整到半字

  for (uint32_t hw = hw_begin; hw < hw_end; hw += 2u)
  {
    volatile uint16_t* p = reinterpret_cast<volatile uint16_t*>(hw);
    volatile uint16_t orig = *p;
    volatile uint16_t val = orig;

    if (hw >= start_addr && hw < end_addr)
    {
      uint8_t b0 = src[hw - start_addr];
      val = static_cast<uint16_t>((val & 0xFF00u) | b0);
    }
    if ((hw + 1u) >= start_addr && (hw + 1u) < end_addr)
    {
      uint8_t b1 = src[(hw + 1u) - start_addr];
      val = static_cast<uint16_t>((val & 0x00FFu) | (static_cast<uint16_t>(b1) << 8));
    }

    if (val == orig) continue;  // 无变化

    // 未擦除单元禁止 0->1 抬位
    if (((~orig) & val) != 0u && orig != 0xE339u)
    {
      FLASH_Lock();
      Flash_SetAccessClock_Sysclk();
      ASSERT(false);
      return ErrorCode::FAILED;
    }

    if (FLASH_ProgramHalfWord(hw, val) != FLASH_COMPLETE)
    {
      FLASH_Lock();
      Flash_SetAccessClock_Sysclk();
      ASSERT(false);
      return ErrorCode::FAILED;
    }

    ASSERT(*p == val);

    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
  }

  FLASH_Lock();
  Flash_SetAccessClock_Sysclk();
  return ErrorCode::OK;
}

bool CH32Flash::IsInRange(uint32_t addr, size_t size) const
{
  const uint32_t BEGIN = base_address_;
  const uint32_t LIMIT =
      sectors_[sector_count_ - 1].address + sectors_[sector_count_ - 1].size;
  const uint32_t END = addr + static_cast<uint32_t>(size);
  return (addr >= BEGIN) && (END <= LIMIT) && (END >= addr);
}
