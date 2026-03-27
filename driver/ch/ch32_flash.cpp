// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr)
#include "ch32_flash.hpp"

using namespace LibXR;

// WCH GCC15 is sensitive to the exact self-programming code shape on CH32V3.
// Keep the erase/write hot loops behind hard noinline boundaries, but leave the
// surrounding unlock/clock/flag choreography in the original call sites.
// WCH GCC15 对 CH32V3 自擦写路径的代码形状很敏感。
// 这里把擦除/写入热循环放到明确的 noinline 边界后面，
// 但解锁、降频、清标志这些外围时序仍留在原来的调用点。
extern "C" __attribute__((noinline)) ErrorCode CH32FlashWriteHotPath(
    uint32_t start_addr, uint32_t end_addr, const uint8_t* src);

namespace
{
using CH32FlashEraseHotPathFn = ErrorCode (*)(uint32_t erase_begin, uint32_t erase_end);
using CH32FlashWriteHotLoopFn = ErrorCode (*)(uint32_t start_addr, uint32_t end_addr,
                                              const uint8_t* src);
using CH32FlashWritePageFn = ErrorCode (*)(uint32_t start_addr, uint32_t end_addr,
                                           const uint8_t* src);

extern "C" ErrorCode CH32FlashEraseHotPathFlash(uint32_t erase_begin, uint32_t erase_end);
extern "C" void CH32FlashEraseHotPathFlashEnd();
extern "C" ErrorCode CH32FlashWriteHotLoopFlash(uint32_t start_addr, uint32_t end_addr,
                                                const uint8_t* src);
extern "C" void CH32FlashWriteHotLoopFlashEnd();
extern "C" ErrorCode CH32FlashWritePageFlash(uint32_t start_addr, uint32_t end_addr,
                                             const uint8_t* src);
extern "C" void CH32FlashWritePageFlashEnd();

struct CH32FlashHotPaths
{
  CH32FlashEraseHotPathFn erase = nullptr;
  CH32FlashWriteHotLoopFn write_halfword = nullptr;
  CH32FlashWritePageFn write_page = nullptr;
};

constexpr size_t kRoutineAlign = 16u;
// Current verified WCH GCC15 build emits:
// - erase loop      : 0x78 bytes
// - halfword writer : 0xD2 bytes
// - page writer     : 0x138 bytes
// Rounded up to 16-byte alignment and packed into one SRAM arena.
constexpr size_t kHotCodeBytes = 0x2C0u;

alignas(kRoutineAlign) static uint8_t g_hot_code[kHotCodeBytes];
static CH32FlashHotPaths g_ch32_flash_hot_paths;

static uintptr_t AlignUp(uintptr_t value, size_t align)
{
  return (value + align - 1u) & ~(static_cast<uintptr_t>(align) - 1u);
}

template <typename Fn>
static size_t RoutineSize(Fn flash_fn, const void* flash_end)
{
  const auto* begin = reinterpret_cast<const uint8_t*>(flash_fn);
  const auto* end = reinterpret_cast<const uint8_t*>(flash_end);
  return static_cast<size_t>(end - begin);
}

static inline void flash_clear_status_direct(uint32_t flags)
{
  FLASH->STATR = flags;
}

extern "C" __attribute__((noinline, used, section(".text.ch32_flash_erase_hot_src")))
ErrorCode CH32FlashEraseHotPathFlash(uint32_t erase_begin, uint32_t erase_end)
{
  constexpr uint32_t kFastEraseBit = (1u << 17);
  constexpr uint32_t kFastPageSize = 256u;

  for (uint32_t adr = erase_begin; adr < erase_end; adr += kFastPageSize)
  {
    FLASH->CTLR |= kFastEraseBit;
    FLASH->ADDR = adr;
    FLASH->CTLR |= (1u << 6);

    uint32_t spin = 1000000u;
    while (FLASH->STATR & 0x1u)
    {
      if (spin-- == 0u)
      {
        FLASH->CTLR &= ~kFastEraseBit;
        return ErrorCode::TIMEOUT;
      }
    }

    if (FLASH->STATR & (1u << 4))
    {
      FLASH->CTLR &= ~kFastEraseBit;
      flash_clear_status_direct(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
      return ErrorCode::STATE_ERR;
    }

    FLASH->CTLR &= ~kFastEraseBit;
    flash_clear_status_direct(FLASH_FLAG_EOP);
  }
  return ErrorCode::OK;
}

extern "C" __attribute__((noinline, used, section(".text.ch32_flash_erase_hot_src")))
void CH32FlashEraseHotPathFlashEnd()
{
}

template <typename Fn>
static Fn CopyRoutineToSram(Fn flash_fn, const void* flash_end, uint8_t*& cursor,
                            uint8_t* limit)
{
  const size_t size = RoutineSize(flash_fn, flash_end);
  cursor = reinterpret_cast<uint8_t*>(AlignUp(reinterpret_cast<uintptr_t>(cursor), kRoutineAlign));

  ASSERT(size != 0u);
  ASSERT(static_cast<size_t>(limit - cursor) >= size);
  if (size == 0u || static_cast<size_t>(limit - cursor) < size)
  {
    return flash_fn;
  }

  std::memcpy(cursor, reinterpret_cast<const void*>(flash_fn), size);
  __builtin___clear_cache(reinterpret_cast<char*>(cursor),
                          reinterpret_cast<char*>(cursor + size));

  Fn sram_fn = reinterpret_cast<Fn>(cursor);
  cursor += size;
  return sram_fn;
}

static void InitHotPathsOnce()
{
  static bool inited = false;
  if (inited)
  {
    return;
  }

  const size_t erase_size =
      AlignUp(RoutineSize(CH32FlashEraseHotPathFlash,
                          reinterpret_cast<const void*>(CH32FlashEraseHotPathFlashEnd)),
              kRoutineAlign);
  const size_t write_halfword_size =
      AlignUp(RoutineSize(CH32FlashWriteHotLoopFlash,
                          reinterpret_cast<const void*>(CH32FlashWriteHotLoopFlashEnd)),
              kRoutineAlign);
  const size_t write_page_size =
      AlignUp(RoutineSize(CH32FlashWritePageFlash,
                          reinterpret_cast<const void*>(CH32FlashWritePageFlashEnd)),
              kRoutineAlign);

  const size_t total_size = erase_size + write_halfword_size + write_page_size;
  ASSERT(total_size <= sizeof(g_hot_code));

  uint8_t* cursor = reinterpret_cast<uint8_t*>(
      AlignUp(reinterpret_cast<uintptr_t>(g_hot_code), kRoutineAlign));
  uint8_t* limit = g_hot_code + sizeof(g_hot_code);

  g_ch32_flash_hot_paths.erase = CopyRoutineToSram(
      CH32FlashEraseHotPathFlash, reinterpret_cast<const void*>(CH32FlashEraseHotPathFlashEnd),
      cursor, limit);
  g_ch32_flash_hot_paths.write_halfword = CopyRoutineToSram(
      CH32FlashWriteHotLoopFlash, reinterpret_cast<const void*>(CH32FlashWriteHotLoopFlashEnd),
      cursor, limit);
  g_ch32_flash_hot_paths.write_page = CopyRoutineToSram(
      CH32FlashWritePageFlash, reinterpret_cast<const void*>(CH32FlashWritePageFlashEnd), cursor,
      limit);
  inited = true;
}

extern "C" __attribute__((noinline, used, section(".text.ch32_flash_write_hot_src")))
ErrorCode CH32FlashWriteHotLoopFlash(uint32_t start_addr, uint32_t end_addr, const uint8_t* src)
{
  const uint32_t HW_BEGIN = start_addr & ~1u;
  const uint32_t HW_END = (end_addr + 1u) & ~1u;

  for (uint32_t hw = HW_BEGIN; hw < HW_END; hw += 2u)
  {
    auto* p = reinterpret_cast<volatile uint16_t*>(hw);
    const uint16_t orig = *p;
    uint16_t val = orig;

    if (hw >= start_addr && hw < end_addr)
    {
      const uint8_t b0 = src[hw - start_addr];
      val = static_cast<uint16_t>((val & 0xFF00u) | b0);
    }
    if ((hw + 1u) >= start_addr && (hw + 1u) < end_addr)
    {
      const uint8_t b1 = src[(hw + 1u) - start_addr];
      val = static_cast<uint16_t>((val & 0x00FFu) | (static_cast<uint16_t>(b1) << 8));
    }

    if (val == orig)
    {
      continue;
    }

    if (((~orig) & val) != 0u && orig != 0xE339u)
    {
      return ErrorCode::STATE_ERR;
    }

    FLASH->CTLR |= (1u << 0);
    *p = val;

    uint32_t spin = 1000000u;
    while (FLASH->STATR & 0x1u)
    {
      if (spin-- == 0u)
      {
        FLASH->CTLR &= ~(1u << 0);
        return ErrorCode::TIMEOUT;
      }
    }

    FLASH->CTLR &= ~(1u << 0);

    if (FLASH->STATR & (1u << 4))
    {
      flash_clear_status_direct(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
      return ErrorCode::STATE_ERR;
    }

    if (*p != val)
    {
      return ErrorCode::CHECK_ERR;
    }

    flash_clear_status_direct(FLASH_FLAG_EOP);
  }

  return ErrorCode::OK;
}

extern "C" __attribute__((noinline, used, section(".text.ch32_flash_write_hot_src")))
void CH32FlashWriteHotLoopFlashEnd()
{
}

extern "C" __attribute__((noinline, used, section(".text.ch32_flash_write_page_src")))
ErrorCode CH32FlashWritePageFlash(uint32_t start_addr, uint32_t end_addr, const uint8_t* src)
{
  constexpr uint32_t kPageSize = 256u;
  constexpr uint32_t kPageProgramBit = (1u << 16);
  constexpr uint32_t kProgramStartBit = (1u << 21);
  constexpr uint32_t kWriteBusyBit = (1u << 1);

  for (uint32_t page = start_addr; page < end_addr; page += kPageSize)
  {
    FLASH->CTLR |= kPageProgramBit;

    uint32_t spin = 1000000u;
    while (FLASH->STATR & 0x1u)
    {
      if (spin-- == 0u)
      {
        FLASH->CTLR &= ~kPageProgramBit;
        return ErrorCode::TIMEOUT;
      }
    }

    spin = 1000000u;
    while (FLASH->STATR & kWriteBusyBit)
    {
      if (spin-- == 0u)
      {
        FLASH->CTLR &= ~kPageProgramBit;
        return ErrorCode::TIMEOUT;
      }
    }

    const uint8_t* page_src = src + (page - start_addr);
    for (uint32_t i = 0u; i < kPageSize; i += 4u)
    {
      const uint32_t word =
          static_cast<uint32_t>(page_src[i]) |
          (static_cast<uint32_t>(page_src[i + 1u]) << 8) |
          (static_cast<uint32_t>(page_src[i + 2u]) << 16) |
          (static_cast<uint32_t>(page_src[i + 3u]) << 24);

      *reinterpret_cast<volatile uint32_t*>(page + i) = word;

      spin = 1000000u;
      while (FLASH->STATR & kWriteBusyBit)
      {
        if (spin-- == 0u)
        {
          FLASH->CTLR &= ~kPageProgramBit;
          return ErrorCode::TIMEOUT;
        }
      }
    }

    FLASH->CTLR |= kProgramStartBit;
    spin = 1000000u;
    while (FLASH->STATR & 0x1u)
    {
      if (spin-- == 0u)
      {
        FLASH->CTLR &= ~kPageProgramBit;
        return ErrorCode::TIMEOUT;
      }
    }

    FLASH->CTLR &= ~kPageProgramBit;

    if (FLASH->STATR & (1u << 4))
    {
      flash_clear_status_direct(FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
      return ErrorCode::STATE_ERR;
    }

    for (uint32_t i = 0u; i < kPageSize; i += 2u)
    {
      const uint16_t expect =
          static_cast<uint16_t>(page_src[i]) |
          (static_cast<uint16_t>(page_src[i + 1u]) << 8);
      const uint16_t actual = *reinterpret_cast<volatile uint16_t*>(page + i);
      if (actual != expect)
      {
        return ErrorCode::CHECK_ERR;
      }
    }

    flash_clear_status_direct(FLASH_FLAG_EOP);
  }

  return ErrorCode::OK;
}

extern "C" __attribute__((noinline, used, section(".text.ch32_flash_write_page_src")))
void CH32FlashWritePageFlashEnd()
{
}
}  // namespace

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
  InitHotPathsOnce();
}

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
  const auto erase_hot_path = g_ch32_flash_hot_paths.erase;
  ASSERT(erase_hot_path != nullptr);

  // 1) 退出增强读模式
  flash_exit_enhanced_read_if_enabled();  // 进入擦写前必须退出 EHMOD

  // 2) 访问时钟降为 SYSCLK/2（SCKMOD=0）
  flash_set_access_clock_half_sysclk();

  // 3) 解锁：常规 + 快速模式
  __disable_irq();
  flash_fast_unlock();
  ClearFlashFlagsOnce();

  // 4) 计算 256B 对齐区间
  const uint32_t FAST_SZ = 256u;
  const uint32_t ERASE_BEGIN = START_ADDR & ~(FAST_SZ - 1u);
  const uint32_t ERASE_END = (END_ADDR + FAST_SZ - 1u) & ~(FAST_SZ - 1u);
  if (ERASE_END <= ERASE_BEGIN)
  {
    flash_fast_lock();
    __enable_irq();
    flash_set_access_clock_sysclk();
    return ErrorCode::OK;
  }

  const ErrorCode ec = erase_hot_path(ERASE_BEGIN, ERASE_END);
  flash_fast_lock();
  __enable_irq();
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
  const auto write_hot_loop = g_ch32_flash_hot_paths.write_halfword;
  const auto write_page = g_ch32_flash_hot_paths.write_page;
  constexpr uint32_t kPageSize = 256u;
  ASSERT(write_hot_loop != nullptr && write_page != nullptr);
  flash_exit_enhanced_read_if_enabled();
  flash_set_access_clock_half_sysclk();

  __disable_irq();
  flash_fast_unlock();
  flash_clear_flags_once();
  ErrorCode ec = ErrorCode::OK;

  const uint32_t aligned_begin = (start_addr + kPageSize - 1u) & ~(kPageSize - 1u);
  const uint32_t aligned_end = end_addr & ~(kPageSize - 1u);

  const uint32_t head_end = (aligned_begin < end_addr) ? aligned_begin : end_addr;
  if (start_addr < head_end)
  {
    ec = write_hot_loop(start_addr, head_end, src);
  }

  const bool has_full_pages = (aligned_begin < aligned_end);
  if (ec == ErrorCode::OK && has_full_pages)
  {
    ec = write_page(aligned_begin, aligned_end, src + (aligned_begin - start_addr));
  }

  // Tail begins after the already-consumed head/body segments.
  // For single-page unaligned writes, `head_end` may already reach `end_addr`,
  // so the tail must start from `head_end` instead of replaying the same span.
  // 尾段必须从已经消费完的 head/body 之后开始。
  // 对“同页内的非对齐小写入”，`head_end` 可能已经等于 `end_addr`，
  // 这里不能再从 `start_addr` 重放同一段。
  const uint32_t tail_begin = has_full_pages ? aligned_end : head_end;
  if (ec == ErrorCode::OK && tail_begin < end_addr)
  {
    ec = write_hot_loop(tail_begin, end_addr, src + (tail_begin - start_addr));
  }

  flash_fast_lock();
  __enable_irq();
  flash_set_access_clock_sysclk();
  return ec;
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
