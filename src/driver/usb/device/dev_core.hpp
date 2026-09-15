#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "device_capabilities.hpp"
#include "device_features.hpp"
#include "timebase.hpp"

namespace LibXR::USB
{
/**
 * Event-driven USB device protocol engine. Capabilities are an explicit compile-
 * time policy; unsupported feature state and dispatch are not instantiated.
 * Class composition remains ordinary runtime composition, not a template tree.
 */
template <typename Capabilities = FullSpeedCapabilities>
class DeviceCore : private Detail::BosFeature<Capabilities::BOS>,
                   private Detail::BusTimeFeature<Capabilities::BUS_TIME>,
                   private Detail::HighSpeedFeature<(Capabilities::SPEEDS &
                                                     SpeedBit(Speed::HIGH)) != 0U>
{
 public:
  using Context = ControlContext;
  DeviceCore(
      EndpointPool& pool, USBSpec spec, Speed speed,
      DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid, uint16_t bcd,
      const std::initializer_list<const DescriptorStrings::LanguagePack*>& languages,
      const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
          configs,
      ConstRawData uid = {nullptr, 0});
  virtual ~DeviceCore() = default;
  DeviceCore(const DeviceCore&) = delete;
  DeviceCore& operator=(const DeviceCore&) = delete;

  virtual void Init(bool in_isr)
  {
    desired_enabled_.store(1U, std::memory_order_release);
    pool_.PostControl(INIT_EVENT, in_isr);
  }
  virtual void Deinit(bool in_isr)
  {
    desired_enabled_.store(0U, std::memory_order_release);
    pool_.PostControl(DEINIT_EVENT, in_isr);
  }
  virtual void Start(bool in_isr) = 0;
  virtual void Stop(bool in_isr) = 0;
  bool IsInited() const { return inited_.load(std::memory_order_acquire) != 0U; }
  void OnSetupPacket(bool in_isr, const SetupPacket* setup);
  void OnBusReset(bool in_isr)
  {
    desired_enabled_.store(1U, std::memory_order_release);
    requested_suspend_.store(0U, std::memory_order_release);
    pool_.PostControl(RESET_EVENT, in_isr);
  }
  void OnDisconnect(bool in_isr)
  {
    desired_enabled_.store(0U, std::memory_order_release);
    pool_.PostControl(DISCONNECT_EVENT, in_isr);
  }
  void OnSuspend(bool in_isr)
  {
    requested_suspend_.store(1U, std::memory_order_release);
    pool_.PostControl(POWER_EVENT, in_isr);
  }
  void OnResume(bool in_isr)
  {
    requested_suspend_.store(0U, std::memory_order_release);
    pool_.PostControl(POWER_EVENT, in_isr);
  }
  void OnSof(bool in_isr, uint16_t frame, uint8_t microframe = 0xff);
  bool WantsBusTime() const
  {
    if constexpr (Capabilities::BUS_TIME) return composition_.WantsBusTime();
    return false;
  }

 protected:
  virtual ErrorCode SetAddress(uint8_t address, Context context) = 0;
  virtual void EnableRemoteWakeup() {}
  virtual void DisableRemoteWakeup() {}
  virtual bool IsRemoteWakeupEnabled() const { return false; }
  /// Backend-specific software bookkeeping after physical reset, before EP0 open.
  virtual void OnControllerReset() {}
  virtual void OnControlReady() {}
  virtual Speed ReadBusSpeed() const { return speed_; }
  Speed GetSpeed() const { return speed_; }

 private:
  static constexpr uint32_t INIT_EVENT = 1U;
  static constexpr uint32_t DEINIT_EVENT = 2U;
  static constexpr uint32_t RESET_EVENT = 4U;
  static constexpr uint32_t DISCONNECT_EVENT = 8U;
  static constexpr uint32_t SETUP_EVENT = 16U;
  static constexpr uint32_t POWER_EVENT = 32U;
  static constexpr uint32_t TIME_EVENT = 64U;

  enum class Phase : uint8_t
  {
    IDLE,
    DATA_IN,
    DATA_OUT,
    STATUS_IN,
    STATUS_OUT,
    STALLED
  };
  void ProcessEvents(bool in_isr, uint32_t events);
  void Initialize(bool in_isr);
  void Deinitialize(bool in_isr);
  void HandleSetup(bool in_isr, const SetupPacket& setup);
  ErrorCode HandleStandardRequest(bool in_isr);
  ErrorCode ClassRequest(bool in_isr);
  ErrorCode SendDescriptor(bool in_isr);
  void WriteControl(ConstRawData data);
  void SendNextIn();
  void ReceiveNextOut();
  void StatusIn();
  void ArmStatusOut();
  void InComplete(bool in_isr, ConstRawData& data);
  void OutComplete(bool in_isr, ConstRawData& data);
  void Finish(bool in_isr);
  void Abort(bool in_isr);
  void Stall(bool in_isr);
  void DeliverBusTime(bool in_isr);
  bool ReadCapturedSetup(SetupPacket& setup) const;

  EndpointPool& pool_;
  DeviceComposition composition_;
  DeviceDescriptor device_desc_;
  Speed speed_;
  const uint16_t ep0_packet_size_;
  const uint32_t profile_speeds_;
  std::atomic<uint32_t> desired_enabled_{0};
  std::atomic<uint32_t> inited_{0};
  std::atomic<uint32_t> requested_suspend_{0};
  // IRQ-written fixed snapshot; atomic words avoid a C++ data race even when a
  // later SETUP replaces it while a different core copies the snapshot.
  std::atomic<uint32_t> setup_sequence_{0};
  std::atomic<uint32_t> setup_words_[2]{};
  uint32_t handled_setup_sequence_ = 0;
  SetupPacket setup_{};
  DeviceClass* control_class_ = nullptr;
  Phase phase_ = Phase::IDLE;
  RawData control_out_;
  RawData legacy_out_{nullptr, 0};
  ConstRawData control_in_{nullptr, 0};
  size_t transferred_ = 0;
  size_t in_segment_size_ = 0;
  bool in_tail_zero_ = false;
  bool status_out_armed_ = false;
  bool status_out_seen_ = false;
  bool replacing_setup_ = false;
  uint8_t pending_address_ = 0xff;
  Callback<ConstRawData&> in_callback_ = Callback<ConstRawData&>::Create(
      [](bool context, DeviceCore* self, ConstRawData& data)
      { self->InComplete(context, data); }, this);
  Callback<ConstRawData&> out_callback_ = Callback<ConstRawData&>::Create(
      [](bool context, DeviceCore* self, ConstRawData& data)
      { self->OutComplete(context, data); }, this);
  Callback<ErrorCode> error_callback_ = Callback<ErrorCode>::Create(
      [](bool context, DeviceCore* self, ErrorCode) { self->Stall(context); }, this);
  Endpoint* in0_ = nullptr;
  Endpoint* out0_ = nullptr;
};
}  // namespace LibXR::USB

#include "dev_core.tpp"
