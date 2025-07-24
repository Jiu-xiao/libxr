#pragma once

#include "libxr_def.hpp"
#include "main.h"
#include "stm32_usb.hpp"
#include "stm32_usb_dev.hpp"
#include "usb/ep.hpp"

namespace LibXR
{

class STM32Endpoint : public USB::Endpoint
{
 public:
  static constexpr uint8_t EP_MAX_SIZE =
      LibXR::max(USB_OTG_FS_MAX_IN_ENDPOINTS, USB_OTG_FS_MAX_OUT_ENDPOINTS);
  STM32Endpoint(EPNumber ep_num, stm32_usb_dev_id_t id, PCD_HandleTypeDef* hpcd,
                Direction dir, size_t fifo_size, LibXR::RawData buffer);

  bool Configure(const Config& cfg) override;
  void Close() override;
  ErrorCode Transfer(size_t size) override;

  ErrorCode Stall() override;
  ErrorCode ClearStall() override;

  size_t MaxTransferSize() const override;

  PCD_HandleTypeDef* hpcd_;
  size_t fifo_size_ = 0;
  stm32_usb_dev_id_t id_;

  static inline STM32Endpoint* map_[STM32_USB_DEV_ID_NUM][EP_MAX_SIZE][2] = {};
};

}  // namespace LibXR
