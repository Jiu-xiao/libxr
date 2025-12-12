#include "libxr_def.hpp"

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
    if ((addr_diff & 3) == 0 && size > 0)
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
        if ((addr_diff & 1) == 0 && size > 0)
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

void LibXR::Memory::FastSet(void* dst, uint8_t value, size_t size)
{
  if (size == 0)
  {
    return;
  }

  uint8_t* d = static_cast<uint8_t*>(dst);

  uintptr_t d_offset = reinterpret_cast<uintptr_t>(d) & (LIBXR_ALIGN_SIZE - 1);

  // 先处理头部到对齐
  if (d_offset)
  {
    size_t head = LIBXR_ALIGN_SIZE - d_offset;
    if (head > size)
    {
      head = size;
    }
    while (head--)
    {
      *d++ = value;
      --size;
    }
  }

#if LIBXR_ALIGN_SIZE == 8
  // 8-byte pattern
  uint64_t pat = value;
  pat |= pat << 8;
  pat |= pat << 16;
  pat |= pat << 32;

  auto* dw = reinterpret_cast<uint64_t*>(d);

  while (size >= 64)
  {
    dw[0] = pat;
    dw[1] = pat;
    dw[2] = pat;
    dw[3] = pat;
    dw[4] = pat;
    dw[5] = pat;
    dw[6] = pat;
    dw[7] = pat;
    dw += 8;
    size -= 64;
  }
  while (size >= 8)
  {
    *dw++ = pat;
    size -= 8;
  }

  d = reinterpret_cast<uint8_t*>(dw);
#else
  // 4-byte pattern
  uint32_t pat = value;
  pat |= pat << 8;
  pat |= pat << 16;

  auto* dw = reinterpret_cast<uint32_t*>(d);

  while (size >= 32)
  {
    dw[0] = pat;
    dw[1] = pat;
    dw[2] = pat;
    dw[3] = pat;
    dw[4] = pat;
    dw[5] = pat;
    dw[6] = pat;
    dw[7] = pat;
    dw += 8;
    size -= 32;
  }
  while (size >= 4)
  {
    *dw++ = pat;
    size -= 4;
  }

  d = reinterpret_cast<uint8_t*>(dw);
#endif

  // 尾巴
  while (size--)
  {
    *d++ = value;
  }
}

int LibXR::Memory::FastCmp(const void* a, const void* b, size_t size)
{
  const uint8_t* p = static_cast<const uint8_t*>(a);
  const uint8_t* q = static_cast<const uint8_t*>(b);

  if ((size == 0) || (p == q))
  {
    return 0;
  }

  auto byte_cmp = [](const uint8_t* x, const uint8_t* y, size_t n) -> int
  {
    for (size_t i = 0; i < n; ++i)
    {
      int diff = static_cast<int>(x[i]) - static_cast<int>(y[i]);
      if (diff != 0)
      {
        return diff;
      }
    }
    return 0;
  };

  uintptr_t p_off = reinterpret_cast<uintptr_t>(p) & (LIBXR_ALIGN_SIZE - 1);
  uintptr_t q_off = reinterpret_cast<uintptr_t>(q) & (LIBXR_ALIGN_SIZE - 1);

  // 若同相位：先补齐到 LIBXR_ALIGN_SIZE 对齐再做宽比较
  if ((p_off == q_off) && (p_off != 0))
  {
    size_t head = LIBXR_ALIGN_SIZE - p_off;
    if (head > size)
    {
      head = size;
    }

    while (head--)
    {
      int diff = static_cast<int>(*p++) - static_cast<int>(*q++);
      if (diff != 0)
      {
        return diff;
      }
      --size;
    }
  }

#if LIBXR_ALIGN_SIZE == 8
  // 8-byte compare（仅在两者均 8 对齐时才安全/快）
  if ((((reinterpret_cast<uintptr_t>(p) | reinterpret_cast<uintptr_t>(q)) & 7u) == 0u))
  {
    auto* pw = reinterpret_cast<const uint64_t*>(p);
    auto* qw = reinterpret_cast<const uint64_t*>(q);

    while (size >= 64)
    {
      if (pw[0] != qw[0])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 0),
                        reinterpret_cast<const uint8_t*>(qw + 0), 8);
      }
      if (pw[1] != qw[1])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 1),
                        reinterpret_cast<const uint8_t*>(qw + 1), 8);
      }
      if (pw[2] != qw[2])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 2),
                        reinterpret_cast<const uint8_t*>(qw + 2), 8);
      }
      if (pw[3] != qw[3])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 3),
                        reinterpret_cast<const uint8_t*>(qw + 3), 8);
      }
      if (pw[4] != qw[4])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 4),
                        reinterpret_cast<const uint8_t*>(qw + 4), 8);
      }
      if (pw[5] != qw[5])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 5),
                        reinterpret_cast<const uint8_t*>(qw + 5), 8);
      }
      if (pw[6] != qw[6])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 6),
                        reinterpret_cast<const uint8_t*>(qw + 6), 8);
      }
      if (pw[7] != qw[7])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 7),
                        reinterpret_cast<const uint8_t*>(qw + 7), 8);
      }

      pw += 8;
      qw += 8;
      size -= 64;
    }

    while (size >= 8)
    {
      if (*pw != *qw)
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw),
                        reinterpret_cast<const uint8_t*>(qw), 8);
      }
      ++pw;
      ++qw;
      size -= 8;
    }

    p = reinterpret_cast<const uint8_t*>(pw);
    q = reinterpret_cast<const uint8_t*>(qw);
  }
#else
  // 4-byte compare（两者均 4 对齐）
  if ((((reinterpret_cast<uintptr_t>(p) | reinterpret_cast<uintptr_t>(q)) & 3u) == 0u))
  {
    auto* pw = reinterpret_cast<const uint32_t*>(p);
    auto* qw = reinterpret_cast<const uint32_t*>(q);

    while (size >= 32)
    {
      if (pw[0] != qw[0])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 0),
                        reinterpret_cast<const uint8_t*>(qw + 0), 4);
      }
      if (pw[1] != qw[1])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 1),
                        reinterpret_cast<const uint8_t*>(qw + 1), 4);
      }
      if (pw[2] != qw[2])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 2),
                        reinterpret_cast<const uint8_t*>(qw + 2), 4);
      }
      if (pw[3] != qw[3])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 3),
                        reinterpret_cast<const uint8_t*>(qw + 3), 4);
      }
      if (pw[4] != qw[4])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 4),
                        reinterpret_cast<const uint8_t*>(qw + 4), 4);
      }
      if (pw[5] != qw[5])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 5),
                        reinterpret_cast<const uint8_t*>(qw + 5), 4);
      }
      if (pw[6] != qw[6])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 6),
                        reinterpret_cast<const uint8_t*>(qw + 6), 4);
      }
      if (pw[7] != qw[7])
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw + 7),
                        reinterpret_cast<const uint8_t*>(qw + 7), 4);
      }

      pw += 8;
      qw += 8;
      size -= 32;
    }

    while (size >= 4)
    {
      if (*pw != *qw)
      {
        return byte_cmp(reinterpret_cast<const uint8_t*>(pw),
                        reinterpret_cast<const uint8_t*>(qw), 4);
      }
      ++pw;
      ++qw;
      size -= 4;
    }

    p = reinterpret_cast<const uint8_t*>(pw);
    q = reinterpret_cast<const uint8_t*>(qw);
  }
#endif

  // tail byte compare（包括：未满足宽比较对齐条件的情况）
  while (size--)
  {
    int diff = static_cast<int>(*p++) - static_cast<int>(*q++);
    if (diff != 0)
    {
      return diff;
    }
  }

  return 0;
}
