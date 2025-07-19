#pragma once

#include "libxr_def.hpp"
#include "main.h"
#include "usb/endpoint.hpp"

namespace LibXR
{

class STM32Endpoint : public USB::Endpoint
{
 public:
  static constexpr uint8_t EP_MAX_SIZE =
      LibXR::max(USB_OTG_FS_MAX_IN_ENDPOINTS, USB_OTG_FS_MAX_OUT_ENDPOINTS);
  STM32Endpoint(uint8_t ep_num, PCD_HandleTypeDef* hpcd, Direction dir, size_t fifo_size);

  bool Configure(const Config& cfg) override;
  void Close() override;
  ErrorCode Write(ConstRawData& data) override;
  ErrorCode Read(RawData& data) override;
  ErrorCode Stall() override;
  ErrorCode ClearStall() override;

  // ISR事件入口（供C回调调用）
  void OnDataInISR();
  void OnDataOutISR();

  PCD_HandleTypeDef* hpcd_;
  static inline STM32Endpoint* map_[EP_MAX_SIZE][2] = {};
};

}  // namespace LibXR
