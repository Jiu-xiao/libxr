#include "desc_cfg.hpp"

using namespace LibXR::USB;

LibXR::RawData ConfigDescriptorItem::GetData() { return data_; }

bool ConfigDescriptor::IsCompositeConfig(
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs)
{
  // 复合设备：有多个功能项（接口块），或某功能项含多个接口
  // 判断标准：configs.size() > 1 或者 (有一项 GetInterfaceNum() > 1)
  if (configs.size() > 1)
  {
    return true;
  }
  for (const auto& group : configs)
  {
    if (group.size() > 1)
    {
      return true;
    }
    for (const auto& item : group)
    {
      if (item->HasIAD())
      {
        return true;
      }
    }
  }
  return false;
}

ConfigDescriptor::ConfigDescriptor(
    EndpointPool& endpoint_pool,
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs,
    uint8_t bmAttributes, uint8_t bMaxPower)
    : endpoint_pool_(endpoint_pool),
      bm_attributes_(bmAttributes),
      b_max_power_(bMaxPower),
      COMPOSITE(IsCompositeConfig(configs)),
      CFG_NUM(configs.size()),
      items_(new Config[CFG_NUM])
{
  ASSERT(CFG_NUM > 0);

  size_t max_config_size = 0;
  size_t config_index = 0;
  for (auto config : configs)
  {
    size_t config_size = sizeof(Header);
    size_t item_index = 0;
    items_[config_index].item_num = config.size();
    items_[config_index].items = new ConfigDescriptorItem*[config.size()];
    for (auto item : config)
    {
      config_size += item->GetMaxConfigSize();
      items_[config_index].items[item_index] = item;
      item_index++;
    }

    if (config_size > max_config_size)
    {
      max_config_size = config_size;
    }
    config_index++;
  }

  buffer_.addr_ = new uint8_t[max_config_size];
  buffer_.size_ = max_config_size;
}

ErrorCode ConfigDescriptor::SwitchConfig(size_t index)
{
  if (index == 0 || index > CFG_NUM)
  {
    return ErrorCode::NOT_FOUND;
  }

  ReleaseEndpoints();
  current_cfg_ = index - 1;
  AssignEndpoints();
  return ErrorCode::OK;
}

void ConfigDescriptor::AssignEndpoints()
{
  if (ep_assigned_)
  {
    return;
  }
  ep_assigned_ = true;

  auto config = items_[current_cfg_];

  size_t start_itf = 0;

  for (size_t i = 0; i < config.item_num; ++i)
  {
    config.items[i]->Init(endpoint_pool_, start_itf);
    start_itf += config.items[i]->GetInterfaceNum();
  }
}

void ConfigDescriptor::ReleaseEndpoints()
{
  if (!ep_assigned_)
  {
    return;
  }
  ep_assigned_ = false;

  auto config = items_[current_cfg_];

  for (size_t i = 0; i < config.item_num; ++i)
  {
    config.items[i]->Deinit(endpoint_pool_);
  }
}

ErrorCode ConfigDescriptor::Generate()
{
  uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_.addr_);
  Header* header = reinterpret_cast<Header*>(buffer);

  // 填充默认或已设定的 header 信息
  memset(header, 0, sizeof(Header));
  header->bLength = 9;
  header->bDescriptorType = 0x02;
  header->bConfigurationValue = this->current_cfg_ + 1;
  header->iConfiguration = this->i_configuration_;
  header->bmAttributes = this->bm_attributes_;
  header->bMaxPower = this->b_max_power_;

  size_t offset = sizeof(Header);
  uint8_t total_interfaces = 0;

  auto config = items_[current_cfg_];

  for (size_t i = 0; i < config.item_num; ++i)
  {
    auto data = config.items[i]->GetData();

    LibXR::Memory::FastCopy(&buffer[offset], data.addr_, data.size_);
    offset += data.size_;

    total_interfaces += config.items[i]->GetInterfaceNum();
  }

  header->wTotalLength = offset;
  header->bNumInterfaces = total_interfaces;
  buffer_index_ = offset;

  return ErrorCode::OK;
}

bool ConfigDescriptor::IsComposite() const { return COMPOSITE; }

ErrorCode ConfigDescriptor::OverrideDeviceDescriptor(DeviceDescriptor& descriptor)
{
  if (COMPOSITE || items_[current_cfg_].item_num != 1)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  auto config = items_[0];

  if (config.item_num != 1 || config.items[0]->GetInterfaceNum() != 1)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  return config.items[0]->WriteDeviceDescriptor(descriptor);
}

LibXR::RawData ConfigDescriptor::GetData() const { return buffer_; }

size_t ConfigDescriptor::GetConfigNum() const { return CFG_NUM; }

size_t ConfigDescriptor::GetCurrentConfig() const { return current_cfg_ + 1; }

uint16_t ConfigDescriptor::GetDeviceStatus() const
{
  return ((bm_attributes_ & CFG_SELF_POWERED) ? 0x01 : 0x00) |
         ((bm_attributes_ & CFG_REMOTE_WAKEUP) ? 0x02 : 0x00);
}

ConfigDescriptorItem* ConfigDescriptor::GetItemByInterfaceNum(size_t index) const
{
  auto config = items_[current_cfg_];
  size_t interface_index = 0;
  for (size_t i = 0; i < config.item_num; ++i)
  {
    interface_index += config.items[i]->GetInterfaceNum();
    if (interface_index >= index)
    {
      return config.items[i];
    }
  }
  return nullptr;
}
