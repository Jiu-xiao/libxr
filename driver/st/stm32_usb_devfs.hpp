#pragma once

#include "double_buffer.hpp"
#include "main.h"
#include "stm32_usb_ep.hpp"
#include "stm32_usbx.hpp"
#include "usb/device_core.hpp"
#include "usb/endpoint_pool.hpp"

#if defined(HAL_PCD_MODULE_ENABLED)
namespace LibXR
{

class STM32USBDeviceFS : public LibXR::USB::EndpointPool, public LibXR::USB::DeviceCore
{
 public:
  STM32USBDeviceFS(PCD_HandleTypeDef* hpcd, size_t rx_endpoint_num, size_t rx_fifo_size,
                   std::initializer_list<size_t> tx_endpoint_fifo_configs,
                   USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid,
                   uint16_t pid, uint16_t bcd, uint8_t num_configs = 1,
                   uint8_t num_langs = 1)
      : LibXR::USB::EndpointPool(rx_endpoint_num + tx_endpoint_fifo_configs.size()),
        LibXR::USB::DeviceCore(this, USB::USBSpec::USB_2_0, USB::Speed::FULL, packet_size,
                               vid, pid, bcd, num_configs, num_langs),
        hpcd_(hpcd)
  {
    self_ = this;

    auto tx_fifo_config_itr = tx_endpoint_fifo_configs.begin();

    auto ep_0_in =
        new STM32Endpoint(0, hpcd, LibXR::USB::Endpoint::Direction::IN, rx_fifo_size);
    auto ep_0_out = new STM32Endpoint(0, hpcd, LibXR::USB::Endpoint::Direction::OUT,
                                      *tx_fifo_config_itr);
    SetEndpoint0(ep_0_in, ep_0_out);

    for (size_t i = 1; i < rx_endpoint_num; i++)
    {
      auto ep_in =
          new STM32Endpoint(i, hpcd, LibXR::USB::Endpoint::Direction::IN, rx_fifo_size);
      Put(ep_in);
    }

    size_t tx_endpoint_index = 0;

    for (; tx_fifo_config_itr != tx_endpoint_fifo_configs.end();
         ++tx_fifo_config_itr, ++tx_endpoint_index)
    {
      auto ep_out =
          new STM32Endpoint(rx_endpoint_num + tx_endpoint_index, hpcd,
                            LibXR::USB::Endpoint::Direction::OUT, *tx_fifo_config_itr);
      Put(ep_out);
    }
  }

  void Start() { HAL_PCD_Start(hpcd_); }
  void Stop() { HAL_PCD_Stop(hpcd_); }

  ErrorCode SetAddress(uint8_t address) override
  {
    auto ans = HAL_PCD_SetAddress(hpcd_, address);
    return (ans == HAL_OK) ? ErrorCode::OK : ErrorCode::FAILED;
  }

  PCD_HandleTypeDef* hpcd_;

  static inline STM32USBDeviceFS* self_ = nullptr;
};

}  // namespace LibXR
#endif
