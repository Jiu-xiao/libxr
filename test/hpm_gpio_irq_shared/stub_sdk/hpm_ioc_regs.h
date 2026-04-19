#pragma once

#include <cstdint>

constexpr uint32_t HPM_TEST_PAD_COUNT = 128u;

struct IOC_PAD_Type
{
  uint32_t FUNC_CTL = 0u;
  uint32_t PAD_CTL = 0u;
};

struct IOC_Type
{
  IOC_PAD_Type PAD[HPM_TEST_PAD_COUNT] = {};
};

using PIOC_Type = IOC_Type;

constexpr uint16_t IOC_PAD_PA00 = 0u;
constexpr uint16_t IOC_PAD_PB00 = 32u;
constexpr uint16_t IOC_PAD_PX00 = 64u;
constexpr uint16_t IOC_PAD_PY00 = 72u;

#define IOC_PAD_FUNC_CTL_ALT_SELECT_SET(value) (static_cast<uint32_t>(value) & 0xFu)
#define IOC_PAD_FUNC_CTL_LOOP_BACK_MASK (1u << 8u)
#define IOC_PAD_FUNC_CTL_ANALOG_MASK (1u << 9u)

#define IOC_PAD_PAD_CTL_PE_MASK (1u << 0u)
#define IOC_PAD_PAD_CTL_PS_MASK (1u << 1u)
#define IOC_PAD_PAD_CTL_OD_MASK (1u << 2u)

#define IOC_PAD_PAD_CTL_PE_SET(value) ((static_cast<uint32_t>(value) & 0x1u) << 0u)
#define IOC_PAD_PAD_CTL_PS_SET(value) ((static_cast<uint32_t>(value) & 0x1u) << 1u)
#define IOC_PAD_PAD_CTL_OD_SET(value) ((static_cast<uint32_t>(value) & 0x1u) << 2u)
