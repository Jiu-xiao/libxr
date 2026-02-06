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

// CH32 classic USB device (non-OTG, FSDEV / PMA)
// NOTE: FSDEV is selected by the presence of RCC_APB1Periph_USB (classic USB device clock)
// and excluded when HS OTG (USBHSD) exists.
#if defined(RCC_APB1Periph_USB) && !defined(USBHSD)
class CH32EndpointDevFs : public USB::Endpoint
{
 public:
  CH32EndpointDevFs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                    bool is_isochronous);

  void Configure(const Config& cfg) override;
  void Close() override;
  ErrorCode Transfer(size_t size) override;

  void TransferComplete(size_t size);

  // 用于 SETUP 包：只把 PMA 数据搬到 endpoint buffer，不触发回调
  void CopyRxDataToBuffer(size_t size);

  ErrorCode Stall() override;
  ErrorCode ClearStall() override;

  void SwitchBuffer() override;

  // 供 USB ISR 使用的一组轻量静态操作
  static void ResetPMAAllocator();
  static void SetEpTxStatus(uint8_t ep, uint16_t status);
  static void SetEpRxStatus(uint8_t ep, uint16_t status);
  static void ClearEpCtrTx(uint8_t ep);
  static void ClearEpCtrRx(uint8_t ep);
  static uint16_t GetRxCount(uint8_t ep);

  bool is_isochronous_ = false;

  size_t last_transfer_size_ = 0;

  // PMA 缓冲区地址（BTABLE 中存的是该地址），单位与 demo USBLIB 一致
  uint16_t pma_addr_ = 0;

  static constexpr uint8_t EP_DEV_FS_MAX_SIZE = 8;
  static inline CH32EndpointDevFs* map_dev_fs_[EP_DEV_FS_MAX_SIZE][2] = {};
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
