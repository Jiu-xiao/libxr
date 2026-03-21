#include "device_class.hpp"

using namespace LibXR::USB;

uint8_t DeviceClass::GetInterfaceStringIndex(size_t local_interface_index) const
{
  if (interface_string_base_index_ == 0u)
  {
    return 0;
  }
  if (GetInterfaceString(local_interface_index) == nullptr)
  {
    return 0;
  }

  uint8_t index = interface_string_base_index_;
  for (size_t i = 0; i < local_interface_index; ++i)
  {
    if (GetInterfaceString(i) != nullptr)
    {
      ++index;
    }
  }
  return index;
}

void DeviceClass::SetInterfaceStringBaseIndex(uint8_t string_index)
{
  interface_string_base_index_ = string_index;
}
