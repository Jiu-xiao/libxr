#include "device_class.hpp"

using namespace LibXR::USB;

DeviceClass::DeviceClass(std::initializer_list<BosCapability*> bos_caps)
    : bos_cap_num_(bos_caps.size())
{
  if (bos_cap_num_ > 0)
  {
    bos_caps_ = new BosCapability*[bos_cap_num_];
    size_t i = 0;
    for (auto* cap : bos_caps)
    {
      bos_caps_[i++] = cap;
    }
  }
}

DeviceClass::~DeviceClass()
{
  // 仅释放指针数组本身（capability 对象生命周期由派生类成员管理）
  // Only free the pointer array itself (capability objects are owned by derived class
  // members).
  delete[] bos_caps_;
  bos_caps_ = nullptr;
  bos_cap_num_ = 0;

  delete[] interface_string_indexes_;
  interface_string_indexes_ = nullptr;
  interface_string_count_ = 0;
}

uint8_t DeviceClass::GetInterfaceStringIndex(size_t local_interface_index) const
{
  if (local_interface_index >= interface_string_count_ || interface_string_indexes_ == nullptr)
  {
    return 0;
  }
  return interface_string_indexes_[local_interface_index];
}

void DeviceClass::PrepareInterfaceStringIndexes(size_t interface_count)
{
  if (interface_count != interface_string_count_)
  {
    delete[] interface_string_indexes_;
    interface_string_indexes_ = nullptr;
    interface_string_count_ = interface_count;

    if (interface_count > 0)
    {
      interface_string_indexes_ = new uint8_t[interface_count];
    }
  }

  ClearInterfaceStringIndexes();
}

void DeviceClass::ClearInterfaceStringIndexes()
{
  if (interface_string_indexes_ == nullptr)
  {
    return;
  }

  for (size_t i = 0; i < interface_string_count_; ++i)
  {
    interface_string_indexes_[i] = 0;
  }
}

void DeviceClass::SetInterfaceStringIndex(size_t local_interface_index, uint8_t string_index)
{
  if (local_interface_index >= interface_string_count_ || interface_string_indexes_ == nullptr)
  {
    return;
  }

  interface_string_indexes_[local_interface_index] = string_index;
}
