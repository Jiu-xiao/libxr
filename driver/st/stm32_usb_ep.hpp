#pragma once

#include "libxr_def.hpp"
#include "main.h"
#include "stm32_usb.hpp"
#include "stm32_usb_dev.hpp"
#include "usb/core/ep.hpp"

#if defined(HAL_PCD_MODULE_ENABLED)

namespace LibXR
{

/**
 * @brief STM32 USB 端点实现 / STM32 USB endpoint implementation
 *
 * 槽到 FIFO/PMA 的拷贝由 HAL 在中断中完成。停止在途请求时，OTG 的 IN 端点先置 SNAK 和
 * EPDIS，等 EPDISD 中断里 HAL 清空 TxFIFO 后确认；OUT 端点和 FSDEV 端点把 HAL 的写指针
 * 改到设备的暂存区，之后不再访问槽，立即确认。
 *
 * HAL copies between slots and the FIFO/PMA inside the interrupt. Stopping an in-flight
 * OTG IN request sets SNAK and EPDIS and is confirmed after HAL flushed the TxFIFO in the
 * EPDISD interrupt. OUT and FSDEV endpoints point HAL's write cursor at the device's
 * scratch area, never touch the slot again and confirm at once.
 */
class STM32Endpoint : public USB::Endpoint
{
 public:
#if defined(USB_OTG_HS) || defined(USB_OTG_FS)
  STM32Endpoint(EPNumber ep_num, stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd,
                Direction dir, size_t fifo_size, LibXR::RawData buffer);
#endif
#if defined(USB_BASE) || defined(USB_DRD_FS)
  STM32Endpoint(EPNumber ep_num, stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd,
                Direction dir, size_t hw_buffer_offset, size_t hw_buffer_size,
                LibXR::RawData buffer);
#endif

  /// 设置 OUT 停止后 HAL 写入的暂存区 / Set the scratch area HAL writes after an OUT stop
  static void SetScratch(stm32_usb_dev_id_t id, RawData scratch);
  /// 控制器中断批处理的前后钩子 / Hooks around one controller IRQ batch
  static void BeforeInterrupt(stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd)
  {
    if (redirect_mask_[id] != 0U) VisitRedirected(id, hpcd);
  }
  static void AfterInterrupt(stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd)
  {
    if (stop_mask_[id] != 0U) VisitStopping(id, hpcd);
  }
  static STM32Endpoint* Find(PCD_HandleTypeDef* hpcd, uint8_t epnum, bool is_in);
  /// 控制器已停止：确认所有等待中的停止 / The controller stopped: confirm every pending
  /// stop
  static void ForceStopAll(stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd, bool in_isr);

  /// HAL 回调：当前请求完成 / HAL callback: the current request finished
  void OnTransferDone(ErrorCode result);

 protected:
  ErrorCode ConfigureHardware(const Config& cfg, Granted& granted) override;
  void CloseHardware(bool in_isr) override;
  ErrorCode StartHardware(const HwRequest& request, bool in_isr) override;
  StopResult StopHardware(bool in_isr) override;
  size_t ReapProgress(const HwRequest& request, bool& retired) override;
  ErrorCode StallHardware(bool in_isr) override;
  ErrorCode ClearStallHardware(bool in_isr) override;
  size_t MaxPacketSizeLimit(Type type) const override;

 private:
  PCD_EPTypeDef* HalEndpoint() const;
  uint8_t HalAddress() const;
  bool IsIn() const { return GetConfig().direction == Direction::IN; }
  void Redirect();
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  void WriteFirstPacket(PCD_EPTypeDef* ep);
#endif
  static void VisitRedirected(stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd);
  static void VisitStopping(stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd);
  enum class StopStage : uint8_t
  {
    NONE,
    IN_NAK,          ///< SNAK issued; waiting for NAK effective.
    IN_DISABLE,      ///< EPDIS issued; waiting for EPDISD.
    OUT_GLOBAL_NAK,  ///< SGONAK issued; waiting for global OUT NAK effective.
    OUT_DISABLE      ///< EPDIS issued; waiting for EPDISD.
  };
  void SetStopStage(StopStage stage);
  void ArmStopTimeout();
  void ConfirmStop(bool in_isr);
  void SetRedirected(bool redirected);
  uint32_t MaskBit() const
  {
    return uint32_t{1} << (EPNumberToInt8(GetNumber()) + (IsIn() ? 16U : 0U));
  }

  PCD_HandleTypeDef* hpcd_;
  stm32_usb_dev_id_t id_;
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  size_t fifo_size_ = 0;
#endif
#if defined(USB_BASE) || defined(USB_DRD_FS)
  size_t hw_buffer_size_ = 0;
#endif
  // Written with the controller IRQ masked, read in that IRQ.
  uint32_t hw_tag_ = 0;
  size_t hw_length_ = 0;
  RawData hw_slot_{nullptr, 0};
  StopStage stop_stage_ = StopStage::NONE;
  bool redirected_ = false;    ///< HAL's OUT write cursor points at the scratch area.
  size_t stopped_actual_ = 0;  ///< OUT bytes in the slot when it was abandoned.

  static inline RawData scratch_[STM32_USB_DEV_ID_NUM] = {};
  // Per controller, bit n (OUT) / 16 + n (IN): endpoints the IRQ hooks must visit.
  // Changed with the controller IRQ masked or inside it.
  static inline uint32_t stop_mask_[STM32_USB_DEV_ID_NUM] = {};
  static inline uint32_t redirect_mask_[STM32_USB_DEV_ID_NUM] = {};

 public:
#if defined(USB_OTG_HS)
#if defined(USB_OTG_HS_MAX_IN_ENDPOINTS)
  static constexpr uint8_t EP_OTG_HS_MAX_SIZE =
      LibXR::max(USB_OTG_HS_MAX_IN_ENDPOINTS, USB_OTG_HS_MAX_OUT_ENDPOINTS);
#else
  static constexpr uint8_t EP_OTG_HS_MAX_SIZE = 9;
#endif

  static inline STM32Endpoint* map_otg_hs_[EP_OTG_HS_MAX_SIZE][2] = {};
#endif

#if defined(USB_OTG_FS)
#if defined(USB_OTG_FS_MAX_IN_ENDPOINTS)
  static constexpr uint8_t EP_OTG_FS_MAX_SIZE =
      LibXR::max(USB_OTG_FS_MAX_IN_ENDPOINTS, USB_OTG_FS_MAX_OUT_ENDPOINTS);
#elif defined(STM32H7) || defined(STM32N6)
  static constexpr uint8_t EP_OTG_FS_MAX_SIZE = 9;
#else
  static constexpr uint8_t EP_OTG_FS_MAX_SIZE = 6;
#endif
  static inline STM32Endpoint* map_otg_fs_[EP_OTG_FS_MAX_SIZE][2] = {};
#endif

#if defined(USB_BASE) || defined(USB_DRD_FS)
  static constexpr uint8_t EP_OTG_FS_MAX_SIZE = 8;
  static inline STM32Endpoint* map_fs_[EP_OTG_FS_MAX_SIZE][2] = {};
#endif
};

}  // namespace LibXR

#endif
