#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "core.hpp"
#include "desc_dev.hpp"
#include "ep_pool.hpp"
#include "lockfree_list.hpp"
#include "lockfree_pool.hpp"

namespace LibXR::USB
{
// USB 配置描述符属性标志（bmAttributes 位定义）
// USB configuration descriptor attribute bits (bmAttributes)
constexpr uint8_t CFG_BUS_POWERED = 0x80;    ///< 总线供电 / Bus powered
constexpr uint8_t CFG_SELF_POWERED = 0x40;   ///< 自供电 / Self powered
constexpr uint8_t CFG_REMOTE_WAKEUP = 0x20;  ///< 支持远程唤醒 / Remote wakeup supported

class ConfigDescriptor;

/**
 * @brief USB 配置项接口类
 *        USB configuration item base class
 *
 */
class ConfigDescriptorItem
{
 public:
#pragma pack(push, 1)
  /**
   * @brief 配置描述符头部结构体
   *        Configuration descriptor header (USB 2.0 Spec 9.6.3)
   *
   */
  struct Header
  {
    uint8_t bLength = 9;      ///< 结构体长度 / Descriptor length (always 9)
    uint8_t bDescriptorType;  ///< 描述符类型（0x02）/ Descriptor type (0x02)
    uint16_t wTotalLength;   ///< 本配置所有描述符总长度 / Total length of all descriptors
                             ///< for this configuration
    uint8_t bNumInterfaces;  ///< 接口数量 / Number of interfaces
    uint8_t bConfigurationValue;  ///< 配置值 / Configuration value
    uint8_t iConfiguration;       ///< 字符串描述符索引 / String descriptor index
    uint8_t bmAttributes;         ///< 属性位 / Attributes bitmap
    uint8_t bMaxPower;            ///< 最大电流（单位2mA）/ Max power (in units of 2mA)
  };

  /**
   * @brief 接口关联描述符（IAD，Interface Association Descriptor）
   *        IAD descriptor structure (用于复合设备多接口归组 / used for grouping
   * interfaces in composite devices)
   *
   */
  struct IADDescriptor
  {
    uint8_t bLength = 8;             ///< 结构体长度 / Descriptor length (always 8)
    uint8_t bDescriptorType = 0x0B;  ///< 描述符类型（0x0B）/ Descriptor type (IAD, 0x0B)
    uint8_t bFirstInterface;         ///< 第一个接口号 / First interface number
    uint8_t bInterfaceCount;    ///< 包含接口数 / Number of interfaces in this function
    uint8_t bFunctionClass;     ///< 功能类代码 / Function class
    uint8_t bFunctionSubClass;  ///< 功能子类代码 / Function subclass
    uint8_t bFunctionProtocol;  ///< 功能协议代码 / Function protocol
    uint8_t iFunction;          ///< 字符串描述符索引 / String descriptor index
  };

  /**
   * @brief 接口描述符结构体
   *        Interface descriptor structure (USB 2.0 Spec 9.6.5)
   */
  struct InterfaceDescriptor
  {
    uint8_t bLength = 9;  ///< 结构体长度 / Descriptor length (always 9)
    uint8_t bDescriptorType =
        0x04;                   ///< 描述符类型（0x04）/ Descriptor type (interface, 0x04)
    uint8_t bInterfaceNumber;   ///< 接口号 / Interface number
    uint8_t bAlternateSetting;  ///< 备用设置号 / Alternate setting number
    uint8_t bNumEndpoints;      ///< 端点数量 / Number of endpoints (excluding endpoint 0)
    uint8_t bInterfaceClass;    ///< 接口类 / Interface class code
    uint8_t bInterfaceSubClass;  ///< 接口子类 / Interface subclass code
    uint8_t bInterfaceProtocol;  ///< 协议代码 / Protocol code
    uint8_t iInterface;          ///< 字符串描述符索引 / String descriptor index
  };

  /**
   * @brief 端点描述符结构体
   *        Endpoint descriptor structure (USB 2.0 Spec 9.6.6)
   */
  struct EndpointDescriptor
  {
    uint8_t bLength = 7;  ///< 结构体长度 / Descriptor length (always 7)
    uint8_t bDescriptorType =
        0x05;                  ///< 描述符类型（0x05）/ Descriptor type (endpoint, 0x05)
    uint8_t bEndpointAddress;  ///< 端点地址 / Endpoint address (IN/OUT & index)
    uint8_t bmAttributes;      ///< 属性位 / Attributes (transfer type, sync, usage)
    uint16_t wMaxPacketSize;   ///< 最大包长 / Max packet size
    uint8_t bInterval;  ///< 轮询间隔 / Polling interval (ms/frames, per endpoint type)
  };
#pragma pack(pop)

  /**
   * @brief USB配置描述符初始化，派生类在此处申请端点
   *
   */
  virtual void Init(EndpointPool& endpoint_pool, size_t start_itf_num) = 0;

  /**
   * @brief USB配置描述符反初始化，派生类在此处释放端点
   *
   */
  virtual void Deinit(EndpointPool& endpoint_pool) = 0;

  /**
   * @brief 写入/补全设备描述符（非IAD情况下会被调用）
   *        Write device descriptor (non-IAD case will be called)
   * @param header 设备描述符 / Device descriptor
   * @return 错误码 / Error code
   */
  virtual ErrorCode WriteDeviceDescriptor(DeviceDescriptor& header)
  {
    UNUSED(header);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 获取该配置项的最大描述符长度
   *        Get the maximum descriptor length of this configuration item
   *
   * @return size_t 描述符长度 / Descriptor length
   */
  virtual size_t GetMaxConfigSize() = 0;

  /**
   * @brief 获取该配置项包含的接口数
   *        Get the number of interfaces included in this configuration item
   * @return 接口数量 / Number of interfaces
   */
  virtual size_t GetInterfaceNum() = 0;

  /**
   * @brief 判断是否包含IAD
   *        Check if this configuration item contains IAD
   *
   * @return true
   * @return false
   */
  virtual bool HasIAD() = 0;

 protected:
  /**
   * @brief 获取本配置项描述符的二进制数据
   *        Get the binary data of this configuration item
   * @return RawData 结构体 / RawData struct
   */
  RawData GetData();

  /**
   * @brief 设置配置项数据
   *        Set configuration item data
   *
   * @param data 配置项数据 / Configuration item data
   */
  void SetData(RawData data) { data_ = data; }

  friend class ConfigDescriptor;

 private:
  RawData data_;  ///< 存储本配置项序列化后描述符数据 / Serialized descriptor data for
                  ///< this item
};

/**
 * @brief USB 配置描述符生成器
 *        USB configuration descriptor generator
 *
 */
class ConfigDescriptor
{
  using Header = ConfigDescriptorItem::Header;

  struct Config
  {
    ConfigDescriptorItem** items;
    size_t item_num;
  };

  static bool IsCompositeConfig(
      const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
          configs);

 public:
  /**
   * @brief 构造函数，初始化配置项存储和属性
   *        Constructor: setup item list and descriptor attributes
   * @param endpoint_pool 端点资源池指针 / Endpoint pool for endpoint allocation
   * @param item_num 支持的功能项（接口块）最大数量 / Maximum number of config items
   * @param bmAttributes bmAttributes 属性位（默认总线供电）/ bmAttributes flags (default:
   * bus powered)
   * @param bMaxPower 最大电流（2mA 单位，默认 50=100mA）/ Max power (unit: 2mA, default:
   * 50 = 100mA)
   */
  ConfigDescriptor(
      EndpointPool& endpoint_pool,
      const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
          configs,
      uint8_t bmAttributes = CFG_BUS_POWERED, uint8_t bMaxPower = 50);

  ErrorCode SwitchConfig(size_t index);

  /**
   * @brief 分配端点并分配总缓冲区
   *        Assign endpoints and allocate the total config descriptor buffer
   */
  void AssignEndpoints();

  /**
   * @brief 释放所有功能项占用的端点资源
   *        Release all endpoints/resources allocated by function blocks
   */
  void ReleaseEndpoints();

  /**
   * @brief 生成完整的配置描述符（自动拼接 header 和所有功能项数据）
   *        Generate and assemble the full configuration descriptor (header + all items)
   * @return ErrorCode::OK 生成成功 / ErrorCode::FAILED 失败
   */
  ErrorCode Generate();

  /**
   * @brief 判断是否为复合设备（composite device）
   *        Check if this is a composite device
   * @return true/false
   */
  bool IsComposite() const;

  /**
   * @brief 覆盖设备描述符（非IAD时可用）
   *        Override the device descriptor (can be used when not using IAD)
   *
   * @param descriptor 设备描述符 / Device descriptor
   * @return ErrorCode
   */
  ErrorCode OverrideDeviceDescriptor(DeviceDescriptor& descriptor);

  /**
   * @brief 获取拼接好的配置描述符数据
   *        Get the generated configuration descriptor data
   * @return RawData 结构体，包含指针和长度 / RawData with pointer and length
   */
  RawData GetData() const;

  /**
   * @brief 获取配置项数量 / Get the number of configuration items
   *
   * @return size_t
   */
  size_t GetConfigNum() const;

  /**
   * @brief 获取当前配置值 / Get the current configuration value
   *
   * @return size_t
   */
  size_t GetCurrentConfig() const;

  /**
   * @brief 获取当前设备状态 / Get the current device status
   *
   * @return uint16_t
   */
  uint16_t GetDeviceStatus() const;

  /**
   * @brief 获取指定接口的配置项 / Get the configuration item by interface number
   *
   * @param index 接口号 / Interface number
   * @return ConfigDescriptorItem*
   */
  ConfigDescriptorItem* GetItemByInterfaceNum(size_t index) const;

 private:
  bool ep_assigned_ = false;  ///< 端点是否已分配 / Is endpoint assigned

  EndpointPool& endpoint_pool_;   ///< 端点资源池 / Endpoint pool
  uint8_t current_cfg_ = 0;       ///< 配置值 / Configuration value
  uint8_t i_configuration_ = 0;   ///< 配置字符串索引 / String descriptor index
  uint8_t bm_attributes_ = 0x80;  ///< 配置属性 / bmAttributes
  uint8_t b_max_power_ = 50;      ///< 最大电流（2mA 单位）/ Max power (2mA unit)

  const bool COMPOSITE = false;    ///< 是否为复合设备 / Is composite device
  const size_t CFG_NUM = 0;        ///< 当前功能项数 / Current item count
  Config* items_ = nullptr;        ///< 功能项数组 / Item array
  RawData buffer_ = {nullptr, 0};  ///< 拼接后的描述符缓冲区 / Assembled descriptor buffer
  size_t buffer_index_ = 0;        ///< 数据写入索引 / Write index (for internal use)
};

}  // namespace LibXR::USB
