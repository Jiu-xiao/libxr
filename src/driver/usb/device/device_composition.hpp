#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "device_class.hpp"
#include "libxr_type.hpp"
#include "usb/core/bos.hpp"
#include "usb/core/desc_cfg.hpp"
#include "usb/core/desc_str.hpp"
#include "usb/core/ep_pool.hpp"

namespace LibXR::USB
{
/**
 * @brief USB 组合管理器：class 列表、字符串、BOS、配置切换
 *        USB composition manager: class list, strings, BOS, and configuration switching.
 */
class DeviceComposition
{
  struct ConfigItems
  {
    ConfigDescriptorItem** items = nullptr;  ///< 配置项指针表 / Item pointer table
    size_t item_num = 0;                     ///< 配置项数量 / Item count
  };

 public:
  DeviceComposition(
      EndpointPool& endpoint_pool,
      const std::initializer_list<const DescriptorStrings::LanguagePack*>& lang_list,
      const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
          configs,
      ConstRawData uid = {nullptr, 0}, uint8_t bmAttributes = CFG_BUS_POWERED,
      uint8_t bMaxPower = 50);

  ~DeviceComposition();

  void Init(bool in_isr);
  void Deinit(bool in_isr);

  ErrorCode SwitchConfig(size_t index, bool in_isr);

  ErrorCode BuildConfigDescriptor();
  [[nodiscard]] RawData GetConfigDescriptor() const;

  [[nodiscard]] ConstRawData GetBosDescriptor();
  ErrorCode ProcessBosVendorRequest(bool in_isr, const SetupPacket* setup,
                                    BosVendorResult& result);

  ErrorCode GetStringDescriptor(uint8_t string_index, uint16_t lang, ConstRawData& data);

  [[nodiscard]] bool IsComposite() const;
  [[nodiscard]] bool CanOverrideDeviceDescriptor() const;
  ErrorCode OverrideDeviceDescriptor(DeviceDescriptor& descriptor);

  [[nodiscard]] size_t GetConfigNum() const;
  [[nodiscard]] size_t GetCurrentConfig() const;
  [[nodiscard]] uint16_t GetDeviceStatus() const;

  [[nodiscard]] DeviceClass* FindClassByInterfaceNumber(size_t index) const;
  [[nodiscard]] DeviceClass* FindClassByEndpointAddress(uint8_t addr) const;

 private:
  [[nodiscard]] const ConfigItems& CurrentConfigItems() const;

  void BindEndpoints(bool in_isr);
  void UnbindEndpoints(bool in_isr);
  void RebuildBosCache();
  void RegisterInterfaceStrings();
  ErrorCode GenerateInterfaceString(uint8_t string_index, ConstRawData& data);

  bool ep_assigned_ = false;  ///< 端点是否已绑定 / Whether endpoints are assigned

  EndpointPool& endpoint_pool_;  ///< 端点池引用 / Endpoint pool reference
  uint8_t current_cfg_ = 0;      ///< 当前配置索引 / Current configuration index
  uint8_t i_configuration_ = 0;  ///< 配置字符串索引 / Configuration string index
  uint8_t bm_attributes_ = CFG_BUS_POWERED;  ///< 配置属性 / bmAttributes
  uint8_t b_max_power_ = 50;  ///< 最大电流（2mA 单位）/ Max power (2mA units)

  const bool composite_ = false;   ///< 是否为复合设备 / Whether composite device
  const size_t config_num_ = 0;    ///< 配置数量 / Configuration count
  ConfigItems* items_ = nullptr;  ///< 配置项集合 / Configuration item set

  DescriptorStrings strings_;  ///< 字符串描述符管理 / String descriptor manager
  const char** interface_strings_ = nullptr;  ///< 接口字符串表 / Interface string table
  size_t interface_string_count_ = 0;  ///< 已注册接口字符串数量 / Registered interface strings
  size_t interface_string_capacity_ = 0;  ///< 接口字符串容量 / Interface string capacity
  RawData interface_string_buffer_{nullptr, 0};  ///< 接口字符串缓冲区 / Interface string buffer
  BosManager bos_;             ///< BOS 聚合管理 / BOS aggregation manager
  ConfigDescriptor config_desc_;  ///< 配置描述符构造器 / Configuration descriptor builder
};

}  // namespace LibXR::USB
