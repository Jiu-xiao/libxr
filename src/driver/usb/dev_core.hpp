#pragma once
#include <cstring>

#include "core.hpp"
#include "desc_cfg.hpp"
#include "ep_pool.hpp"

namespace LibXR::USB
{

class DeviceClass : public ConfigDescriptorItem
{
 public:
  struct RequestResult
  {
    RawData read_data{nullptr, 0};
    ConstRawData write_data{nullptr, 0};
    bool read_zlp = false;
    bool write_zlp = false;
  };

  virtual ErrorCode OnClassRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                   uint16_t wLength, RequestResult &result)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(wValue);
    UNUSED(wLength);
    UNUSED(result);
    return ErrorCode::NOT_SUPPORT;
  }

  virtual ErrorCode OnClassData(bool in_isr, uint8_t bRequest, LibXR::ConstRawData &data)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(data);
    return ErrorCode::NOT_SUPPORT;
  }
};

class DeviceCore
{
 public:
  enum Context : uint8_t
  {
    UNKNOW,
    SETUP,
    DATA_OUT,
    STATUS_OUT,
    DATA_IN,
    STATUS_IN,
    ZLP
  };

  DeviceCore(
      EndpointPool &ep_pool, USBSpec spec, Speed speed,
      DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid, uint16_t bcd,
      const std::initializer_list<const DescriptorStrings::LanguagePack *> &lang_list,
      const std::initializer_list<const std::initializer_list<ConfigDescriptorItem *>>
          &configs);

  virtual void Init();

  void OnSetupPacket(bool in_isr, const SetupPacket *setup);

 private:
  static void OnEP0OutCompleteStatic(bool in_isr, DeviceCore *self,
                                     LibXR::ConstRawData &data);

  static void OnEP0InCompleteStatic(bool in_isr, DeviceCore *self,
                                    LibXR::ConstRawData &data);

  static bool IsValidUSBCombination(USBSpec spec, Speed speed,
                                    DeviceDescriptor::PacketSize0 packet_size);

  void OnEP0OutComplete(bool in_isr, LibXR::ConstRawData &data);

  void OnEP0InComplete(bool in_isr, LibXR::ConstRawData &data);

  void ReadZLP();

  void WriteZLP();

  void DevWriteEP0Data(LibXR::ConstRawData data, size_t packet_max_length,
                       size_t request_size = 0);

  void DevReadEP0Data(LibXR::RawData data, size_t packet_max_length);

  ErrorCode ProcessStandardRequest(bool in_isr, const SetupPacket *&setup,
                                   RequestDirection direction, Recipient recipient);

  ErrorCode RespondWithStatus(const SetupPacket *setup, Recipient recipient);

  ErrorCode ClearFeature(const SetupPacket *setup, Recipient recipient);

  ErrorCode ApplyFeature(const SetupPacket *setup, Recipient recipient);

  ErrorCode SendDescriptor(const SetupPacket *setup);

  ErrorCode PrepareAddressChange(uint16_t address);

  ErrorCode SwitchConfiguration(uint16_t value);

  ErrorCode SendConfiguration();

  void StallControlEndpoint();

  void ClearControlEndpointStall();

  ErrorCode ProcessClassRequest(bool in_isr, const SetupPacket *setup,
                                RequestDirection direction, Recipient recipient);

  ErrorCode ProcessVendorRequest(bool in_isr, const SetupPacket *&setup,
                                 RequestDirection direction, Recipient recipient);

  virtual ErrorCode SetAddress(uint8_t address, Context state) = 0;

  Speed GetSpeed() const;

 private:
  ConfigDescriptor config_desc_;
  DeviceDescriptor device_desc_;
  DescriptorStrings strings_;

  struct
  {
    EndpointPool &pool;
    Endpoint *in0 = nullptr;
    Endpoint *out0 = nullptr;
  } endpoint_;

  struct
  {
    Speed speed = Speed::FULL;
    Context in0;
    Context out0;
    ConstRawData write_remain;
    RawData read_remain;
    uint8_t pending_addr = 0xFF;
    uint8_t *out0_buffer = nullptr;
    bool need_write_zlp = false;
  } state_;

  struct
  {
    bool write = false;
    bool read = false;
    DeviceClass *class_ptr = nullptr;
    uint8_t b_request = 0;
  } class_req_;
};
}  // namespace LibXR::USB
