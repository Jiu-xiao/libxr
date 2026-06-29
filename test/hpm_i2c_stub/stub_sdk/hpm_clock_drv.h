#pragma once

#include <stdint.h>

typedef int clock_name_t;

enum
{
  clock_i2c0 = 0,
};

static inline void clock_add_to_group(clock_name_t clock, uint32_t group)
{
  (void)clock;
  (void)group;
}

static inline uint32_t clock_get_frequency(clock_name_t clock)
{
  (void)clock;
  return 24000000U;
}

static inline uint32_t clock_get_core_clock_ticks_per_us(void) { return 1U; }
