#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "core.hpp"
#include "endpoint.hpp"
#include "endpoint_pool.hpp"
#include "lockfree_list.hpp"
#include "lockfree_pool.hpp"

namespace LibXR::USB
{

enum class DescriptorType : uint8_t
{
  DEVICE = 1,
  CONFIGURATION = 0x02,
  STRING = 0x03,
  INTERFACE = 0x04,
  ENDPOINT = 0x05,
  IAD = 0x0B,
  CS_INTERFACE = 0x24,
};

class DescriptorStrings
{
 public:
  enum class Index : uint8_t
  {
    LANGUAGE_ID = 0x00,
    MANUFACTURER_STRING = 0x01,
    PRODUCT_STRING = 0x02,
    SERIAL_NUMBER_STRING = 0x03
  };

  enum class Language : uint16_t
  {
    EN_US = 0x0409,  // English (United States)
    ZH_CN = 0x0804   // Chinese (Simplified, PRC)
  };

  static constexpr size_t STRING_LIST_SIZE = 3;

  typedef const char* StringData;

  DescriptorStrings(size_t lang_num)
      : max_lang_num_(lang_num),
        header_(new uint16_t[lang_num + 1]),
        land_id_(header_ + 1),
        string_list_(new StringData[lang_num * STRING_LIST_SIZE])
  {
    ASSERT(lang_num > 0);
    memset(header_, 0, lang_num + 1);
    memset(static_cast<void*>(string_list_), 0,
           lang_num * STRING_LIST_SIZE * sizeof(StringData));
    memset(buffer_.addr_, 0, buffer_.size_);
  }

  void GenerateLangIDDescriptor()
  {
    *header_ = (static_cast<uint16_t>(lang_num_ * 2 + 2) << 8) | 0x03;
  }

  ErrorCode AddLanguage(Language lang_id, const char* manufacturer_id = "N/A",
                        const char* product_id = "N/A", const char* serial_id = "N/A")
  {
    if (lang_num_ >= max_lang_num_)
    {
      return ErrorCode::FULL;
    }

    if (buffer_.addr_ != nullptr)
    {
      return ErrorCode::FAILED;
    }

    land_id_[lang_num_] = static_cast<uint16_t>(lang_id);

    string_list_[lang_num_ * STRING_LIST_SIZE +
                 static_cast<size_t>(Index::MANUFACTURER_STRING)] = manufacturer_id;
    string_list_[lang_num_ * STRING_LIST_SIZE +
                 static_cast<size_t>(Index::PRODUCT_STRING)] = product_id;
    string_list_[lang_num_ * STRING_LIST_SIZE +
                 static_cast<size_t>(Index::SERIAL_NUMBER_STRING)] = serial_id;

    lang_num_++;

    GenerateLangIDDescriptor();

    size_t max_size =
        LibXR::max(Utf16LELength(manufacturer_id), Utf16LELength(product_id));
    max_size = LibXR::max(max_size, Utf16LELength(serial_id));
    buffer_.size_ = LibXR::max(max_size + 2, buffer_.size_);

    return ErrorCode::OK;
  }

  void Setup()
  {
    ASSERT(buffer_.addr_ == nullptr);
    ASSERT(lang_num_ > 0);
    buffer_.addr_ = new uint8_t[buffer_.size_];
  }

  ErrorCode GenerateString(Index index, uint16_t lang)
  {
    ASSERT(buffer_.addr_ != nullptr);

    int ans = -1;
    for (int i = 0; i < lang_num_; ++i)
    {
      if (land_id_[i] == lang)
      {
        ans = i;
      }
    }

    if (ans == -1)
    {
      return ErrorCode::NOT_FOUND;
    }

    uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_.addr_);

    buffer[1] = 0x03;
    const char* str =
        string_list_[ans * STRING_LIST_SIZE + (static_cast<size_t>(index) - 1)];
    buffer[0] = ToUTF16LE(str, &buffer[2]) + 2;

    return ErrorCode::OK;
  }

  RawData GetData()
  {
    ASSERT(buffer_.addr_ != nullptr);
    uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_.addr_);

    RawData data{buffer_.addr_, buffer[0]};
    return data;
  }

  RawData GetLangIDData() { return RawData{header_, (lang_num_ + 1) * sizeof(uint16_t)}; }

  static size_t ToUTF16LE(const char* str, uint8_t* buffer)
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
        // 忽略超出 BMP 的字符（如 emoji）
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

    return len;
  }

  static size_t Utf16LELength(const char* str)
  {
    size_t len = 0;
    size_t i = 0;

    while (str[i])
    {
      unsigned char c = static_cast<unsigned char>(str[i]);

      if (c < 0x80)
      {
        ++i;
        len += 2;
      }
      else if ((c & 0xE0) == 0xC0)
      {
        if (!str[i + 1])
        {
          break;
        }
        i += 2;
        len += 2;
      }
      else if ((c & 0xF0) == 0xE0)
      {
        if (!str[i + 1] || !str[i + 2])
        {
          break;
        }
        i += 3;
        len += 2;
      }
      else if ((c & 0xF8) == 0xF0)
      {
        // Emoji 等代理对，跳过但不增加 len
        if (!str[i + 1] || !str[i + 2] || !str[i + 3])
        {
          break;
        }
        i += 4;
      }
      else
      {
        ++i;  // 非法起始字节，跳过
      }
    }

    return len;
  }

  size_t max_lang_num_ = 0;
  size_t lang_num_ = 0;
  uint16_t* header_ = nullptr;
  uint16_t* land_id_ = nullptr;
  StringData* string_list_;
  RawData buffer_ = {nullptr, 0};
};

/**
 * @brief USB描述符基类
 *        USB descriptor base class
 *
 */
class DeviceDescriptor
{
 public:
  enum class DeviceClass : uint8_t
  {
    PER_INTERFACE = 0x00,
    AUDIO = 0x01,
    COMM = 0x02,
    HID = 0x03,
    PHYSICAL = 0x05,
    IMAGE = 0x06,
    PRINTER = 0x07,
    MASS_STORAGE = 0x08,
    HUB = 0x09,
    CDC_DATA = 0x0A,
    SMART_CARD = 0x0B,
    CONTENT_SECURITY = 0x0D,
    VIDEO = 0x0E,
    PERSONAL_HEALTHCARE = 0x0F,
    BILLBOARD = 0x11,
    TYPE_C_BRIDGE = 0x12,
    BULK_DISPLAY = 0x13,
    MCTP = 0x14,
    I3C = 0x3C,
    DIAGNOSTIC = 0xDC,
    WIRELESS = 0xE0,
    MISCELLANEOUS = 0xEF,
    APPLICATION_SPECIFIC = 0xFE,
    VENDOR_SPECIFIC = 0xFF
  };

  enum class PacketSize0 : uint8_t
  {
    SIZE_8 = 8,
    SIZE_16 = 16,
    SIZE_32 = 32,
    SIZE_64 = 64
  };

  static constexpr uint8_t DEVICE_DESC_LENGTH = 18;

#pragma pack(push, 1)
  struct Data
  {
    uint8_t bLength;
    DescriptorType bDescriptorType;
    USBSpec bcdUSB;
    DeviceClass bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    PacketSize0 bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
  };
#pragma pack(pop)

  static_assert(sizeof(Data) == 18, "DeviceDescriptor must be 18 bytes");

  Data data_;

  DeviceDescriptor(USBSpec spec, PacketSize0 packet_size, uint16_t vid, uint16_t pid,
                   uint16_t bcd, uint8_t num_configs)
      : data_{DEVICE_DESC_LENGTH,
              DescriptorType::DEVICE,
              spec,
              DeviceClass::MISCELLANEOUS,
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

  RawData GetData() { return RawData{reinterpret_cast<uint8_t*>(&data_), sizeof(data_)}; }
};

constexpr uint8_t USB_CONFIG_BUS_POWERED = 0x80;
constexpr uint8_t USB_CONFIG_SELF_POWERED = 0x40;
constexpr uint8_t USB_CONFIG_REMOTE_WAKEUP = 0x20;

class ConfigDescriptorItem
{
 public:
#pragma pack(push, 1)
  struct Header
  {
    uint8_t bLength = 9;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
  };

  // IAD 描述符结构体
  struct IADDescriptor
  {
    uint8_t bLength = 8;
    uint8_t bDescriptorType = 0x0B;
    uint8_t bFirstInterface;
    uint8_t bInterfaceCount;
    uint8_t bFunctionClass;
    uint8_t bFunctionSubClass;
    uint8_t bFunctionProtocol;
    uint8_t iFunction;
  };

  // Interface 描述符结构体
  struct InterfaceDescriptor
  {
    uint8_t bLength = 9;
    uint8_t bDescriptorType = 0x04;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
  };

  // Endpoint 描述符结构体
  struct EndpointDescriptor
  {
    uint8_t bLength = 7;
    uint8_t bDescriptorType = 0x05;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
  };
#pragma pack(pop)

  /**
   * @brief USB配置描述符初始化，派生类在此处申请端点
   *
   */
  virtual void Init(EndpointPool* endpoint_pool) = 0;

  /**
   * @brief USB配置描述符反初始化，派生类在此处释放端点
   *
   */
  virtual void Deinit(EndpointPool* endpoint_pool) = 0;

  virtual size_t GetInterfaceNum() = 0;

  virtual ErrorCode WriteDeviceDescriptor(DeviceDescriptor* header) = 0;

  RawData GetData() { return data_; }

  RawData data_;
};

class ConfigDescriptor
{
  using Header = ConfigDescriptorItem::Header;

 public:
  ConfigDescriptor(EndpointPool* endpoint_pool, size_t item_num,
                   uint8_t bmAttributes = USB_CONFIG_BUS_POWERED, uint8_t bMaxPower = 50)
      : endpoint_pool_(endpoint_pool),
        bmAttributes_(bmAttributes),
        bMaxPower_(bMaxPower),
        max_item_num_(item_num),
        items_(new ConfigDescriptorItem*[item_num])
  {
    ASSERT(item_num > 0);
    memset(static_cast<void*>(items_), 0, item_num * sizeof(ConfigDescriptorItem*));
  }

  void SetConfigurationValue(uint8_t config_value) { config_value_ = config_value; }

  ErrorCode AddItem(ConfigDescriptorItem* item)
  {
    if (item_num_ >= max_item_num_)
    {
      return ErrorCode::FULL;
    }

    ASSERT(buffer_.addr_ == nullptr);

    items_[item_num_] = item;
    item_num_++;

    if (composite_)
    {
      return ErrorCode::OK;
    }

    if (item_num_ > 0 || item->GetInterfaceNum() > 1)
    {
      composite_ = true;
    }

    return ErrorCode::OK;
  }

  void AssignEndpointsAndBuffer()
  {
    uint32_t size = sizeof(Header);
    for (size_t i = 0; i < item_num_; ++i)
    {
      items_[i]->Init(endpoint_pool_);
      size += items_[i]->GetData().size_;
    }

    if (buffer_.addr_ == nullptr)
    {
      buffer_.addr_ = new uint8_t[size];
      buffer_.size_ = size;
    }
  }

  void ReleaseEndpoints()
  {
    for (size_t i = 0; i < item_num_; ++i)
    {
      items_[i]->Deinit(endpoint_pool_);
    }
  }

  ErrorCode Generate()
  {
    ASSERT(item_num_ > 0);
    AssignEndpointsAndBuffer();

    uint8_t* buffer = reinterpret_cast<uint8_t*>(buffer_.addr_);
    Header* header = reinterpret_cast<Header*>(buffer);

    // 填充默认或已设定的 header 信息
    memset(header, 0, sizeof(Header));
    header->bLength = 9;
    header->bDescriptorType = 0x02;
    header->bConfigurationValue = this->config_value_;
    header->iConfiguration = this->iConfiguration_;
    header->bmAttributes = this->bmAttributes_;
    header->bMaxPower = this->bMaxPower_;

    size_t offset = sizeof(Header);
    uint8_t total_interfaces = 0;

    for (size_t i = 0; i < item_num_; ++i)
    {
      auto data = items_[i]->GetData();

      memcpy(&buffer[offset], data.addr_, data.size_);
      offset += data.size_;

      total_interfaces += items_[i]->GetInterfaceNum();
    }

    header->wTotalLength = offset;
    header->bNumInterfaces = total_interfaces;
    buffer_index_ = offset;

    return ErrorCode::OK;
  }

  bool IsComposite() const { return composite_; }

  RawData GetData() const { return buffer_; }

  EndpointPool* endpoint_pool_ = nullptr;
  uint8_t config_value_ = 1;
  uint8_t iConfiguration_ = 0;
  uint8_t bmAttributes_ = 0x80;
  uint8_t bMaxPower_ = 50;

  bool composite_ = false;
  size_t max_item_num_ = 0;
  size_t item_num_ = 0;
  ConfigDescriptorItem** items_ = nullptr;
  RawData buffer_ = {nullptr, 0};
  size_t buffer_index_ = 0;
};

}  // namespace LibXR::USB
