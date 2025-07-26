#include "desc_dev.hpp"

using namespace LibXR::USB;

DeviceDescriptor::DeviceDescriptor(USBSpec spec, PacketSize0 packet_size, uint16_t vid,
                                   uint16_t pid, uint16_t bcd, uint8_t num_configs)
    : data_{DEVICE_DESC_LENGTH,
            DescriptorType::DEVICE,
            spec,
            ClassID::MISCELLANEOUS,
            0x02,
            0x01,
            packet_size,
            vid,
            pid,
            bcd,
            static_cast<uint8_t>(DescriptorStrings::Index::MANUFACTURER_STRING),
            static_cast<uint8_t>(DescriptorStrings::Index::PRODUCT_STRING),
            static_cast<uint8_t>(DescriptorStrings::Index::SERIAL_NUMBER_STRING),
            num_configs}
{
}

LibXR::RawData DeviceDescriptor::GetData()
{
  return RawData{reinterpret_cast<uint8_t*>(&data_), sizeof(data_)};
}

USBSpec DeviceDescriptor::GetUSBSpec() const { return data_.bcdUSB; }
