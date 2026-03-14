#include "device_composition.hpp"

#include <cstddef>
#include <cstdint>

namespace
{
using LibXR::USB::BOS_HEADER_SIZE;
using LibXR::USB::BosCapability;
using LibXR::USB::CFG_REMOTE_WAKEUP;
using LibXR::USB::CFG_SELF_POWERED;
using LibXR::USB::ConfigDescriptorItem;
using LibXR::USB::DESCRIPTOR_TYPE_DEVICE_CAPABILITY;
using LibXR::USB::DEV_CAPABILITY_TYPE_USB20EXT;
using LibXR::USB::DescriptorStrings;
using LibXR::USB::DeviceClass;

struct InterfaceStringLayout
{
  size_t count = 0;
  size_t max_len = 0;
};

static size_t calc_total_item_num(
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs)
{
  size_t total = 0;
  for (const auto& group : configs)
  {
    total += group.size();
  }
  return total;
}

static bool contains_class(DeviceClass* const* list, size_t count, DeviceClass* item)
{
  for (size_t i = 0; i < count; ++i)
  {
    if (list[i] == item)
    {
      return true;
    }
  }
  return false;
}

static size_t calc_utf16le_len_runtime(const char* input)
{
  if (input == nullptr)
  {
    return 0;
  }

  size_t len = 0;
  for (size_t i = 0; input[i];)
  {
    unsigned char c = static_cast<unsigned char>(input[i]);
    if (c < 0x80)
    {
      len += 2;
      i += 1;
    }
    else if ((c & 0xE0) == 0xC0)
    {
      len += 2;
      i += 2;
    }
    else if ((c & 0xF0) == 0xE0)
    {
      len += 2;
      i += 3;
    }
    else
    {
      i += 4;
    }
  }
  return len;
}

static void to_utf16le(const char* str, uint8_t* buffer)
{
  size_t len = 0;
  const unsigned char* s = reinterpret_cast<const unsigned char*>(str);

  while (*s)
  {
    uint32_t codepoint = 0;

    if (*s < 0x80)
    {
      codepoint = *s++;
    }
    else if ((*s & 0xE0) == 0xC0)
    {
      codepoint = (*s & 0x1F) << 6;
      s++;
      codepoint |= (*s & 0x3F);
      s++;
    }
    else if ((*s & 0xF0) == 0xE0)
    {
      codepoint = (*s & 0x0F) << 12;
      s++;
      codepoint |= (*s & 0x3F) << 6;
      s++;
      codepoint |= (*s & 0x3F);
      s++;
    }
    else if ((*s & 0xF8) == 0xF0)
    {
      s++;
      if (*s)
      {
        s++;
      }
      if (*s)
      {
        s++;
      }
      if (*s)
      {
        s++;
      }
      continue;
    }
    else
    {
      s++;
      continue;
    }

    buffer[len++] = codepoint & 0xFF;
    buffer[len++] = (codepoint >> 8) & 0xFF;
  }
}

static InterfaceStringLayout calc_interface_string_layout(
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs)
{
  const size_t MAX_CLASS_NUM = calc_total_item_num(configs);
  auto** seen = new DeviceClass*[MAX_CLASS_NUM];
  size_t seen_count = 0;

  InterfaceStringLayout layout{};

  for (const auto& group : configs)
  {
    for (auto* item : group)
    {
      auto* device_class = static_cast<DeviceClass*>(item);
      if (device_class == nullptr || contains_class(seen, seen_count, device_class))
      {
        continue;
      }

      seen[seen_count++] = device_class;
      const size_t INTERFACE_NUM = device_class->GetInterfaceCount();
      for (size_t i = 0; i < INTERFACE_NUM; ++i)
      {
        const char* str = device_class->GetInterfaceString(i);
        if (str == nullptr || str[0] == '\0')
        {
          continue;
        }

        ++layout.count;
        const size_t LEN = calc_utf16le_len_runtime(str);
        if (LEN > layout.max_len)
        {
          layout.max_len = LEN;
        }
      }
    }
  }

  delete[] seen;
  return layout;
}

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

static bool is_composite_config(const std::initializer_list<ConfigDescriptorItem*>& group)
{
  return (group.size() > 1) || config_contains_iad(group);
}

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

static bool is_composite_device(
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs)
{
  for (const auto& group : configs)
  {
    if (is_composite_config(group))
    {
      return true;
    }
  }
  return false;
}

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
using LibXR::ConstRawData;
using LibXR::RawData;

DeviceComposition::DeviceComposition(
    EndpointPool& endpoint_pool,
    const std::initializer_list<const DescriptorStrings::LanguagePack*>& lang_list,
    const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
        configs,
    ConstRawData uid, uint8_t bmAttributes, uint8_t bMaxPower)
    : endpoint_pool_(endpoint_pool),
      bm_attributes_(bmAttributes),
      b_max_power_(bMaxPower),
      composite_(is_composite_device(configs)),
      config_num_(configs.size()),
      items_(new ConfigItems[config_num_]),
      strings_(lang_list, reinterpret_cast<const uint8_t*>(uid.addr_), uid.size_),
      bos_(calc_bos_descriptor_size_max(configs), calc_bos_capability_num_max(configs)),
      config_desc_(ConfigDescriptor::CalcMaxConfigSize(configs), bmAttributes, bMaxPower)
{
  ASSERT(config_num_ > 0);

  size_t config_index = 0;
  for (const auto& cfg_group : configs)
  {
    items_[config_index].item_num = cfg_group.size();
    items_[config_index].items = new ConfigDescriptorItem*[cfg_group.size()];

    size_t item_index = 0;
    for (auto* item : cfg_group)
    {
      items_[config_index].items[item_index++] = item;
    }

    ++config_index;
  }

  const auto interface_string_layout = calc_interface_string_layout(configs);
  interface_string_capacity_ = interface_string_layout.count;
  if (interface_string_capacity_ > 0)
  {
    interface_strings_ = new const char*[interface_string_capacity_];
    interface_string_buffer_.addr_ = new uint8_t[interface_string_layout.max_len + 2];
    interface_string_buffer_.size_ = interface_string_layout.max_len + 2;
  }

  RegisterInterfaceStrings();
}

DeviceComposition::~DeviceComposition()
{
  if (items_ != nullptr)
  {
    for (size_t i = 0; i < config_num_; ++i)
    {
      delete[] items_[i].items;
      items_[i].items = nullptr;
      items_[i].item_num = 0;
    }
  }

  delete[] items_;
  items_ = nullptr;

  delete[] interface_strings_;
  interface_strings_ = nullptr;
  interface_string_count_ = 0;
  interface_string_capacity_ = 0;

  delete[] reinterpret_cast<uint8_t*>(interface_string_buffer_.addr_);
  interface_string_buffer_ = {nullptr, 0};
}

const DeviceComposition::ConfigItems& DeviceComposition::CurrentConfigItems() const
{
  ASSERT(config_num_ > 0);
  ASSERT(current_cfg_ < config_num_);
  return items_[current_cfg_];
}

void DeviceComposition::Init(bool in_isr)
{
  BindEndpoints(in_isr);
  RebuildBosCache();
}

void DeviceComposition::Deinit(bool in_isr) { UnbindEndpoints(in_isr); }

ErrorCode DeviceComposition::SwitchConfig(size_t index, bool in_isr)
{
  if (index == 0 || index > config_num_)
  {
    return ErrorCode::NOT_FOUND;
  }

  UnbindEndpoints(in_isr);
  current_cfg_ = static_cast<uint8_t>(index - 1);
  BindEndpoints(in_isr);
  RebuildBosCache();
  return ErrorCode::OK;
}

ErrorCode DeviceComposition::BuildConfigDescriptor()
{
  const auto& CFG = CurrentConfigItems();
  return config_desc_.BuildConfigDescriptor(CFG.items, CFG.item_num,
                                            static_cast<uint8_t>(current_cfg_ + 1),
                                            i_configuration_);
}

RawData DeviceComposition::GetConfigDescriptor() const { return config_desc_.GetData(); }

ConstRawData DeviceComposition::GetBosDescriptor() { return bos_.GetBosDescriptor(); }

ErrorCode DeviceComposition::ProcessBosVendorRequest(bool in_isr, const SetupPacket* setup,
                                                     BosVendorResult& result)
{
  return bos_.ProcessVendorRequest(in_isr, setup, result);
}

ErrorCode DeviceComposition::GetStringDescriptor(uint8_t string_index, uint16_t lang,
                                                 ConstRawData& data)
{
  if (string_index == 0)
  {
    data = ConstRawData(strings_.GetLangIDData());
    return ErrorCode::OK;
  }

  if (string_index > static_cast<uint8_t>(DescriptorStrings::Index::SERIAL_NUMBER_STRING))
  {
    return GenerateInterfaceString(string_index, data);
  }

  auto ec = strings_.GenerateString(
      static_cast<DescriptorStrings::Index>(string_index), lang);
  if (ec != ErrorCode::OK)
  {
    return ec;
  }

  data = ConstRawData(strings_.GetData());
  return ErrorCode::OK;
}

bool DeviceComposition::IsComposite() const { return composite_; }

bool DeviceComposition::CanOverrideDeviceDescriptor() const
{
  if (config_num_ != 1)
  {
    return false;
  }

  const auto CFG = items_[0];
  return is_device_descriptor_override_eligible(CFG.items, CFG.item_num);
}

ErrorCode DeviceComposition::OverrideDeviceDescriptor(DeviceDescriptor& descriptor)
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

size_t DeviceComposition::GetConfigNum() const { return config_num_; }

size_t DeviceComposition::GetCurrentConfig() const { return current_cfg_ + 1; }

uint16_t DeviceComposition::GetDeviceStatus() const
{
  return ((bm_attributes_ & CFG_SELF_POWERED) ? 0x01 : 0x00) |
         ((bm_attributes_ & CFG_REMOTE_WAKEUP) ? 0x02 : 0x00);
}

DeviceClass* DeviceComposition::FindClassByInterfaceNumber(size_t index) const
{
  const auto& CFG = CurrentConfigItems();

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
      return static_cast<DeviceClass*>(item);
    }
  }

  return nullptr;
}

DeviceClass* DeviceComposition::FindClassByEndpointAddress(uint8_t addr) const
{
  const auto& CFG = CurrentConfigItems();

  for (size_t i = 0; i < CFG.item_num; ++i)
  {
    auto* item = CFG.items[i];
    if (item == nullptr)
    {
      continue;
    }

    if (item->OwnsEndpoint(addr))
    {
      return static_cast<DeviceClass*>(item);
    }
  }

  return nullptr;
}

void DeviceComposition::BindEndpoints(bool in_isr)
{
  if (ep_assigned_)
  {
    return;
  }
  ep_assigned_ = true;

  const auto& CFG = CurrentConfigItems();

  size_t start_itf = 0;
  for (size_t i = 0; i < CFG.item_num; ++i)
  {
    auto* item = CFG.items[i];
    if (item == nullptr)
    {
      continue;
    }

    item->BindEndpoints(endpoint_pool_, static_cast<uint8_t>(start_itf), in_isr);
    start_itf += item->GetInterfaceCount();
  }
}

void DeviceComposition::UnbindEndpoints(bool in_isr)
{
  if (!ep_assigned_)
  {
    return;
  }
  ep_assigned_ = false;

  const auto& CFG = CurrentConfigItems();
  for (size_t i = 0; i < CFG.item_num; ++i)
  {
    auto* item = CFG.items[i];
    if (item == nullptr)
    {
      continue;
    }

    item->UnbindEndpoints(endpoint_pool_, in_isr);
  }
}

void DeviceComposition::RebuildBosCache()
{
  bos_.ClearCapabilities();
  const auto& CFG = CurrentConfigItems();
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
      if (cap != nullptr)
      {
        bos_.AddCapability(cap);
      }
    }
  }

  (void)bos_.GetBosDescriptor();
}

ErrorCode DeviceComposition::GenerateInterfaceString(uint8_t string_index, ConstRawData& data)
{
  const size_t BASE_INDEX = static_cast<uint8_t>(DescriptorStrings::Index::SERIAL_NUMBER_STRING);
  if (string_index <= BASE_INDEX)
  {
    return ErrorCode::NOT_FOUND;
  }

  const size_t EXTRA_INDEX = static_cast<size_t>(string_index - BASE_INDEX - 1u);
  if (EXTRA_INDEX >= interface_string_count_ || interface_string_buffer_.addr_ == nullptr)
  {
    return ErrorCode::NOT_FOUND;
  }

  uint8_t* buffer = reinterpret_cast<uint8_t*>(interface_string_buffer_.addr_);
  const char* str = interface_strings_[EXTRA_INDEX];
  const size_t STR_LEN = calc_utf16le_len_runtime(str);
  ASSERT(STR_LEN + 2 <= 255);

  buffer[1] = 0x03;
  buffer[0] = static_cast<uint8_t>(STR_LEN + 2);
  to_utf16le(str, buffer + 2);
  data = ConstRawData(interface_string_buffer_.addr_, buffer[0]);
  return ErrorCode::OK;
}

void DeviceComposition::RegisterInterfaceStrings()
{
  if (interface_string_capacity_ == 0)
  {
    return;
  }

  const size_t MAX_CLASS_NUM = [this]() {
    size_t total = 0;
    for (size_t i = 0; i < config_num_; ++i)
    {
      total += items_[i].item_num;
    }
    return total;
  }();

  auto** seen = new DeviceClass*[MAX_CLASS_NUM];
  size_t seen_count = 0;
  uint8_t next_index =
      static_cast<uint8_t>(DescriptorStrings::Index::SERIAL_NUMBER_STRING) + 1u;

  for (size_t cfg_index = 0; cfg_index < config_num_; ++cfg_index)
  {
    const auto& CFG = items_[cfg_index];
    for (size_t item_index = 0; item_index < CFG.item_num; ++item_index)
    {
      auto* device_class = static_cast<DeviceClass*>(CFG.items[item_index]);
      if (device_class == nullptr || contains_class(seen, seen_count, device_class))
      {
        continue;
      }

      seen[seen_count++] = device_class;

      const size_t INTERFACE_NUM = device_class->GetInterfaceCount();
      device_class->PrepareInterfaceStringIndexes(INTERFACE_NUM);

      for (size_t i = 0; i < INTERFACE_NUM; ++i)
      {
        const char* str = device_class->GetInterfaceString(i);
        if (str == nullptr || str[0] == '\0')
        {
          continue;
        }

        ASSERT(interface_string_count_ < interface_string_capacity_);
        interface_strings_[interface_string_count_++] = str;
        device_class->SetInterfaceStringIndex(i, next_index++);
      }
    }
  }

  delete[] seen;
}
