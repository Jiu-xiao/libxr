#pragma once

#include "libxr_def.hpp"
#include "main.h"
#include "stm32_usb.hpp"
#include "stm32_usb_dev.hpp"
#include "usb/core/ep.hpp"

#if defined(HAL_PCD_MODULE_ENABLED)

namespace LibXR
{

class STM32Endpoint : public USB::Endpoint
{
 public:
#if defined(USB_OTG_HS) || defined(USB_OTG_FS)
  STM32Endpoint(EPNumber ep_num, stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd,
                Direction dir, size_t fifo_size, LibXR::RawData buffer);
#endif
#if defined(USB_BASE)
  STM32Endpoint(EPNumber ep_num, stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd,
                Direction dir, size_t hw_buffer_offset, size_t hw_buffer_size,
                bool double_hw_buffer, LibXR::RawData buffer);
#endif

  void Configure(const Config& cfg) override;
  void Close() override;
  ErrorCode Transfer(size_t size) override;

  ErrorCode Stall() override;
  ErrorCode ClearStall() override;

  size_t MaxTransferSize() const override;

  PCD_HandleTypeDef* hpcd_;
#if defined(USB_OTG_FS) || defined(USB_OTG_HS)
  size_t fifo_size_ = 0;
#endif
#if defined(USB_BASE)
  size_t hw_buffer_size_ = 0;
  bool double_hw_buffer_ = false;
#endif
  stm32_usb_dev_id_t id_;

#if defined(USB_OTG_HS)
#if defined(USB_OTG_HS_MAX_IN_ENDPOINTS)
  static constexpr uint8_t EP_OTG_HS_MAX_SIZE =
      LibXR::max(USB_OTG_HS_MAX_IN_ENDPOINTS, USB_OTG_HS_MAX_OUT_ENDPOINTS);
#else
  static constexpr uint8_t EP_OTG_HS_MAX_SIZE = 9;
#endif

  static inline STM32Endpoint* map_hs_[EP_OTG_HS_MAX_SIZE][2] = {};
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
  static inline STM32Endpoint* map_fs_[EP_OTG_FS_MAX_SIZE][2] = {};
#endif

#if defined(USB_BASE)
  static constexpr uint8_t EP_OTG_FS_MAX_SIZE = 8;
  static inline STM32Endpoint* map_otg_fs_[EP_OTG_FS_MAX_SIZE][2] = {};
#endif
};

}  // namespace LibXR

#endif
