#pragma once

#include "ch32_usb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "usb/core/ep.hpp"

namespace LibXR
{

// The CH32 OTG FS/HS endpoints still speak the pre-slot-model backend interface; they
// are migrated in step 5. Until then the legacy classes are only compiled when
// LIBXR_CH32_USB_OTG_LEGACY is set, so a part that also defines USBFSD (V203 has both
// the DevFS device and the OTG FS peripheral) can build the migrated DevFS path.
// Remove this switch with the step-5 migration.
#if (defined(USBFSD) && defined(LIBXR_CH32_USB_OTG_LEGACY)) || defined(__DOXYGEN__)

/**
 * @brief CH32 OTG FS 端点实现 / CH32 OTG FS endpoint implementation
 */
class CH32EndpointOtgFs : public USB::Endpoint
{
 public:
  CH32EndpointOtgFs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                    bool single_direction);

  void ConfigureHardware(const Config& cfg) override;
  void CloseHardware() override;
  ErrorCode StartHardware(RawData buffer, size_t size) override;

  void TransferComplete(size_t size);
  ErrorCode StallHardware() override;
  ErrorCode ClearStallHardware() override;

  bool tog_ = false;
  bool is_isochronous_ = false;
  bool single_direction_ = false;

  size_t last_transfer_size_ = 0;
  RawData dma_buffer_;
  RawData transfer_dma_{nullptr, 0};

  static constexpr uint8_t EP_OTG_FS_MAX_SIZE = 8;
  static inline CH32EndpointOtgFs* map_otg_fs_[EP_OTG_FS_MAX_SIZE][2] = {};
};

#endif  // defined(USBFSD) || defined(__DOXYGEN__)

#if (defined(USBHSD) && defined(LIBXR_CH32_USB_OTG_LEGACY)) || defined(__DOXYGEN__)

/**
 * @brief CH32 OTG HS 端点实现 / CH32 OTG HS endpoint implementation
 */
class CH32EndpointOtgHs : public USB::Endpoint
{
 public:
  CH32EndpointOtgHs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                    bool double_buffer);

  void ConfigureHardware(const Config& cfg) override;
  void CloseHardware() override;
  ErrorCode StartHardware(RawData buffer, size_t size) override;

  void TransferComplete(size_t size);
  ErrorCode StallHardware() override;
  ErrorCode ClearStallHardware() override;

  uint8_t dev_id_;
  bool tog0_ = false;
  bool tog1_ = false;

  size_t last_transfer_size_ = 0;
  RawData dma_buffer_;
  RawData transfer_dma_{nullptr, 0};

  static constexpr uint8_t EP_OTG_HS_MAX_SIZE = 16;
  static inline CH32EndpointOtgHs* map_otg_hs_[EP_OTG_HS_MAX_SIZE][2] = {};
};

#endif  // defined(USBHSD) || defined(__DOXYGEN__)

}  // namespace LibXR
