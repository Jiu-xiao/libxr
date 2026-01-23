#include "desc_cfg.hpp"

#include <cstddef>
#include <cstdint>

#include "usb/core/bos.hpp"

namespace
{
using LibXR::USB::BOS_HEADER_SIZE;
using LibXR::USB::BosCapability;
using LibXR::USB::ConfigDescriptorItem;
using LibXR::USB::DESCRIPTOR_TYPE_DEVICE_CAPABILITY;
using LibXR::USB::DEV_CAPABILITY_TYPE_USB20EXT;

/** Check whether any item in a configuration group uses IAD / 检查组内是否存在使用 IAD
 * 的配置项 */
static bool config_contains_iad(const std::initializer_list<ConfigDescriptorItem*>& group)
{
  for (auto* item : group)
  {
    if (item != nullptr && item->HasIAD())
    {
      return true;
    }
  }
  return false;
}

/** Composite if group has multiple items, or any item uses IAD / 复合条件：多个 item
 * 或任一 item 含 IAD */
static bool is_composite_config(const std::initializer_list<ConfigDescriptorItem*>& group)
{
  return (group.size() > 1) || config_contains_iad(group);
}

/** Eligibility check for device descriptor override / device descriptor override
 * 适用条件检查 */
static bool is_device_descriptor_override_eligible(ConfigDescriptorItem* const* items,
                                                   size_t item_num)
{
  if (item_num != 1)
  {
    return false;
  }

  auto* item = items[0];
  if (item == nullptr)
  {
    return false;
  }

  if (item->HasIAD())
  {
    return false;
  }

  return (item->GetInterfaceCount() == 1);
}

/** Max BOS capability count among configurations / 各 configuration 中 BOS capability
 * 数量上限 */
static size_t calc_bos_capability_num_max(
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs)
{
  size_t max_num = 0;
  for (const auto& group : configs)
  {
    size_t num = 0;
    for (auto* item : group)
    {
      if (item == nullptr)
      {
        continue;
      }
      num += item->GetBosCapabilityCount();
    }
    if (num > max_num)
    {
      max_num = num;
    }
  }
  return max_num;
}

/** Max BOS descriptor size among configurations / 各 configuration 中 BOS
 * 描述符最大字节数 */
static size_t calc_bos_descriptor_size_max(
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs)
{
  static constexpr size_t USB2_EXT_SIZE = 7;

  size_t max_total = BOS_HEADER_SIZE;
  for (const auto& group : configs)
  {
    size_t cap_bytes = 0;
    bool has_usb2_ext = false;

    for (auto* item : group)
    {
      if (item == nullptr)
      {
        continue;
      }

      const size_t CAP_NUM = item->GetBosCapabilityCount();
      for (size_t i = 0; i < CAP_NUM; ++i)
      {
        BosCapability* cap = item->GetBosCapability(i);
        if (cap == nullptr)
        {
          continue;
        }

        auto blk = cap->GetCapabilityDescriptor();
        ASSERT(blk.addr_ != nullptr);
        ASSERT(blk.size_ >= 3);

        cap_bytes += blk.size_;

        const uint8_t* p = reinterpret_cast<const uint8_t*>(blk.addr_);
        if (p[1] == DESCRIPTOR_TYPE_DEVICE_CAPABILITY &&
            p[2] == DEV_CAPABILITY_TYPE_USB20EXT)
        {
          has_usb2_ext = true;
        }
      }
    }

    const size_t TOTAL = BOS_HEADER_SIZE + cap_bytes + (has_usb2_ext ? 0 : USB2_EXT_SIZE);
    if (TOTAL > max_total)
    {
      max_total = TOTAL;
    }
  }

  ASSERT(max_total <= 0xFFFF);
  return max_total;
}

}  // namespace

using namespace LibXR::USB;

bool ConfigDescriptor::CanOverrideDeviceDescriptor() const
{
  /** Single configuration + single item + single interface + no IAD / 单一配置 + 单一项 +
   * 单一接口 + 非 IAD */
  if (CFG_NUM != 1)
  {
    return false;
  }

  const auto CFG = items_[0];
  return is_device_descriptor_override_eligible(CFG.items, CFG.item_num);
}

bool ConfigDescriptor::IsCompositeConfig(
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs)
{
  /** Composite semantics come from within one configuration / 复合语义来自单个
   * configuration 内的组合 */
  for (const auto& group : configs)
  {
    if (is_composite_config(group))
    {
      return true;
    }
  }
  return false;
}

LibXR::RawData ConfigDescriptorItem::GetData() { return data_; }

void ConfigDescriptor::RebuildBosCache()
{
  /** Collect capabilities from current configuration only / 仅收集当前 configuration
   * 的能力 */
  ClearCapabilities();

  ASSERT(CFG_NUM > 0);
  ASSERT(current_cfg_ < CFG_NUM);

  const auto CFG = items_[current_cfg_];

  for (size_t i = 0; i < CFG.item_num; ++i)
  {
    auto* item = CFG.items[i];
    if (item == nullptr)
    {
      continue;
    }

    const size_t CAP_NUM = item->GetBosCapabilityCount();
    for (size_t j = 0; j < CAP_NUM; ++j)
    {
      auto* cap = item->GetBosCapability(j);
      if (cap == nullptr)
      {
        continue;
      }
      AddCapability(cap);
    }
  }

  (void)GetBosDescriptor();
}

ConfigDescriptor::ConfigDescriptor(
    EndpointPool& endpoint_pool,
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs,
    uint8_t bmAttributes, uint8_t bMaxPower)
    : BosManager(calc_bos_descriptor_size_max(configs),
                 calc_bos_capability_num_max(configs)),
      endpoint_pool_(endpoint_pool),
      bm_attributes_(bmAttributes),
      b_max_power_(bMaxPower),
      COMPOSITE(IsCompositeConfig(configs)),
      CFG_NUM(configs.size()),
      items_(new Config[CFG_NUM])
{
  ASSERT(CFG_NUM > 0);

  size_t max_config_size = 0;
  size_t config_index = 0;

  for (const auto& cfg_group : configs)
  {
    size_t config_size = sizeof(Header);

    items_[config_index].item_num = cfg_group.size();
    items_[config_index].items = new ConfigDescriptorItem*[cfg_group.size()];

    size_t item_index = 0;
    for (auto* item : cfg_group)
    {
      items_[config_index].items[item_index] = item;
      if (item != nullptr)
      {
        config_size += item->GetMaxConfigSize();
      }
      ++item_index;
    }

    if (config_size > max_config_size)
    {
      max_config_size = config_size;
    }
    ++config_index;
  }

  buffer_.addr_ = new uint8_t[max_config_size];
  buffer_.size_ = max_config_size;
}

ErrorCode ConfigDescriptor::SwitchConfig(size_t index)
{
  /** USB configuration value starts from 1; 0 means unconfigured / configuration value 从
   * 1 开始，0 表示未配置 */
  if (index == 0 || index > CFG_NUM)
  {
    return ErrorCode::NOT_FOUND;
  }

  UnbindEndpoints();
  current_cfg_ = index - 1;
  BindEndpoints();
  return ErrorCode::OK;
}

void ConfigDescriptor::BindEndpoints()
{
  if (ep_assigned_)
  {
    return;
  }
  ep_assigned_ = true;

  const auto CFG = items_[current_cfg_];

  size_t start_itf = 0;
  for (size_t i = 0; i < CFG.item_num; ++i)
  {
    auto* item = CFG.items[i];
    if (item == nullptr)
    {
      continue;
    }

    item->BindEndpoints(endpoint_pool_, start_itf);
    start_itf += item->GetInterfaceCount();
  }
}

void ConfigDescriptor::UnbindEndpoints()
{
  if (!ep_assigned_)
  {
    return;
  }
  ep_assigned_ = false;

  const auto CFG = items_[current_cfg_];

  for (size_t i = 0; i < CFG.item_num; ++i)
  {
    auto* item = CFG.items[i];
    if (item == nullptr)
    {
      continue;
    }
    item->UnbindEndpoints(endpoint_pool_);
  }
}

ErrorCode ConfigDescriptor::BuildConfigDescriptor()
{
  uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_.addr_);
  Header* header = reinterpret_cast<Header*>(buffer);

  /** Initialize header with defaults / 用默认值初始化 header */
  *header = Header{};

  header->bLength = 9;
  header->bDescriptorType = 0x02;
  header->bConfigurationValue = static_cast<uint8_t>(current_cfg_ + 1);
  header->iConfiguration = i_configuration_;
  header->bmAttributes = bm_attributes_;
  header->bMaxPower = b_max_power_;

  size_t offset = sizeof(Header);
  uint8_t total_interfaces = 0;

  const auto CFG = items_[current_cfg_];

  for (size_t i = 0; i < CFG.item_num; ++i)
  {
    auto* item = CFG.items[i];
    if (item == nullptr)
    {
      continue;
    }

    auto data = item->GetData();
    LibXR::Memory::FastCopy(&buffer[offset], data.addr_, data.size_);
    offset += data.size_;

    total_interfaces = static_cast<uint8_t>(total_interfaces + item->GetInterfaceCount());
  }

  ASSERT(offset <= 0xFFFF);
  header->wTotalLength = static_cast<uint16_t>(offset);
  header->bNumInterfaces = total_interfaces;

  buffer_index_ = offset;
  return ErrorCode::OK;
}

bool ConfigDescriptor::IsComposite() const { return COMPOSITE; }

ErrorCode ConfigDescriptor::OverrideDeviceDescriptor(DeviceDescriptor& descriptor)
{
  if (!CanOverrideDeviceDescriptor())
  {
    return ErrorCode::NOT_SUPPORT;
  }

  const auto CFG = items_[0];
  if (CFG.item_num != 1 || CFG.items == nullptr || CFG.items[0] == nullptr)
  {
    return ErrorCode::NOT_SUPPORT;
  }

  return CFG.items[0]->WriteDeviceDescriptor(descriptor);
}

LibXR::RawData ConfigDescriptor::GetData() const { return buffer_; }

size_t ConfigDescriptor::GetConfigNum() const { return CFG_NUM; }

size_t ConfigDescriptor::GetCurrentConfig() const { return current_cfg_ + 1; }

uint16_t ConfigDescriptor::GetDeviceStatus() const
{
  return ((bm_attributes_ & CFG_SELF_POWERED) ? 0x01 : 0x00) |
         ((bm_attributes_ & CFG_REMOTE_WAKEUP) ? 0x02 : 0x00);
}

ConfigDescriptorItem* ConfigDescriptor::FindItemByInterfaceNumber(size_t index) const
{
  /** Map cumulative interface count to item / 通过累积接口数量映射所属 item */
  const auto CFG = items_[current_cfg_];

  int interface_index = -1;
  for (size_t i = 0; i < CFG.item_num; ++i)
  {
    auto* item = CFG.items[i];
    if (item == nullptr)
    {
      continue;
    }

    interface_index += static_cast<int>(item->GetInterfaceCount());
    if (interface_index >= static_cast<int>(index))
    {
      return item;
    }
  }
  return nullptr;
}

ConfigDescriptorItem* ConfigDescriptor::FindItemByEndpointAddress(uint8_t addr) const
{
  const auto CFG = items_[current_cfg_];

  for (size_t i = 0; i < CFG.item_num; ++i)
  {
    auto* item = CFG.items[i];
    if (item == nullptr)
    {
      continue;
    }

    if (item->OwnsEndpoint(addr))
    {
      return item;
    }
  }
  return nullptr;
}
