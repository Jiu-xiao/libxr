#include "libxr_def.hpp"

/**
 * @brief Fast memory copy routine optimized for alignment and burst copying.
 *
 * This function copies memory from @p src to @p dst, using different strategies based on
 * the alignment of the pointers and the address difference. If both pointers have the
 * same alignment offset, it performs head alignment, then burst copies in 8- or 4-byte
 * words (depending on @c LIBXR_ALIGN_SIZE). If alignment differs, it tries to use the
 * largest possible word copy, falling back to 4-byte or 2-byte copying if possible, or
 * byte copy otherwise.
 *
 * @param dst   Destination buffer pointer.
 * @param src   Source buffer pointer.
 * @param size  Number of bytes to copy.
 */
void LibXR::Memory::FastCopy(void* dst, const void* src, size_t size)
{
  uint8_t* d = static_cast<uint8_t*>(dst);
  const uint8_t* s = static_cast<const uint8_t*>(src);

  uintptr_t d_offset = reinterpret_cast<uintptr_t>(d) & (LIBXR_ALIGN_SIZE - 1);
  uintptr_t s_offset = reinterpret_cast<uintptr_t>(s) & (LIBXR_ALIGN_SIZE - 1);

  /**
   * If source and destination have the same alignment offset,
   * we can perform fast aligned copying after handling the leading bytes.
   */
  if (d_offset == s_offset)
  {
    /// Handle unaligned head bytes before reaching alignment.
    if (d_offset)
    {
      size_t head = LIBXR_ALIGN_SIZE - d_offset;
      if (head > size)
      {
        head = size;
      }
      while (head--)
      {
        *d++ = *s++;
        --size;
      }
    }

#if LIBXR_ALIGN_SIZE == 8
    /// Burst copy 8 bytes per cycle (64-bit), using 8x unrolled loop.
    auto* dw = reinterpret_cast<uint64_t*>(d);
    auto* sw = reinterpret_cast<const uint64_t*>(s);

    while (size >= 64)
    {
      dw[0] = sw[0];
      dw[1] = sw[1];
      dw[2] = sw[2];
      dw[3] = sw[3];
      dw[4] = sw[4];
      dw[5] = sw[5];
      dw[6] = sw[6];
      dw[7] = sw[7];
      dw += 8;
      sw += 8;
      size -= 64;
    }
    while (size >= 8)
    {
      *dw++ = *sw++;
      size -= 8;
    }

    d = reinterpret_cast<uint8_t*>(dw);
    s = reinterpret_cast<const uint8_t*>(sw);
#else
    /// Burst copy 4 bytes per cycle (32-bit), using 8x unrolled loop.
    auto* dw = reinterpret_cast<uint32_t*>(d);
    auto* sw = reinterpret_cast<const uint32_t*>(s);

    while (size >= 32)
    {
      dw[0] = sw[0];
      dw[1] = sw[1];
      dw[2] = sw[2];
      dw[3] = sw[3];
      dw[4] = sw[4];
      dw[5] = sw[5];
      dw[6] = sw[6];
      dw[7] = sw[7];
      dw += 8;
      sw += 8;
      size -= 32;
    }
    while (size >= 4)
    {
      *dw++ = *sw++;
      size -= 4;
    }

    d = reinterpret_cast<uint8_t*>(dw);
    s = reinterpret_cast<const uint8_t*>(sw);
#endif
  }
  /**
   * If alignments are different, try to maximize word copy size
   * based on address difference.
   */
  else
  {
    uintptr_t addr_diff = reinterpret_cast<uintptr_t>(s) - reinterpret_cast<uintptr_t>(d);

#if LIBXR_ALIGN_SIZE == 8
    /// If address difference is a multiple of 4, use 4-byte copying.
    if ((addr_diff & 3) == 0)
    {
      while ((reinterpret_cast<uintptr_t>(d) & 3) && size)
      {
        *d++ = *s++;
        --size;
      }
      auto* d32 = reinterpret_cast<uint32_t*>(d);
      auto* s32 = reinterpret_cast<const uint32_t*>(s);

      while (size >= 32)
      {
        d32[0] = s32[0];
        d32[1] = s32[1];
        d32[2] = s32[2];
        d32[3] = s32[3];
        d32[4] = s32[4];
        d32[5] = s32[5];
        d32[6] = s32[6];
        d32[7] = s32[7];
        d32 += 8;
        s32 += 8;
        size -= 32;
      }
      while (size >= 4)
      {
        *d32++ = *s32++;
        size -= 4;
      }

      d = reinterpret_cast<uint8_t*>(d32);
      s = reinterpret_cast<const uint8_t*>(s32);
    }
    /// If address difference is even, use 2-byte copying.
    else
#endif
        if ((addr_diff & 1) == 0)
    {
      if (reinterpret_cast<uintptr_t>(d) & 1)
      {
        *d++ = *s++;
        --size;
      }
      auto* d16 = reinterpret_cast<uint16_t*>(d);
      auto* s16 = reinterpret_cast<const uint16_t*>(s);

      while (size >= 16)
      {
        d16[0] = s16[0];
        d16[1] = s16[1];
        d16[2] = s16[2];
        d16[3] = s16[3];
        d16[4] = s16[4];
        d16[5] = s16[5];
        d16[6] = s16[6];
        d16[7] = s16[7];
        d16 += 8;
        s16 += 8;
        size -= 16;
      }
      while (size >= 2)
      {
        *d16++ = *s16++;
        size -= 2;
      }

      d = reinterpret_cast<uint8_t*>(d16);
      s = reinterpret_cast<const uint8_t*>(s16);
    }
    // Otherwise, fallback to byte-wise copying below.
  }

  /// Copy any remaining bytes (tail).
  while (size--)
  {
    *d++ = *s++;
  }
}
