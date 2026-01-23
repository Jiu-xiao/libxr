#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "bos.hpp"
#include "core.hpp"
#include "desc_dev.hpp"
#include "ep_pool.hpp"
#include "libxr_type.hpp"

namespace LibXR::USB
{
/**
 * @brief USB configuration descriptor attribute bits (bmAttributes)
 *        配置描述符属性位（bmAttributes）
 */
constexpr uint8_t CFG_BUS_POWERED = 0x80;    ///< 总线供电 / Bus-powered
constexpr uint8_t CFG_SELF_POWERED = 0x40;   ///< 自供电 / Self-powered
constexpr uint8_t CFG_REMOTE_WAKEUP = 0x20;  ///< 远程唤醒 / Remote wakeup

class ConfigDescriptor;
class DeviceCore;

/**
 * @brief USB 配置项基类（功能块）/ USB configuration item base (functional block)
 *
 * 一个配置项通常包含一个功能集合，可由多个 Interface/IAD 组成。
 * A configuration item usually represents one function group, consisting of multiple
 * Interface descriptors and/or an IAD.
 *
 * 同时作为 BOS capability 提供者（BosCapabilityProvider）。
 * Also serves as a BOS capability provider (BosCapabilityProvider).
 */
class ConfigDescriptorItem : public BosCapabilityProvider
{
 public:
  virtual ~ConfigDescriptorItem() = default;

#pragma pack(push, 1)
  /**
   * @brief 配置描述符头（9 字节）/ Configuration descriptor header (9 bytes)
   */
  struct Header
  {
    uint8_t bLength = 9;          ///< 描述符长度 / Descriptor length
    uint8_t bDescriptorType;      ///< 描述符类型（0x02）/ Descriptor type (0x02)
    uint16_t wTotalLength;        ///< 配置总长度 / Total configuration length
    uint8_t bNumInterfaces;       ///< 接口数量 / Number of interfaces
    uint8_t bConfigurationValue;  ///< 配置值 / Configuration value
    uint8_t iConfiguration;       ///< 配置字符串索引 / Configuration string index
    uint8_t bmAttributes;         ///< 属性位 / Attributes
    uint8_t bMaxPower;            ///< 最大电流（2mA 单位）/ Max power (2mA units)
  };

  /**
   * @brief IAD（8 字节）/ Interface Association Descriptor (8 bytes)
   */
  struct IADDescriptor
  {
    uint8_t bLength = 8;             ///< 描述符长度 / Descriptor length
    uint8_t bDescriptorType = 0x0B;  ///< 描述符类型 / Descriptor type
    uint8_t bFirstInterface;         ///< 首接口号 / First interface number
    uint8_t bInterfaceCount;         ///< 接口数量 / Interface count
    uint8_t bFunctionClass;          ///< 功能类 / Function class
    uint8_t bFunctionSubClass;       ///< 功能子类 / Function subclass
    uint8_t bFunctionProtocol;       ///< 功能协议 / Function protocol
    uint8_t iFunction;               ///< 功能字符串索引 / Function string index
  };

  /**
   * @brief 接口描述符（9 字节）/ Interface descriptor (9 bytes)
   */
  struct InterfaceDescriptor
  {
    uint8_t bLength = 9;             ///< 描述符长度 / Descriptor length
    uint8_t bDescriptorType = 0x04;  ///< 描述符类型 / Descriptor type
    uint8_t bInterfaceNumber;        ///< 接口号 / Interface number
    uint8_t bAlternateSetting;       ///< 备用设置号 / Alternate setting
    uint8_t bNumEndpoints;           ///< 端点数量 / Number of endpoints
    uint8_t bInterfaceClass;         ///< 接口类 / Interface class
    uint8_t bInterfaceSubClass;      ///< 接口子类 / Interface subclass
    uint8_t bInterfaceProtocol;      ///< 接口协议 / Interface protocol
    uint8_t iInterface;              ///< 接口字符串索引 / Interface string index
  };

  /**
   * @brief 端点描述符（7 字节）/ Endpoint descriptor (7 bytes)
   */
  struct EndpointDescriptor
  {
    uint8_t bLength = 7;             ///< 描述符长度 / Descriptor length
    uint8_t bDescriptorType = 0x05;  ///< 描述符类型 / Descriptor type
    uint8_t bEndpointAddress;        ///< 端点地址 / Endpoint address
    uint8_t bmAttributes;            ///< 端点属性 / Endpoint attributes
    uint16_t wMaxPacketSize;         ///< 最大包长 / Maximum packet size
    uint8_t bInterval;               ///< 轮询间隔 / Polling interval
  };
#pragma pack(pop)

  /**
   * @brief 绑定端点资源 / Bind endpoint resources
   * @param endpoint_pool 端点池 / Endpoint pool
   * @param start_itf_num 起始接口号 / Start interface number
   */
  virtual void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num) = 0;

  /**
   * @brief 解绑端点资源 / Unbind endpoint resources
   * @param endpoint_pool 端点池 / Endpoint pool
   */
  virtual void UnbindEndpoints(EndpointPool& endpoint_pool) = 0;

  /**
   * @brief 可选：覆盖设备描述符字段 / Optional: override device descriptor fields
   * @param header 设备描述符 / Device descriptor
   * @return OK：已覆盖；NOT_SUPPORT：不支持 / OK: applied; NOT_SUPPORT: not supported
   */
  virtual ErrorCode WriteDeviceDescriptor(DeviceDescriptor& header)
  {
    UNUSED(header);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 可选：设置接口备用设置 / Optional: set interface alternate setting
   * @param itf 接口号 / Interface number
   * @param alt 备用设置 / Alternate setting
   * @return OK：成功；NOT_SUPPORT：不支持 / OK: success; NOT_SUPPORT: not supported
   */
  virtual ErrorCode SetAltSetting(uint8_t itf, uint8_t alt)
  {
    UNUSED(itf);
    return (alt == 0) ? ErrorCode::OK : ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 可选：获取接口备用设置 / Optional: get interface alternate setting
   * @param itf 接口号 / Interface number
   * @param alt 输出：备用设置 / Output: alternate setting
   * @return OK：成功；NOT_SUPPORT：不支持 / OK: success; NOT_SUPPORT: not supported
   */
  virtual ErrorCode GetAltSetting(uint8_t itf, uint8_t& alt)
  {
    UNUSED(itf);
    UNUSED(alt);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 可选：端点归属判定 / Optional: endpoint ownership
   * @param ep_addr 端点地址 / Endpoint address
   * @return true：归属；false：不归属 / true: owned; false: not owned
   */
  virtual bool OwnsEndpoint(uint8_t ep_addr) const
  {
    UNUSED(ep_addr);
    return false;
  }

  /**
   * @brief 最大配置描述符占用 / Maximum bytes required in configuration descriptor
   * @return 最大字节数 / Maximum bytes
   */
  virtual size_t GetMaxConfigSize() = 0;

  /**
   * @brief 接口数量 / Number of interfaces contributed
   * @return 接口数量 / Interface count
   */
  virtual size_t GetInterfaceCount() = 0;

  /**
   * @brief 是否包含 IAD / Whether an IAD is used
   * @return true：包含；false：不包含 / true: used; false: not used
   */
  virtual bool HasIAD() = 0;

 protected:
  /**
   * @brief 获取内部数据缓存 / Get internal data cache
   * @return RawData 数据结构 / RawData
   */
  RawData GetData();

  /**
   * @brief 设置内部数据缓存 / Set internal data cache
   * @param data 数据缓存 / Data cache
   */
  void SetData(RawData data) { data_ = data; }

  friend class ConfigDescriptor;

 private:
  RawData data_{nullptr, 0};  ///< 内部数据缓存 / Internal data cache
};

/**
 * @brief 配置描述符管理与构建器，并聚合 BOS 能力
 *        Configuration descriptor builder with BOS aggregation.
 */
class ConfigDescriptor : public BosManager
{
  using Header = ConfigDescriptorItem::Header;

  /**
   * @brief 单个 configuration 的配置项集合 / Item set for one configuration
   */
  struct Config
  {
    ConfigDescriptorItem** items = nullptr;  ///< 配置项指针表 / Item pointer table
    size_t item_num = 0;                     ///< 配置项数量 / Item count
  };

  static bool IsCompositeConfig(
      const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
          configs);

 public:
  /**
   * @brief 构造函数 / Constructor
   *
   * @param endpoint_pool 端点池 / Endpoint pool
   * @param configs configuration 列表（每个子列表为一个 configuration）
   *                Config list (each sub-list is one configuration)
   * @param bmAttributes 配置属性 / bmAttributes
   * @param bMaxPower 最大电流（单位 2mA）/ Max power (2mA units)
   */
  ConfigDescriptor(
      EndpointPool& endpoint_pool,
      const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
          configs,
      uint8_t bmAttributes = CFG_BUS_POWERED, uint8_t bMaxPower = 50);

  /**
   * @brief 切换当前 configuration / Switch current configuration
   * @param index 配置索引 / Configuration index
   * @return 错误码 / Error code
   */
  ErrorCode SwitchConfig(size_t index);

  /**
   * @brief 绑定当前配置端点 / Bind endpoints for current configuration
   */
  void BindEndpoints();

  /**
   * @brief 解绑当前配置端点 / Unbind endpoints for current configuration
   */
  void UnbindEndpoints();

  /**
   * @brief 构建当前配置描述符 / Build current configuration descriptor
   * @return 错误码 / Error code
   */
  ErrorCode BuildConfigDescriptor();

  /**
   * @brief 是否为复合设备 / Whether composite device
   * @return true：复合设备；false：非复合 / true: composite; false: non-composite
   */
  [[nodiscard]] bool IsComposite() const;

  /**
   * @brief 重建 BOS 缓存 / Rebuild BOS cache
   */
  void RebuildBosCache();

  /**
   * @brief 是否允许覆盖设备描述符 / Whether device descriptor override is allowed
   * @return true：允许；false：不允许 / true: allowed; false: disallowed
   */
  [[nodiscard]] bool CanOverrideDeviceDescriptor() const;

  /**
   * @brief 覆盖设备描述符 / Override device descriptor
   * @param descriptor 设备描述符 / Device descriptor
   * @return 错误码 / Error code
   */
  ErrorCode OverrideDeviceDescriptor(DeviceDescriptor& descriptor);

  /**
   * @brief 获取配置描述符数据 / Get configuration descriptor data
   * @return RawData 数据结构 / RawData
   */
  [[nodiscard]] RawData GetData() const;

  /**
   * @brief 配置数量 / Number of configurations
   * @return 配置数量 / Configuration count
   */
  [[nodiscard]] size_t GetConfigNum() const;

  /**
   * @brief 当前配置索引 / Current configuration index
   * @return 配置索引 / Configuration index
   */
  [[nodiscard]] size_t GetCurrentConfig() const;

  /**
   * @brief 设备状态（GET_STATUS）/ Device status (GET_STATUS)
   * @return 设备状态位 / Device status bits
   */
  [[nodiscard]] uint16_t GetDeviceStatus() const;

  /**
   * @brief 按接口号查找配置项 / Find item by interface number
   * @param index 接口号 / Interface number
   * @return 配置项指针（可能为空）/ Item pointer (nullable)
   */
  [[nodiscard]] ConfigDescriptorItem* FindItemByInterfaceNumber(size_t index) const;

  /**
   * @brief 按端点地址查找配置项 / Find item by endpoint address
   * @param addr 端点地址 / Endpoint address
   * @return 配置项指针（可能为空）/ Item pointer (nullable)
   */
  [[nodiscard]] ConfigDescriptorItem* FindItemByEndpointAddress(uint8_t addr) const;

 private:
  bool ep_assigned_ = false;  ///< 端点是否已绑定 / Whether endpoints are assigned

  EndpointPool& endpoint_pool_;  ///< 端点池引用 / Endpoint pool reference
  uint8_t current_cfg_ = 0;      ///< 当前配置索引 / Current configuration index
  uint8_t i_configuration_ = 0;  ///< 配置字符串索引 / Configuration string index
  uint8_t bm_attributes_ = CFG_BUS_POWERED;  ///< 配置属性 / bmAttributes
  uint8_t b_max_power_ = 50;  ///< 最大电流（2mA 单位）/ Max power (2mA units)

  const bool COMPOSITE = false;  ///< 是否为复合设备 / Whether composite device
  const size_t CFG_NUM = 0;      ///< 配置数量 / Configuration count
  Config* items_ = nullptr;      ///< 配置项集合 / Configuration item set

  RawData buffer_{nullptr, 0};  ///< 配置描述符缓冲区 / Configuration descriptor buffer
  size_t buffer_index_ = 0;     ///< 缓冲区写入位置 / Buffer write index
};

}  // namespace LibXR::USB
