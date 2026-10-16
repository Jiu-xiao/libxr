#pragma once

#include "ch32_usb.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "usb/core/ep.hpp"

namespace LibXR
{

#if defined(USBFSD) || defined(__DOXYGEN__)

/**
 * @brief CH32 OTG FS 端点实现 / CH32 OTG FS endpoint implementation
 */
class CH32EndpointOtgFs : public USB::Endpoint
{
 public:
  CH32EndpointOtgFs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                    bool single_direction);

  ErrorCode ConfigureHardware(const Config& cfg, Granted& granted) override;
  void CloseHardware(bool in_isr) override;
  ErrorCode StartHardware(const HwRequest& request, bool in_isr) override;
  StopResult StopHardware(bool in_isr) override;
  size_t ReapProgress(const HwRequest& request, bool& retired) override;

  void TransferComplete(size_t size);
  ErrorCode StallHardware(bool in_isr) override;
  ErrorCode ClearStallHardware(bool in_isr) override;
  size_t MaxPacketSizeLimit(Type type) const override;

  bool tog_ = false;
  bool is_isochronous_ = false;
  bool single_direction_ = false;

  size_t last_transfer_size_ = 0;
  RawData dma_buffer_;
  RawData transfer_dma_{nullptr, 0};

  uint32_t transfer_tag_ = 0;
  bool transfer_active_ = false;
  // 硬件是否为本请求报过完成（ReapProgress 据此判断能否算成功）。
  // Whether the hardware reported a completion for this request (ReapProgress
  // uses it to decide whether the request can count as successful).
  bool hw_completed_ = false;
  size_t transfer_requested_ = 0;
  size_t transfer_accumulated_ = 0;
  uint8_t* transfer_cursor_ = nullptr;
  // IN 分包：剩余未发出的字节数（0 表示本请求只剩最后一包）。
  // IN splitting: bytes of the request not sent yet (0 = last packet in flight).
  size_t transfer_remaining_ = 0;
  // 共享双向端点的块基址：硬件 OUT 用 [base, base+64)，IN 固定用 [base+64,
  // base+128)，UEPn_DMA 必须一直是 base。IN 端点对象从构造参数拿到的是
  // base+64（TX 列表项），故这里回退 64 得到块基址。
  // Block base of a shared bidirectional endpoint: the hardware uses [base,
  // base+64) for OUT and always [base+64, base+128) for IN, so UEPn_DMA must
  // stay at base. The IN endpoint object is given base+64 (its TX list entry),
  // so step back to the block base.
  uint8_t* block_dma_base_ = nullptr;

  // IRQ 分发（自由函数）需要经端点对象读取槽内存（EP0 setup 包）。
  // The IRQ dispatch (free functions) reads slot memory through the endpoint
  // objects (the EP0 setup packet).
  using USB::Endpoint::SlotMemory;

  static constexpr uint8_t EP_OTG_FS_MAX_SIZE = 8;
  static constexpr size_t SHARED_BLOCK_HALF = 64U;
  static inline CH32EndpointOtgFs* map_otg_fs_[EP_OTG_FS_MAX_SIZE][2] = {};
};

#endif  // defined(USBFSD) || defined(__DOXYGEN__)

#if defined(RCC_APB1Periph_USB) || defined(__DOXYGEN__)

/**
 * @brief CH32 FSDEV 端点实现 / CH32 FSDEV endpoint implementation
 */
class CH32EndpointDevFs : public USB::Endpoint
{
 public:
  CH32EndpointDevFs(EPNumber ep_num, Direction dir, LibXR::RawData buffer,
                    bool is_isochronous);

  // DevFS 属第 4 步（共用 FSDEV 运行期），本分支不迁移：仅做最小编译适配（新
  // 签名 + 占位实现），V307 的库构建会编译本文件；V203 的 USB 功能不可用是
  // 预期状态，功能迁移由第 4 步交付。
  // DevFS belongs to step 4 (shared FSDEV runtime) and is NOT migrated on this
  // branch: only a minimal compile adaptation (new signatures + placeholder
  // implementations) so the V307 library build compiles this file. V203 USB
  // functionality staying unavailable is expected; the real migration ships with
  // step 4.
  ErrorCode ConfigureHardware(const Config& cfg, Granted& granted) override;
  void CloseHardware(bool in_isr) override;
  ErrorCode StartHardware(const HwRequest& request, bool in_isr) override;
  USB::Endpoint::StopResult StopHardware(bool) override { return StopResult::DONE; }
  size_t ReapProgress(const HwRequest&, bool& retired) override
  {
    retired = true;
    return 0;
  }
  size_t MaxPacketSizeLimit(Type) const override { return 64U; }

  void TransferComplete(size_t size);

  void CopyRxDataToBuffer(size_t size);

  ErrorCode StallHardware(bool in_isr) override;
  ErrorCode ClearStallHardware(bool in_isr) override;

  // FSDEV 设备层（ch32_usb_devfs.cpp）需要从类外读取 EP0 槽内存来取 setup 包。
  // The FSDEV device layer (ch32_usb_devfs.cpp) reads the EP0 slot memory from
  // outside the class to fetch the setup packet.
  using USB::Endpoint::SlotMemory;

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

#endif  // defined(RCC_APB1Periph_USB) || defined(__DOXYGEN__)

#if defined(USBHSD) || defined(__DOXYGEN__)

/**
 * @brief CH32 OTG HS 端点实现 / CH32 OTG HS endpoint implementation
 */
class CH32EndpointOtgHs : public USB::Endpoint
{
 public:
  CH32EndpointOtgHs(EPNumber ep_num, Direction dir, LibXR::RawData buffer);

  ErrorCode ConfigureHardware(const Config& cfg, Granted& granted) override;
  void CloseHardware(bool in_isr) override;
  ErrorCode StartHardware(const HwRequest& request, bool in_isr) override;
  StopResult StopHardware(bool in_isr) override;
  size_t ReapProgress(const HwRequest& request, bool& retired) override;

  void TransferComplete(size_t size);
  ErrorCode StallHardware(bool in_isr) override;
  ErrorCode ClearStallHardware(bool in_isr) override;
  size_t MaxPacketSizeLimit(Type type) const override;

  uint8_t dev_id_;
  bool tog0_ = false;
  bool tog1_ = false;

  size_t last_transfer_size_ = 0;
  RawData dma_buffer_;
  RawData transfer_dma_{nullptr, 0};

  uint32_t transfer_tag_ = 0;
  bool transfer_active_ = false;
  // 硬件是否为本请求报过完成（ReapProgress 据此判断能否算成功）。
  // Whether the hardware reported a completion for this request (ReapProgress
  // uses it to decide whether the request can count as successful).
  bool hw_completed_ = false;
  size_t transfer_requested_ = 0;
  size_t transfer_accumulated_ = 0;
  uint8_t* transfer_cursor_ = nullptr;
  // IN 分包：剩余未发出的字节数（0 表示本请求只剩最后一包）。
  // IN splitting: bytes of the request not sent yet (0 = last packet in flight).
  size_t transfer_remaining_ = 0;

  // 同 OtgFs：IRQ 分发（自由函数）经端点对象读取槽内存。
  // Same as OtgFs: the IRQ dispatch (free functions) reads slot memory through
  // the endpoint objects.
  using USB::Endpoint::SlotMemory;

  static constexpr uint8_t EP_OTG_HS_MAX_SIZE = 16;
  static inline CH32EndpointOtgHs* map_otg_hs_[EP_OTG_HS_MAX_SIZE][2] = {};
};

#endif  // defined(USBHSD) || defined(__DOXYGEN__)

}  // namespace LibXR
