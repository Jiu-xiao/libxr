#include "libxr_mem.hpp"

#include "libxr_def.hpp"

LIBXR_FORCE_OPTIMIZE_O3 void LibXR::Memory::FastCopy(void* dst, const void* src, size_t size)
{
  uint8_t* d = static_cast<uint8_t*>(dst);
  const uint8_t* s = static_cast<const uint8_t*>(src);

  uintptr_t d_offset = reinterpret_cast<uintptr_t>(d) & (LibXR::ALIGN_SIZE - 1);
  uintptr_t s_offset = reinterpret_cast<uintptr_t>(s) & (LibXR::ALIGN_SIZE - 1);

  /**
   * If source and destination have the same alignment offset,
   * we can perform fast aligned copying after handling the leading bytes.
   */
  if (d_offset == s_offset)
  {
    /// Handle unaligned head bytes before reaching alignment.
    if (d_offset)
    {
      size_t head = LibXR::ALIGN_SIZE - d_offset;
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

    if constexpr (LibXR::ALIGN_SIZE == 8)
    {
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
    }
    else
    {
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
    }
  }
  /**
   * If alignments are different, try to maximize word copy size
   * based on address difference.
   */
  else
  {
    uintptr_t addr_diff = reinterpret_cast<uintptr_t>(s) - reinterpret_cast<uintptr_t>(d);

    if constexpr (LibXR::ALIGN_SIZE == 8)
    {
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
      else if ((addr_diff & 1) == 0 && size > 0)
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
    }
    else if ((addr_diff & 1) == 0 && size > 0)
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

LIBXR_FORCE_OPTIMIZE_O3 void LibXR::Memory::FastMove(void* dst, const void* src, size_t size)
{
  if (size == 0 || dst == src)
  {
    return;
  }

  auto* d = static_cast<uint8_t*>(dst);
  const auto* s = static_cast<const uint8_t*>(src);

  if (!(d < s + size && s < d + size))
  {
    FastCopy(dst, src, size);
    return;
  }

  if (d > s)
  {
    // Backward-overlap move: consume from the tail first so we never overwrite
    // bytes that still need to be read from the source window.
    uintptr_t d_end_offset =
        reinterpret_cast<uintptr_t>(d + size) & (LibXR::ALIGN_SIZE - 1);
    uintptr_t s_end_offset =
        reinterpret_cast<uintptr_t>(s + size) & (LibXR::ALIGN_SIZE - 1);

    d += size;
    s += size;

    if (d_end_offset == s_end_offset)
    {
      // Once both ends share the same alignment, we can peel the unaligned tail
      // bytes and then switch to wide backward copies safely.
      if (d_end_offset)
      {
        size_t tail = d_end_offset;
        if (tail > size)
        {
          tail = size;
        }
        while (tail--)
        {
          *--d = *--s;
          --size;
        }
      }

      if constexpr (LibXR::ALIGN_SIZE == 8)
      {
        auto* dw = reinterpret_cast<uint64_t*>(d);
        auto* sw = reinterpret_cast<const uint64_t*>(s);

        while (size >= 64)
        {
          uint64_t a0 = sw[-1];
          uint64_t a1 = sw[-2];
          uint64_t a2 = sw[-3];
          uint64_t a3 = sw[-4];
          uint64_t a4 = sw[-5];
          uint64_t a5 = sw[-6];
          uint64_t a6 = sw[-7];
          uint64_t a7 = sw[-8];
          dw[-1] = a0;
          dw[-2] = a1;
          dw[-3] = a2;
          dw[-4] = a3;
          dw[-5] = a4;
          dw[-6] = a5;
          dw[-7] = a6;
          dw[-8] = a7;
          dw -= 8;
          sw -= 8;
          size -= 64;
        }
        while (size >= 8)
        {
          uint64_t a = *--sw;
          *--dw = a;
          size -= 8;
        }

        d = reinterpret_cast<uint8_t*>(dw);
        s = reinterpret_cast<const uint8_t*>(sw);
      }
      else
      {
        auto* dw = reinterpret_cast<uint32_t*>(d);
        auto* sw = reinterpret_cast<const uint32_t*>(s);

        while (size >= 32)
        {
          uint32_t a0 = sw[-1];
          uint32_t a1 = sw[-2];
          uint32_t a2 = sw[-3];
          uint32_t a3 = sw[-4];
          uint32_t a4 = sw[-5];
          uint32_t a5 = sw[-6];
          uint32_t a6 = sw[-7];
          uint32_t a7 = sw[-8];
          dw[-1] = a0;
          dw[-2] = a1;
          dw[-3] = a2;
          dw[-4] = a3;
          dw[-5] = a4;
          dw[-6] = a5;
          dw[-7] = a6;
          dw[-8] = a7;
          dw -= 8;
          sw -= 8;
          size -= 32;
        }
        while (size >= 4)
        {
          uint32_t a = *--sw;
          *--dw = a;
          size -= 4;
        }

        d = reinterpret_cast<uint8_t*>(dw);
        s = reinterpret_cast<const uint8_t*>(sw);
      }
    }

    while (size--)
    {
      *--d = *--s;
    }
    return;
  }

  // Forward-overlap move: valid only when destination starts before source, so
  // consuming from the head cannot destroy unread source bytes.
  uintptr_t d_offset = reinterpret_cast<uintptr_t>(d) & (LibXR::ALIGN_SIZE - 1);
  uintptr_t s_offset = reinterpret_cast<uintptr_t>(s) & (LibXR::ALIGN_SIZE - 1);

  if (d_offset == s_offset)
  {
    // Same-alignment forward overlap can reuse the same "align head, then burst"
    // strategy as FastCopy because source bytes are always read before they are
    // overwritten.
    if (d_offset)
    {
      size_t head = LibXR::ALIGN_SIZE - d_offset;
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

    if constexpr (LibXR::ALIGN_SIZE == 8)
    {
      auto* dw = reinterpret_cast<uint64_t*>(d);
      auto* sw = reinterpret_cast<const uint64_t*>(s);

      while (size >= 64)
      {
        uint64_t a0 = sw[0];
        uint64_t a1 = sw[1];
        uint64_t a2 = sw[2];
        uint64_t a3 = sw[3];
        uint64_t a4 = sw[4];
        uint64_t a5 = sw[5];
        uint64_t a6 = sw[6];
        uint64_t a7 = sw[7];
        dw[0] = a0;
        dw[1] = a1;
        dw[2] = a2;
        dw[3] = a3;
        dw[4] = a4;
        dw[5] = a5;
        dw[6] = a6;
        dw[7] = a7;
        dw += 8;
        sw += 8;
        size -= 64;
      }
      while (size >= 8)
      {
        uint64_t a = *sw++;
        *dw++ = a;
        size -= 8;
      }

      d = reinterpret_cast<uint8_t*>(dw);
      s = reinterpret_cast<const uint8_t*>(sw);
    }
    else
    {
      auto* dw = reinterpret_cast<uint32_t*>(d);
      auto* sw = reinterpret_cast<const uint32_t*>(s);

      while (size >= 32)
      {
        uint32_t a0 = sw[0];
        uint32_t a1 = sw[1];
        uint32_t a2 = sw[2];
        uint32_t a3 = sw[3];
        uint32_t a4 = sw[4];
        uint32_t a5 = sw[5];
        uint32_t a6 = sw[6];
        uint32_t a7 = sw[7];
        dw[0] = a0;
        dw[1] = a1;
        dw[2] = a2;
        dw[3] = a3;
        dw[4] = a4;
        dw[5] = a5;
        dw[6] = a6;
        dw[7] = a7;
        dw += 8;
        sw += 8;
        size -= 32;
      }
      while (size >= 4)
      {
        uint32_t a = *sw++;
        *dw++ = a;
        size -= 4;
      }

      d = reinterpret_cast<uint8_t*>(dw);
      s = reinterpret_cast<const uint8_t*>(sw);
    }
  }

  while (size--)
  {
    *d++ = *s++;
  }
}

LIBXR_FORCE_OPTIMIZE_O3 void LibXR::Memory::FastSet(void* dst, uint8_t value, size_t size)
{
  if (size == 0)
  {
    return;
  }

  uint8_t* d = static_cast<uint8_t*>(dst);

  uintptr_t d_offset = reinterpret_cast<uintptr_t>(d) & (LibXR::ALIGN_SIZE - 1);

  // 先处理头部到对齐
  if (d_offset)
  {
    size_t head = LibXR::ALIGN_SIZE - d_offset;
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

  if constexpr (LibXR::ALIGN_SIZE == 8)
  {
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
  }
  else
  {
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
  }

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
  const uint8_t* mismatch_p = nullptr;
  const uint8_t* mismatch_q = nullptr;
  size_t mismatch_size = 0;

  if ((size == 0) || (p == q))
  {
    return 0;
  }

  uintptr_t p_off = reinterpret_cast<uintptr_t>(p) & (LibXR::ALIGN_SIZE - 1);
  uintptr_t q_off = reinterpret_cast<uintptr_t>(q) & (LibXR::ALIGN_SIZE - 1);

  // 若同相位：先补齐到 LibXR::ALIGN_SIZE 对齐再做宽比较
  if ((p_off == q_off) && (p_off != 0))
  {
    size_t head = LibXR::ALIGN_SIZE - p_off;
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

  if constexpr (LibXR::ALIGN_SIZE == 8)
  {
    // 8-byte compare（仅在两者均 8 对齐时才安全/快）
    if ((((reinterpret_cast<uintptr_t>(p) | reinterpret_cast<uintptr_t>(q)) & 7u) == 0u))
    {
      auto* pw = reinterpret_cast<const uint64_t*>(p);
      auto* qw = reinterpret_cast<const uint64_t*>(q);

      while (size >= 64)
      {
        if (pw[0] != qw[0])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 0);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 0);
          mismatch_size = 8;
          goto compare_fixed_bytes;
        }
        if (pw[1] != qw[1])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 1);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 1);
          mismatch_size = 8;
          goto compare_fixed_bytes;
        }
        if (pw[2] != qw[2])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 2);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 2);
          mismatch_size = 8;
          goto compare_fixed_bytes;
        }
        if (pw[3] != qw[3])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 3);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 3);
          mismatch_size = 8;
          goto compare_fixed_bytes;
        }
        if (pw[4] != qw[4])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 4);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 4);
          mismatch_size = 8;
          goto compare_fixed_bytes;
        }
        if (pw[5] != qw[5])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 5);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 5);
          mismatch_size = 8;
          goto compare_fixed_bytes;
        }
        if (pw[6] != qw[6])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 6);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 6);
          mismatch_size = 8;
          goto compare_fixed_bytes;
        }
        if (pw[7] != qw[7])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 7);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 7);
          mismatch_size = 8;
          goto compare_fixed_bytes;
        }

        pw += 8;
        qw += 8;
        size -= 64;
      }

      while (size >= 8)
      {
        if (*pw != *qw)
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw);
          mismatch_size = 8;
          goto compare_fixed_bytes;
        }
        ++pw;
        ++qw;
        size -= 8;
      }

      p = reinterpret_cast<const uint8_t*>(pw);
      q = reinterpret_cast<const uint8_t*>(qw);
    }
  }
  else
  {
    // 4-byte compare（两者均 4 对齐）
    if ((((reinterpret_cast<uintptr_t>(p) | reinterpret_cast<uintptr_t>(q)) & 3u) == 0u))
    {
      auto* pw = reinterpret_cast<const uint32_t*>(p);
      auto* qw = reinterpret_cast<const uint32_t*>(q);

      while (size >= 32)
      {
        if (pw[0] != qw[0])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 0);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 0);
          mismatch_size = 4;
          goto compare_fixed_bytes;
        }
        if (pw[1] != qw[1])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 1);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 1);
          mismatch_size = 4;
          goto compare_fixed_bytes;
        }
        if (pw[2] != qw[2])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 2);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 2);
          mismatch_size = 4;
          goto compare_fixed_bytes;
        }
        if (pw[3] != qw[3])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 3);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 3);
          mismatch_size = 4;
          goto compare_fixed_bytes;
        }
        if (pw[4] != qw[4])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 4);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 4);
          mismatch_size = 4;
          goto compare_fixed_bytes;
        }
        if (pw[5] != qw[5])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 5);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 5);
          mismatch_size = 4;
          goto compare_fixed_bytes;
        }
        if (pw[6] != qw[6])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 6);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 6);
          mismatch_size = 4;
          goto compare_fixed_bytes;
        }
        if (pw[7] != qw[7])
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw + 7);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw + 7);
          mismatch_size = 4;
          goto compare_fixed_bytes;
        }

        pw += 8;
        qw += 8;
        size -= 32;
      }

      while (size >= 4)
      {
        if (*pw != *qw)
        {
          mismatch_p = reinterpret_cast<const uint8_t*>(pw);
          mismatch_q = reinterpret_cast<const uint8_t*>(qw);
          mismatch_size = 4;
          goto compare_fixed_bytes;
        }
        ++pw;
        ++qw;
        size -= 4;
      }

      p = reinterpret_cast<const uint8_t*>(pw);
      q = reinterpret_cast<const uint8_t*>(qw);
    }
  }

compare_fixed_bytes:
  if (mismatch_size == 8)
  {
    int diff = static_cast<int>(mismatch_p[0]) - static_cast<int>(mismatch_q[0]);
    if (diff != 0)
    {
      return diff;
    }
    diff = static_cast<int>(mismatch_p[1]) - static_cast<int>(mismatch_q[1]);
    if (diff != 0)
    {
      return diff;
    }
    diff = static_cast<int>(mismatch_p[2]) - static_cast<int>(mismatch_q[2]);
    if (diff != 0)
    {
      return diff;
    }
    diff = static_cast<int>(mismatch_p[3]) - static_cast<int>(mismatch_q[3]);
    if (diff != 0)
    {
      return diff;
    }
    diff = static_cast<int>(mismatch_p[4]) - static_cast<int>(mismatch_q[4]);
    if (diff != 0)
    {
      return diff;
    }
    diff = static_cast<int>(mismatch_p[5]) - static_cast<int>(mismatch_q[5]);
    if (diff != 0)
    {
      return diff;
    }
    diff = static_cast<int>(mismatch_p[6]) - static_cast<int>(mismatch_q[6]);
    if (diff != 0)
    {
      return diff;
    }
    return static_cast<int>(mismatch_p[7]) - static_cast<int>(mismatch_q[7]);
  }

  if (mismatch_size == 4)
  {
    int diff = static_cast<int>(mismatch_p[0]) - static_cast<int>(mismatch_q[0]);
    if (diff != 0)
    {
      return diff;
    }
    diff = static_cast<int>(mismatch_p[1]) - static_cast<int>(mismatch_q[1]);
    if (diff != 0)
    {
      return diff;
    }
    diff = static_cast<int>(mismatch_p[2]) - static_cast<int>(mismatch_q[2]);
    if (diff != 0)
    {
      return diff;
    }
    return static_cast<int>(mismatch_p[3]) - static_cast<int>(mismatch_q[3]);
  }

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
