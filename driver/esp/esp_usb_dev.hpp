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

  static constexpr uint8_t kEndpointCount = 7;
  static constexpr uint8_t kInEndpointLimit = 5;
  static constexpr uint32_t kInterruptDispatchGuard = 64U;
  static constexpr size_t kSetupPacketBytes = 8U;
  static constexpr size_t kSetupDmaBufferBytes = 64U;

  struct DebugTraceState
  {
    uint32_t bus_reset_count = 0;
    uint32_t enum_done_count = 0;
    uint32_t setup_data_count = 0;
    uint32_t setup_done_count = 0;
    uint32_t ep0_setup_irq_count = 0;
    uint32_t ep0_in_start_count = 0;
    uint32_t ep0_in_complete_count = 0;
    uint32_t ep0_out_start_count = 0;
    uint32_t ep0_out_complete_count = 0;
    uint32_t line_setup_irq_count = 0;
    uint32_t line_setup_xfer_overlap_count = 0;
    uint32_t line_out_arm_count = 0;
    uint32_t line_out_irq_count = 0;
    uint32_t line_out_cb_count = 0;
    uint32_t line_in_zlp_start_count = 0;
    uint32_t line_in_irq_count = 0;
    uint32_t line_in_manual_finish_count = 0;
    uint32_t last_line_doepint = 0;
    uint32_t last_line_diepint = 0;
    uint16_t last_ep0_in_size = 0;
    uint16_t last_ep0_out_size = 0;
    uint16_t last_line_out_actual = 0;
    uint8_t line_state = 0;
    uint8_t last_setup_packet[8] = {};
  };

  /**
   * @brief Direction-indexed endpoint pointers owned by the device core
   */
  struct EndpointMap
  {
    USB::Endpoint* in[kEndpointCount] = {};
    USB::Endpoint* out[kEndpointCount] = {};
  };

  /**
   * @brief DWC2 FIFO allocation state tracked across endpoint configuration
   */
  struct FifoState
  {
    uint16_t depth_words = 0U;
    uint16_t rx_words = 0U;
    uint16_t tx_next_words = 0U;
    uint16_t tx_words[kEndpointCount] = {};
    bool tx_bound[kEndpointCount] = {};
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
    bool core_inited = false;
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
  alignas(kSetupDmaBufferBytes) uint8_t setup_packet_[kSetupDmaBufferBytes] = {};
  // Internal debug trace retained only for in-tree endpoint/device code.
  DebugTraceState debug_ = {};
};

}  // namespace LibXR

#endif
