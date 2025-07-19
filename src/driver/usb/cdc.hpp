#pragma once
#include <cstring>

#include "descriptor.hpp"
#include "device_core.hpp"
#include "endpoint_pool.hpp"

namespace LibXR::USB
{

class CDC : public ConfigDescriptorItem
{
  enum class CDCDescriptorSubtype : uint8_t
  {
    HEADER = 0x00,
    CALL_MANAGEMENT = 0x01,
    ACM = 0x02,
    UNION = 0x06,
  };

  enum class CDCClass : uint8_t
  {
    COMM = 0x02,
    DATA = 0x0A
  };

  enum class Protocol : uint8_t
  {
    AT_COMMAND = 0x01,
  };

  enum class Subclass : uint8_t
  {
    ABSTRACT_CONTROL_MODEL = 0x02
  };

 public:
  CDC(uint16_t data_packet_size = 64, size_t data_in_ep_num = 0,
      size_t data_out_ep_num = 0, size_t comm_ep_num = 0)
      : packet_size_(data_packet_size),
        data_in_ep_num_(data_in_ep_num),
        data_out_ep_num_(data_out_ep_num),
        comm_ep_num_(comm_ep_num)
  {
  }

  void Init(EndpointPool* endpoint_pool) override
  {
    auto ans = endpoint_pool->Get(ep_data_in_, Endpoint::Direction::IN, data_in_ep_num_);
    ASSERT(ans == ErrorCode::OK);
    ans = endpoint_pool->Get(ep_data_out_, Endpoint::Direction::OUT, data_out_ep_num_);
    ASSERT(ans == ErrorCode::OK);
    ans = endpoint_pool->Get(ep_comm_in_, Endpoint::Direction::IN, comm_ep_num_);
    ASSERT(ans == ErrorCode::OK);

    ep_data_in_->Configure({Endpoint::Direction::IN, Endpoint::Type::BULK, packet_size_});
    ep_data_out_->Configure(
        {Endpoint::Direction::OUT, Endpoint::Type::BULK, packet_size_});
    ep_comm_in_->Configure({Endpoint::Direction::IN, Endpoint::Type::INTERRUPT, 8});

    // === 填充描述符 ===
    static constexpr uint8_t COMM_INTERFACE = 0;
    static constexpr uint8_t DATA_INTERFACE = 1;

    // IAD 描述符
    desc_block_.iad = {8,
                       static_cast<uint8_t>(DescriptorType::IAD),
                       COMM_INTERFACE,
                       2,
                       static_cast<uint8_t>(CDCClass::COMM),
                       static_cast<uint8_t>(Subclass::ABSTRACT_CONTROL_MODEL),
                       static_cast<uint8_t>(Protocol::AT_COMMAND),
                       0};

    // Comm Interface 描述符
    desc_block_.comm_intf = {9,
                             static_cast<uint8_t>(DescriptorType::INTERFACE),
                             COMM_INTERFACE,
                             0,
                             1,
                             static_cast<uint8_t>(CDCClass::COMM),
                             static_cast<uint8_t>(Subclass::ABSTRACT_CONTROL_MODEL),
                             static_cast<uint8_t>(Protocol::AT_COMMAND),
                             0};

    // Class-specific: Header
    desc_block_.cdc_header = {5, DescriptorType::CS_INTERFACE,
                              CDCDescriptorSubtype::HEADER, 0x0110};

    // Call Management
    desc_block_.cdc_callmgmt = {5, DescriptorType::CS_INTERFACE,
                                CDCDescriptorSubtype::CALL_MANAGEMENT,
                                0x00,  // 无 DTE 管理能力
                                DATA_INTERFACE};

    // Abstract Control Model
    desc_block_.cdc_acm = {
        4, DescriptorType::CS_INTERFACE, CDCDescriptorSubtype::ACM,
        0x02};  // 支持 Set_Line_Coding, Get_Line_Coding, Set_Control_Line_State

    // Union Functional
    desc_block_.cdc_union = {5, DescriptorType::CS_INTERFACE, CDCDescriptorSubtype::UNION,
                             COMM_INTERFACE, DATA_INTERFACE};

    // Data Interface 描述符
    desc_block_.data_intf = {9,
                             static_cast<uint8_t>(DescriptorType::INTERFACE),
                             DATA_INTERFACE,
                             0,
                             2,
                             static_cast<uint8_t>(CDCClass::DATA),
                             0x00,
                             0x00,
                             0};

    // Data OUT Endpoint
    desc_block_.data_ep_out = {7,
                               static_cast<uint8_t>(DescriptorType::ENDPOINT),
                               ep_data_out_->Address(),
                               static_cast<uint8_t>(Endpoint::Type::BULK),
                               packet_size_,
                               0};

    // Data IN Endpoint
    desc_block_.data_ep_in = {7,
                              static_cast<uint8_t>(DescriptorType::ENDPOINT),
                              static_cast<uint8_t>(ep_data_in_->Address()),
                              static_cast<uint8_t>(Endpoint::Type::BULK),
                              packet_size_,
                              0};

    desc_block_.comm_ep = {
        7,
        static_cast<uint8_t>(DescriptorType::ENDPOINT),
        static_cast<uint8_t>(ep_comm_in_->Address() | 0x80),  // IN 端点
        static_cast<uint8_t>(Endpoint::Type::INTERRUPT),
        8,    // 推荐：8字节中断包
        0x10  // 16ms polling interval
    };

    // 设置最终原始数据指针
    data_ = RawData{reinterpret_cast<uint8_t*>(&desc_block_), sizeof(desc_block_)};
  }

  void Deinit(EndpointPool* endpoint_pool) override
  {
    ep_data_in_->Close();
    ep_data_out_->Close();
    ep_comm_in_->Close();
    endpoint_pool->Release(ep_data_in_);
    endpoint_pool->Release(ep_data_out_);
    endpoint_pool->Release(ep_comm_in_);
    ep_data_in_ = nullptr;
    ep_data_out_ = nullptr;
    ep_comm_in_ = nullptr;
  }

  size_t GetInterfaceNum() override { return 2; }

  ErrorCode WriteDeviceDescriptor(DeviceDescriptor* header) override
  {
    /* 复合接口设备不需要描述符 */
    ASSERT(false);
    UNUSED(header);
    return ErrorCode::NOT_SUPPORT;
  }

 private:
#pragma pack(push, 1)
  struct CDCDescBlock
  {
    // 1. IAD
    IADDescriptor iad;
    // 2. Comm Interface
    InterfaceDescriptor comm_intf;
    // 3. Class-specific descriptors
    struct
    {
      uint8_t bFunctionLength = 5;
      DescriptorType bDescriptorType = DescriptorType::CS_INTERFACE;
      CDCDescriptorSubtype bDescriptorSubtype = CDCDescriptorSubtype::HEADER;
      uint16_t bcdCDC = 0x0110;
    } cdc_header;
    struct
    {
      uint8_t bFunctionLength = 5;
      DescriptorType bDescriptorType = DescriptorType::CS_INTERFACE;
      CDCDescriptorSubtype bDescriptorSubtype = CDCDescriptorSubtype::CALL_MANAGEMENT;
      uint8_t bmCapabilities = 0x00;
      uint8_t bDataInterface;
    } cdc_callmgmt;
    struct
    {
      uint8_t bFunctionLength = 4;
      DescriptorType bDescriptorType = DescriptorType::CS_INTERFACE;
      CDCDescriptorSubtype bDescriptorSubtype = CDCDescriptorSubtype::ACM;
      uint8_t bmCapabilities = 0x02;
    } cdc_acm;
    struct
    {
      uint8_t bFunctionLength = 5;
      DescriptorType bDescriptorType = DescriptorType::CS_INTERFACE;
      CDCDescriptorSubtype bDescriptorSubtype = CDCDescriptorSubtype::UNION;
      uint8_t bMasterInterface;
      uint8_t bSlaveInterface0;
    } cdc_union;

    // 4. Data Interface
    InterfaceDescriptor data_intf;

    // 5. Data Endpoints
    EndpointDescriptor data_ep_out;
    EndpointDescriptor data_ep_in;
    EndpointDescriptor comm_ep;
  } desc_block_;
#pragma pack(pop)

  uint16_t packet_size_;

  size_t data_in_ep_num_ = -1;
  size_t data_out_ep_num_ = -1;
  size_t comm_ep_num_ = -1;

  Endpoint* ep_data_in_ = nullptr;
  Endpoint* ep_data_out_ = nullptr;
  Endpoint* ep_comm_in_ = nullptr;
};

}  // namespace LibXR::USB
