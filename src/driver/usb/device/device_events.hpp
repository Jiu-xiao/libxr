#pragma once
#include <cstdint>

namespace LibXR::USB
{
enum class DeviceEvent : uint8_t
{
  CONFIGURED,
  DECONFIGURED,
  RESET,
  DISCONNECTED,
  SUSPENDED,
  RESUMED,
  ENDPOINT_HALTED,
  ENDPOINT_RESUMED
};

struct BusTime
{
  uint16_t frame = 0xffff;  ///< 0xffff if this controller cannot expose the frame number.
  uint8_t microframe = 0xff;  ///< 0xff when the controller only reports full frames.
  uint32_t elapsed_intervals = 0;
  bool discontinuity = true;
};
}  // namespace LibXR::USB
