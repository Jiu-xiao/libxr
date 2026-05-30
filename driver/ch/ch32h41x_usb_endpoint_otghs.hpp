#pragma once

#include "ch32_usb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "usb/core/ep.hpp"

#if defined(USBHSD) && defined(LIBXR_CH32_IS_H41X)

#define LIBXR_CH32_USB_OTGHS_ENDPOINT_SPECIALIZED 1

namespace LibXR
{

class CH32H41xEndpointOtgHs : public USB::Endpoint
{
 public:
  CH32H41xEndpointOtgHs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
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
  static inline CH32H41xEndpointOtgHs* map_otg_hs_[EP_OTG_HS_MAX_SIZE][2] = {};
};

using CH32EndpointOtgHs = CH32H41xEndpointOtgHs;

}  // namespace LibXR

#endif
