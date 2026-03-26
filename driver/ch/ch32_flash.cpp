// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
#include "ch32_flash.hpp"

using namespace LibXR;

// WCH GCC15 is sensitive to the exact self-programming code shape on CH32V3.
// Keep the erase/write hot loops behind hard noinline boundaries, but leave the
// surrounding unlock/clock/flag choreography in the original call sites.
// WCH GCC15 对 CH32V3 自擦写路径的代码形状很敏感。
// 这里把擦除/写入热循环放到明确的 noinline 边界后面，
// 但解锁、降频、清标志这些外围时序仍留在原来的调用点。
extern "C" __attribute__((noinline)) ErrorCode CH32FlashEraseHotPath(uint32_t erase_begin,
                                                                     uint32_t erase_end);
extern "C" __attribute__((noinline)) ErrorCode CH32FlashWriteHotPath(
    uint32_t start_addr, uint32_t end_addr, const uint8_t* src);

// 访问时钟切半
static void flash_set_access_clock_half_sysclk(void)
{
  FLASH_Unlock();              // Unlock FPEC/CTL.
  FLASH->CTLR &= ~(1u << 25);  // SCKMOD=0 => SYSCLK/2.
  FLASH_Lock();                // Optional relock.
}

// 访问时钟还原
static void flash_set_access_clock_sysclk(void)
{
  FLASH_Unlock();
  FLASH->CTLR |= (1u << 25);  // SCKMOD=1 => access clock = SYSCLK.
  FLASH_Lock();
}

static inline void flash_exit_enhanced_read_if_enabled()
{
  if (FLASH->STATR & (1u << 7))
  {  // EHMODS=1?
    FLASH_Unlock();
    FLASH->CTLR &= ~(1u << 24);  // EHMOD=0.
    FLASH->CTLR |= (1u << 22);   // RSENACT=1 (write-only, auto-cleared by hardware).
    FLASH_Lock();
  }
}

static inline void flash_fast_unlock()
{
  FLASH_Unlock();  // Regular unlock sequence (KEYR).
  // Fast-mode unlock sequence (MODEKEYR KEY1/KEY2).
  FLASH->MODEKEYR = 0x45670123u;
  FLASH->MODEKEYR = 0xCDEF89ABu;
}

static inline void flash_fast_lock()
{
  FLASH_Unlock();
  FLASH->CTLR |= (1u << 15);  // FLOCK=1.
  FLASH_Lock();
}

static bool flash_wait_busy_clear(uint32_t spin = 1000000u)
{
  while (FLASH->STATR & 0x1u)
  {
    if (spin-- == 0)
    {
      return false;
    }
  }
  return true;
}

static inline void flash_clear_flags_once()
{
#ifdef FLASH_FLAG_BSY
  FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
#else
  FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
#endif
}

inline void CH32Flash::ClearFlashFlagsOnce()
{
  flash_clear_flags_once();
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

#if defined(__OPTIMIZE_SIZE__) || defined(LIBXR_CH32_FLASH_ERASE_ASM)
#if defined(__OPTIMIZE_SIZE__) || defined(LIBXR_CH32_FLASH_ERASE_ASM)
extern "C" __attribute__((naked, noinline)) ErrorCode CH32FlashEraseHotPath(
    uint32_t erase_begin, uint32_t erase_end)
{
  // `Os + LTO` on the slow-exec route can hard-fault immediately after STRT.
  // Keep only the size-optimized line on this fixed erase-loop sequence; default
  // optimized builds keep the simpler C loop below, which already passes.
  // slow-exec + `Os + LTO` 会在 STRT 之后立刻撞上取指窗口并 HardFault。
  // 这里只有 size-opt 构建走固定汇编序列；默认优化线继续走下面更简单的 C 循环，
  // 因为默认线本来就已经通过。
  __asm volatile(
      "bgeu a0, a1, 2f\n"
      "lui a3, 0x40022\n"
      "li a2, 32\n"
      "1:\n"
      "sw a0, 20(a3)\n"
      "lw a5, 16(a3)\n"
      "lui a4, 0x0f4\n"
      "addi a4, a4, 577\n"
      "ori a5, a5, 64\n"
      "sw a5, 16(a3)\n"
      "j 3f\n"
      "4:\n"
      "beqz a4, 5f\n"
      "3:\n"
      "lw a5, 12(a3)\n"
      "addi a4, a4, -1\n"
      "andi a5, a5, 1\n"
      "bnez a5, 4b\n"
      "lw a5, 12(a3)\n"
      "andi a5, a5, 16\n"
      "bnez a5, 6f\n"
      "sw a2, 12(a3)\n"
      "addi a0, a0, 256\n"
      "bltu a0, a1, 1b\n"
      "2:\n"
      "li a0, 0\n"
      "ret\n"
      "6:\n"
      "li a5, 48\n"
      "sw a5, 12(a3)\n"
      "5:\n"
      "li a0, -1\n"
      "ret\n");
}
#else
extern "C" __attribute__((noinline)) ErrorCode CH32FlashEraseHotPath(uint32_t erase_begin,
                                                                     uint32_t erase_end)
{
  for (uint32_t adr = erase_begin; adr < erase_end; adr += 256u)
  {
    FLASH->ADDR = adr;
    FLASH->CTLR |= (1u << 6);

    if (!flash_wait_busy_clear())
    {
      return ErrorCode::FAILED;
    }

    if (FLASH->STATR & (1u << 4))
    {
      FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
      return ErrorCode::FAILED;
    }

    FLASH_ClearFlag(FLASH_FLAG_EOP);
  }
  return ErrorCode::OK;
}
#endif
#else
extern "C" __attribute__((noinline)) ErrorCode CH32FlashEraseHotPath(uint32_t erase_begin,
                                                                     uint32_t erase_end)
{
  for (uint32_t adr = erase_begin; adr < erase_end; adr += 256u)
  {
    FLASH->ADDR = adr;
    FLASH->CTLR |= (1u << 6);

    if (!flash_wait_busy_clear())
    {
      return ErrorCode::FAILED;
    }

    if (FLASH->STATR & (1u << 4))
    {
      FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
      return ErrorCode::FAILED;
    }

    FLASH_ClearFlag(FLASH_FLAG_EOP);
  }
  return ErrorCode::OK;
}
#endif

ErrorCode CH32Flash::Erase(size_t offset, size_t size)
{
  if (size == 0)
  {
    return ErrorCode::ARG_ERR;
  }

  ASSERT(SystemCoreClock <= 120000000);

  const uint32_t START_ADDR = base_address_ + static_cast<uint32_t>(offset);
  if (!IsInRange(START_ADDR, size))
  {
    return ErrorCode::OUT_OF_RANGE;
  }
  const uint32_t END_ADDR = START_ADDR + static_cast<uint32_t>(size);

  // 1) 退出增强读模式
  flash_exit_enhanced_read_if_enabled();  // 进入擦写前必须退出 EHMOD

  // 2) 访问时钟降为 SYSCLK/2（SCKMOD=0）
  flash_set_access_clock_half_sysclk();

  // 3) 解锁：常规 + 快速模式
  flash_fast_unlock();
  ClearFlashFlagsOnce();

  // 4) 计算 256B 对齐区间
  const uint32_t FAST_SZ = 256u;
  const uint32_t ERASE_BEGIN = START_ADDR & ~(FAST_SZ - 1u);
  const uint32_t ERASE_END = (END_ADDR + FAST_SZ - 1u) & ~(FAST_SZ - 1u);
  if (ERASE_END <= ERASE_BEGIN)
  {
    flash_fast_lock();
    flash_set_access_clock_sysclk();
    return ErrorCode::OK;
  }

  FLASH->CTLR |= (1u << 17);
  const ErrorCode ec = CH32FlashEraseHotPath(ERASE_BEGIN, ERASE_END);
  FLASH->CTLR &= ~(1u << 17);
  flash_fast_lock();
  flash_set_access_clock_sysclk();
  return ec;
}

ErrorCode CH32Flash::Write(size_t offset, ConstRawData data)
{
  ASSERT(SystemCoreClock <= 120000000);

  if (!data.addr_ || data.size_ == 0)
  {
    ASSERT(false);
    return ErrorCode::ARG_ERR;
  }

  const uint32_t START_ADDR = base_address_ + static_cast<uint32_t>(offset);
  if (!IsInRange(START_ADDR, data.size_))
  {
    ASSERT(false);
    return ErrorCode::OUT_OF_RANGE;
  }

  const uint8_t* src = reinterpret_cast<const uint8_t*>(data.addr_);
  const uint32_t END_ADDR = START_ADDR + static_cast<uint32_t>(data.size_);
  return CH32FlashWriteHotPath(START_ADDR, END_ADDR, src);
}

extern "C" __attribute__((noinline)) ErrorCode CH32FlashWriteHotPath(
    uint32_t start_addr, uint32_t end_addr, const uint8_t* src)
{
  flash_exit_enhanced_read_if_enabled();
  flash_set_access_clock_half_sysclk();

  FLASH_Unlock();
  flash_clear_flags_once();

  const uint32_t HW_BEGIN = start_addr & ~1u;
  const uint32_t HW_END = (end_addr + 1u) & ~1u;

  for (uint32_t hw = HW_BEGIN; hw < HW_END; hw += 2u)
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

    if (val == orig)
    {
      continue;  // 无变化
    }

    // 未擦除单元禁止 0->1 抬位
    if (((~orig) & val) != 0u && orig != 0xE339u)
    {
      FLASH_Lock();
      flash_set_access_clock_sysclk();
      return ErrorCode::FAILED;
    }

    if (FLASH_ProgramHalfWord(hw, val) != FLASH_COMPLETE)
    {
      FLASH_Lock();
      flash_set_access_clock_sysclk();
      return ErrorCode::FAILED;
    }

    if (*p != val)
    {
      FLASH_Lock();
      flash_set_access_clock_sysclk();
      return ErrorCode::FAILED;
    }

    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
  }

  FLASH_Lock();
  flash_set_access_clock_sysclk();
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

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
