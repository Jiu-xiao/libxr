#pragma once

#include "ch32_usb.hpp"
#include "libxr_def.hpp"
#include "usb/core/ep.hpp"

namespace LibXR
{

class CH32Endpoint : public USB::Endpoint
{
 public:
  CH32Endpoint(EPNumber ep_num, ch32_usb_dev_id_t dev_id, Direction dir,
               LibXR::RawData buffer);

  void Configure(const Config& cfg) override;
  void Close() override;
  ErrorCode Transfer(size_t size) override;

  void TransferComplete(size_t size);
  ErrorCode Stall() override;
  ErrorCode ClearStall() override;

  void SwitchBuffer() override;

  uint8_t dev_id_;
  bool tog_ = false;

#if defined(USBFSD)
  static constexpr uint8_t EP_DEV_FS_MAX_SIZE = 8;
  static inline CH32Endpoint* map_dev_[EP_DEV_FS_MAX_SIZE][2] = {};
#endif
};

}  // namespace LibXR
