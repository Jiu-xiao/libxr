#pragma once

#include "ch32_usb.hpp"
#include "libxr_def.hpp"
#include "usb/core/ep.hpp"

namespace LibXR
{
#if defined(USBFSD)
class CH32EndpointOtgFs : public USB::Endpoint
{
 public:
  CH32EndpointOtgFs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                    bool is_isochronous);

  void Configure(const Config& cfg) override;
  void Close() override;
  ErrorCode Transfer(size_t size) override;

  void TransferComplete(size_t size);
  ErrorCode Stall() override;
  ErrorCode ClearStall() override;

  void SwitchBuffer() override;

  bool tog_ = false;
  bool is_isochronous_ = false;

  size_t last_transfer_size_ = 0;

  RawData dma_buffer_;

  static constexpr uint8_t EP_OTG_FS_MAX_SIZE = 8;
  static inline CH32EndpointOtgFs* map_otg_fs_[EP_OTG_FS_MAX_SIZE][2] = {};
};
#endif

#if defined(USBHSD)
class CH32EndpointOtgHs : public USB::Endpoint
{
 public:
  CH32EndpointOtgHs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                    bool double_buffer);

  void Configure(const Config& cfg) override;
  void Close() override;
  ErrorCode Transfer(size_t size) override;

  void TransferComplete(size_t size);
  ErrorCode Stall() override;
  ErrorCode ClearStall() override;

  void SwitchBuffer() override;

  uint8_t dev_id_;
  bool tog0_ = false;
  bool tog1_ = false;
  bool hw_double_buffer_ = false;

  size_t last_transfer_size_ = 0;

  RawData dma_buffer_;

  static constexpr uint8_t EP_OTG_HS_MAX_SIZE = 16;
  static inline CH32EndpointOtgHs* map_otg_hs_[EP_OTG_HS_MAX_SIZE][2] = {};
};
#endif
}  // namespace LibXR
