#pragma once

#include "ch32_usb.hpp"
#include "ch32h41x_usb_endpoint_otghs.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "usb/core/ep.hpp"

namespace LibXR
{

#if defined(USBFSD)

/**
 * @brief CH32 OTG FS 端点实现 / CH32 OTG FS endpoint implementation
 */
class CH32EndpointOtgFs : public USB::Endpoint
{
 public:
  CH32EndpointOtgFs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                    bool single_direction);

  void Configure(const Config& cfg) override;
  void Close() override;
  ErrorCode Transfer(size_t size) override;

  void TransferComplete(size_t size);
  ErrorCode Stall() override;
  ErrorCode ClearStall() override;

  void SwitchBuffer() override;

  bool tog_ = false;
  bool is_isochronous_ = false;
  bool single_direction_ = false;

  size_t last_transfer_size_ = 0;
  RawData dma_buffer_;

  static constexpr uint8_t EP_OTG_FS_MAX_SIZE = 8;
  static inline CH32EndpointOtgFs* map_otg_fs_[EP_OTG_FS_MAX_SIZE][2] = {};
};

#endif  // defined(USBFSD)

#if defined(RCC_APB1Periph_USB)

/**
 * @brief CH32 FSDEV 端点实现 / CH32 FSDEV endpoint implementation
 */
class CH32EndpointDevFs : public USB::Endpoint
{
 public:
  CH32EndpointDevFs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                    bool is_isochronous);

  void Configure(const Config& cfg) override;
  void Close() override;
  ErrorCode Transfer(size_t size) override;

  void TransferComplete(size_t size);

  void CopyRxDataToBuffer(size_t size);

  ErrorCode Stall() override;
  ErrorCode ClearStall() override;

  void SwitchBuffer() override;

  static void ResetPMAAllocator();
  static void SetEpTxStatus(uint8_t ep, uint16_t status);
  static void SetEpRxStatus(uint8_t ep, uint16_t status);
  static void ClearEpCtrTx(uint8_t ep);
  static void ClearEpCtrRx(uint8_t ep);
  static uint16_t GetRxCount(uint8_t ep);

  bool is_isochronous_ = false;

  size_t last_transfer_size_ = 0;

  uint16_t pma_addr_ = 0;

  static constexpr uint8_t EP_DEV_FS_MAX_SIZE = 8;
  static inline CH32EndpointDevFs* map_dev_fs_[EP_DEV_FS_MAX_SIZE][2] = {};
};

#endif  // defined(RCC_APB1Periph_USB)

#if defined(USBHSD)

/**
 * @brief CH32 OTG HS 端点实现 / CH32 OTG HS endpoint implementation
 */
#if !defined(LIBXR_CH32_USB_OTGHS_ENDPOINT_SPECIALIZED)
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

#endif  // defined(USBHSD)

#if defined(LIBXR_CH32_HAS_USB_OTG_SS)

/**
 * @brief CH32 OTG SS 端点实现 / CH32 OTG SS endpoint implementation
 */
class CH32EndpointOtgSs : public USB::Endpoint
{
 public:
  CH32EndpointOtgSs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                    uint8_t max_burst = 1);

  void Configure(const Config& cfg) override;
  void Close() override;
  size_t MaxTransferSize() const override;
  uint8_t MaxBurst() const override;
  ErrorCode Transfer(size_t size) override;

  void TransferComplete(size_t size);
  ErrorCode Stall() override;
  ErrorCode ClearStall() override;
  void SwitchBuffer() override;

  uint8_t seq_ = 0u;
  uint8_t max_burst_ = 1u;
  RawData dma_buffer_;
  size_t bank_size_ = 0u;
  uint8_t active_bank_ = 0u;
  size_t last_transfer_size_ = 0u;

  static constexpr uint8_t EP_OTG_SS_MAX_SIZE = 8;
  static inline CH32EndpointOtgSs* map_otg_ss_[EP_OTG_SS_MAX_SIZE][2] = {};
};

#endif  // defined(LIBXR_CH32_HAS_USB_OTG_SS)

}  // namespace LibXR
