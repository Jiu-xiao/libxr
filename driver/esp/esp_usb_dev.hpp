#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "esp_def.hpp"
#include "esp_intr_alloc.h"
#include "libxr_type.hpp"
#include "usb/core/ep_pool.hpp"
#include "usb/device/dev_core.hpp"

#if SOC_USB_OTG_SUPPORTED && defined(CONFIG_IDF_TARGET_ESP32S3) && \
    CONFIG_IDF_TARGET_ESP32S3

namespace LibXR
{

class ESP32USBEndpoint;

/**
 * @brief ESP32-S3 USB OTG device core implementation
 */
class ESP32USBDevice : public USB::EndpointPool, public USB::DeviceCore
{
 public:
  /**
   * @brief Endpoint configuration entry for ESP32-S3 USB device
   */
  struct EPConfig
  {
    /**
     * @brief How this config entry should populate endpoint directions
     */
    enum class DirectionHint : int8_t
    {
      BothDirections = -1,
      OutOnly = 0,
      InOnly = 1,
    };

    RawData buffer;
    DirectionHint direction_hint = DirectionHint::BothDirections;

    EPConfig() = delete;
    explicit EPConfig(RawData buffer) : buffer(buffer) {}
    EPConfig(RawData buffer, bool is_in)
        : buffer(buffer),
          direction_hint(is_in ? DirectionHint::InOnly : DirectionHint::OutOnly)
    {
    }
  };

  ESP32USBDevice(
      const std::initializer_list<EPConfig> ep_cfgs,
      USB::DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid,
      uint16_t bcd,
      const std::initializer_list<const USB::DescriptorStrings::LanguagePack*> lang_list,
      const std::initializer_list<const std::initializer_list<USB::ConfigDescriptorItem*>>
          configs,
      ConstRawData uid = {nullptr, 0});

  void Init(bool in_isr) override;
  void Deinit(bool in_isr) override;

  ErrorCode SetAddress(uint8_t address, USB::DeviceCore::Context context) override;
  void Start(bool in_isr) override;
  void Stop(bool in_isr) override;

 private:
  friend class ESP32USBEndpoint;

  static constexpr uint8_t ENDPOINT_COUNT = 7;
  static constexpr uint8_t IN_ENDPOINT_LIMIT = 5;
  static constexpr uint32_t INTERRUPT_DISPATCH_GUARD = 64U;
  static constexpr size_t SETUP_PACKET_BYTES = 8U;
  static constexpr size_t SETUP_DMA_BUFFER_BYTES = 64U;

  /**
   * @brief Minimal EP0 setup facts shared with the endpoint layer
   */
  struct ControlState
  {
    bool setup_direction_out = false;
  };

  /**
   * @brief Direction-indexed endpoint pointers owned by the device core
   */
  struct EndpointMap
  {
    USB::Endpoint* in[ENDPOINT_COUNT] = {};
    USB::Endpoint* out[ENDPOINT_COUNT] = {};
  };

  /**
   * @brief DWC2 FIFO allocation state tracked across endpoint configuration
   */
  struct FifoState
  {
    uint16_t depth_words = 0U;
    uint16_t rx_words = 0U;
    uint16_t tx_next_words = 0U;
    uint16_t tx_words[ENDPOINT_COUNT] = {};
    bool tx_bound[ENDPOINT_COUNT] = {};
    uint8_t allocated_in = 0U;
  };

  /**
   * @brief Runtime-owned resources and device-global state flags
   */
  struct RuntimeState
  {
    intr_handle_t intr_handle = nullptr;
    void* phy_handle = nullptr;
    bool phy_ready = false;
    bool irq_ready = false;
    bool started = false;
    bool rom_usb_cleaned = false;
  };

  static void IRAM_ATTR IsrEntry(void* arg);
  bool EnsurePhyReady();
  bool EnsureInterruptReady();
  void EnsureRomUsbCleaned();
  void InitializeCore();
  void ClearTxFifoRegisters();
  void FlushFifos();
  void ResetFifoState();
  void ResetDeviceState();
  void ResetEndpointHardwareState();
  void ReloadSetupPacketCount();
  void HandleInterrupt();
  void HandleBusReset(bool in_isr);
  void HandleEndpointInterrupt(bool in_isr, bool in_dir);
  void HandleRxFifoLevel();
  void ResetControlState();
  void UpdateSetupState(const uint8_t* setup);
  bool LastSetupDirectionOut() const { return control_.setup_direction_out; }
  bool DmaEnabled() const { return true; }

  bool AllocateTxFifo(uint8_t ep_num, uint16_t packet_size, bool is_bulk,
                      uint16_t& fifo_words);
  bool EnsureRxFifo(uint16_t packet_size);

  // Endpoint ownership map.
  EndpointMap endpoint_map_ = {};
  // FIFO sizing / allocation bookkeeping.
  FifoState fifo_state_ = {};
  // Runtime resource ownership and global flags.
  RuntimeState runtime_ = {};
  // Shared DMA-visible setup packet buffer.
  alignas(SETUP_DMA_BUFFER_BYTES) uint8_t setup_packet_[SETUP_DMA_BUFFER_BYTES] = {};
  // Functional EP0 setup state that must survive until status completion.
  ControlState control_ = {};
};

}  // namespace LibXR

#endif
