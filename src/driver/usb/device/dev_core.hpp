#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "libxr_type.hpp"
#include "usb/core/core.hpp"
#include "usb/core/desc_cfg.hpp"
#include "usb/core/ep_pool.hpp"

namespace LibXR::USB
{
class DeviceCore;

/**
 * @brief USB 设备类接口基类 / USB device class interface base
 *
 * 所有自定义 USB 类（HID/CDC/MSC 等）应派生自本类。
 * All custom USB classes (HID/CDC/MSC, etc.) should derive from this class.
 *
 * @note 该类主要供 DeviceCore 驱动及派生类扩展使用。
 *       This class is mainly used by DeviceCore and derived-class extensions.
 */
class DeviceClass : public ConfigDescriptorItem
{
 public:
  /**
   * @brief 构造：传入本类提供的 BOS capabilities（对象指针列表）
   *        Constructor: pass BOS capabilities provided by this class (pointer list).
   *
   * @param bos_caps BOS capability 指针列表 / BOS capability pointer list
   *
   * @note 基类会动态分配一个指针数组保存这些 capability 指针。
   *       capability 对象本身生命周期应由派生类成员/静态对象管理。
   */
  explicit DeviceClass(std::initializer_list<BosCapability*> bos_caps = {});

  /**
   * @brief 析构函数 / Destructor
   */
  ~DeviceClass() override;

 protected:
  /**
   * @brief 控制请求（Class/Vendor）处理结果 / Control request (Class/Vendor) handling
   * result
   *
   */
  struct ControlTransferResult
  {
    RawData read_data{nullptr, 0};  ///< OUT 数据阶段接收缓冲区（Host->Device）/ OUT data
                                    ///< stage buffer (Host->Device)
    ConstRawData write_data{nullptr, 0};  ///< IN 数据阶段发送数据（Device->Host）/ IN
                                          ///< data stage payload (Device->Host)
    bool read_zlp = false;   ///< 期望 STATUS OUT（arm OUT 等待 ZLP）/ Expect STATUS OUT
                             ///< (arm OUT for ZLP)
    bool write_zlp = false;  ///< 发送 STATUS IN（发送 ZLP）/ Send STATUS IN (send ZLP)

    RawData& OutData() { return read_data; }
    const RawData& OutData() const { return read_data; }

    ConstRawData& InData() { return write_data; }
    const ConstRawData& InData() const { return write_data; }

    bool& ExpectStatusOutZLP() { return read_zlp; }
    bool ExpectStatusOutZLP() const { return read_zlp; }

    bool& SendStatusInZLP() { return write_zlp; }
    bool SendStatusInZLP() const { return write_zlp; }
  };

  /**
   * @brief 处理标准请求 GET_DESCRIPTOR（类特定描述符）
   *        Handle standard GET_DESCRIPTOR request (class-specific descriptors).
   *
   * @param in_isr   是否在 ISR / Whether in ISR context
   * @param bRequest 请求码 / Request code
   * @param wValue   wValue / wValue
   * @param wLength  wLength / wLength
   * @param out_data 输出：返回给主机的描述符数据（Device->Host）
   *                 Output: descriptor data to return (Device->Host)
   * @return 错误码 / Error code
   */
  virtual ErrorCode OnGetDescriptor(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                    uint16_t wLength, ConstRawData& out_data)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(wValue);
    UNUSED(wLength);
    UNUSED(out_data);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 处理 Class-specific 请求（Setup stage）/ Handle class-specific request (Setup
   * stage)
   *
   * @param in_isr   是否在 ISR / Whether in ISR context
   * @param bRequest 请求码 / Request code
   * @param wValue   wValue / wValue
   * @param wLength  wLength / wLength
   * @param wIndex   wIndex / wIndex
   * @param result   输出：控制传输结果 / Output: control transfer result
   * @return 错误码 / Error code
   */
  virtual ErrorCode OnClassRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                   uint16_t wLength, uint16_t wIndex,
                                   ControlTransferResult& result)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(wValue);
    UNUSED(wLength);
    UNUSED(wIndex);
    UNUSED(result);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 处理 Class request 数据阶段 / Handle class request data stage
   *
   * @param in_isr   是否在 ISR / Whether in ISR context
   * @param bRequest 请求码 / Request code
   * @param data     数据阶段数据 / Data stage payload
   * @return 错误码 / Error code
   *
   * @note 当 OnClassRequest 返回需要 OUT/IN data stage 时，数据阶段完成后回调此函数。
   *       When OnClassRequest requires an OUT/IN data stage, this callback is invoked
   * after completion.
   */
  virtual ErrorCode OnClassData(bool in_isr, uint8_t bRequest, LibXR::ConstRawData& data)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(data);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 处理 Vendor request（Setup stage）/ Handle vendor request (Setup stage)
   *
   * @param in_isr   是否在 ISR / Whether in ISR context
   * @param bRequest 请求码 / Request code
   * @param wValue   wValue / wValue
   * @param wLength  wLength / wLength
   * @param wIndex   wIndex / wIndex
   * @param result   输出：控制传输结果 / Output: control transfer result
   * @return 错误码 / Error code
   */
  virtual ErrorCode OnVendorRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                    uint16_t wLength, uint16_t wIndex,
                                    ControlTransferResult& result)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(wValue);
    UNUSED(wLength);
    UNUSED(wIndex);
    UNUSED(result);
    return ErrorCode::NOT_SUPPORT;
  }

  friend class DeviceCore;

 private:
  BosCapability** bos_caps_ =
      nullptr;              ///< BOS capability 指针表 / BOS capability pointer table
  size_t bos_cap_num_ = 0;  ///< BOS capability 数量 / BOS capability count
};

/**
 * @brief USB 设备协议栈核心：EP0 控制传输、描述符、配置、标准/类/厂商请求
 *        USB device core: EP0 control transfer, descriptors, configuration,
 *        and standard/class/vendor requests.
 */
class DeviceCore
{
 public:
  /**
   * @brief 控制传输上下文 / Control transfer context
   *
   * 说明：保留历史拼写 UNKNOW，同时增加同值别名 UNKNOWN，便于代码可读性提升。
   * Note: keep legacy spelling UNKNOWN and add alias UNKNOWN for readability.
   */
  enum Context : uint8_t
  {
    UNKNOWN = 0,  ///< 未知 / Unknown
    SETUP,        ///< Setup stage / Setup stage
    DATA_OUT,     ///< OUT data stage / OUT data stage
    STATUS_OUT,   ///< OUT status stage / OUT status stage
    DATA_IN,      ///< IN data stage / IN data stage
    STATUS_IN,    ///< IN status stage / IN status stage
    ZLP           ///< ZLP stage marker / ZLP stage marker
  };

  /**
   * @brief 构造函数 / Constructor
   *
   * @param ep_pool     端点池 / Endpoint pool
   * @param spec        USB 规范版本 / USB specification
   * @param speed       设备速度 / Device speed
   * @param packet_size EP0 包长 / EP0 packet size
   * @param vid         Vendor ID / Vendor ID
   * @param pid         Product ID / Product ID
   * @param bcd         设备版本号（BCD）/ Device release number (BCD)
   * @param lang_list   字符串语言包列表 / String language pack list
   * @param configs     配置列表（每个子列表为一个 configuration）
   *                   Config list (each sub-list is one configuration)
   * @param uid         UID 原始数据（可选）/ UID raw data (optional)
   */
  DeviceCore(
      EndpointPool& ep_pool, USBSpec spec, Speed speed,
      DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid, uint16_t bcd,
      const std::initializer_list<const DescriptorStrings::LanguagePack*>& lang_list,
      const std::initializer_list<const std::initializer_list<ConfigDescriptorItem*>>&
          configs,
      ConstRawData uid = {nullptr, 0});

  /**
   * @brief 初始化 / Initialize
   *
   * @param in_isr 是否在 ISR / Whether in ISR context
   */
  virtual void Init(bool in_isr);

  /**
   * @brief 反初始化 / Deinitialize
   *
   * @param in_isr 是否在 ISR / Whether in ISR context
   */
  virtual void Deinit(bool in_isr);

  /**
   * @brief 启动设备（由子类实现）/ Start device (implemented by derived class)
   *
   * @param in_isr 是否在 ISR / Whether in ISR context
   */
  virtual void Start(bool in_isr) = 0;

  /**
   * @brief 停止设备（由子类实现）/ Stop device (implemented by derived class)
   *
   * @param in_isr 是否在 ISR / Whether in ISR context
   */
  virtual void Stop(bool in_isr) = 0;

  /**
   * @brief 处理 Setup 包 / Handle Setup packet
   * @param in_isr 是否在 ISR / Whether in ISR context
   * @param setup  Setup 包 / Setup packet
   */
  void OnSetupPacket(bool in_isr, const SetupPacket* setup);

 protected:
  /**
   * @brief 设置设备地址（由子类实现）
   *        Set device address (implemented by derived class).
   *
   * @param address 设备地址 / Device address
   * @param state   当前上下文 / Current context
   * @return 错误码 / Error code
   */
  virtual ErrorCode SetAddress(uint8_t address, Context state) = 0;

  /**
   * @brief 启用远程唤醒 / Enable remote wakeup
   */
  virtual void EnableRemoteWakeup() {}

  /**
   * @brief 禁用远程唤醒 / Disable remote wakeup
   */
  virtual void DisableRemoteWakeup() {}

  /**
   * @brief 远程唤醒是否启用 / Whether remote wakeup is enabled
   * @return true：已启用；false：未启用 / true: enabled; false: disabled
   */
  virtual bool IsRemoteWakeupEnabled() const { return false; }

  /**
   * @brief 获取设备速度 / Get device speed
   * @return 设备速度 / Device speed
   */
  [[nodiscard]] Speed GetSpeed() const;

 private:
  static void OnEP0OutCompleteStatic(bool in_isr, DeviceCore* self,
                                     LibXR::ConstRawData& data);
  static void OnEP0InCompleteStatic(bool in_isr, DeviceCore* self,
                                    LibXR::ConstRawData& data);

  static bool IsValidUSBCombination(USBSpec spec, Speed speed,
                                    DeviceDescriptor::PacketSize0 packet_size);

  void OnEP0OutComplete(bool in_isr, LibXR::ConstRawData& data);
  void OnEP0InComplete(bool in_isr, LibXR::ConstRawData& data);

  void ReadZLP(Context context = Context::ZLP);
  void WriteZLP(Context context = Context::ZLP);

  void DevWriteEP0Data(LibXR::ConstRawData data, size_t packet_max_length,
                       size_t request_size = 0, bool early_read_zlp = false);
  void DevReadEP0Data(LibXR::RawData data, size_t packet_max_length);

  ErrorCode ProcessStandardRequest(bool in_isr, const SetupPacket*& setup,
                                   RequestDirection direction, Recipient recipient);

  ErrorCode RespondWithStatus(const SetupPacket* setup, Recipient recipient);
  ErrorCode ClearFeature(const SetupPacket* setup, Recipient recipient);
  ErrorCode ApplyFeature(const SetupPacket* setup, Recipient recipient);
  ErrorCode SendDescriptor(bool in_isr, const SetupPacket* setup, Recipient recipient);
  ErrorCode PrepareAddressChange(uint16_t address);
  ErrorCode SwitchConfiguration(uint16_t value, bool in_isr);
  ErrorCode SendConfiguration();

  void StallControlEndpoint();
  void ClearControlEndpointStall();

  ErrorCode ProcessClassRequest(bool in_isr, const SetupPacket* setup,
                                RequestDirection direction, Recipient recipient);

  ErrorCode ProcessVendorRequest(bool in_isr, const SetupPacket*& setup,
                                 RequestDirection direction, Recipient recipient);

 private:
  ConfigDescriptor config_desc_;  ///< 配置描述符管理器 / Configuration descriptor manager
  DeviceDescriptor device_desc_;  ///< 设备描述符 / Device descriptor
  DescriptorStrings strings_;     ///< 字符串描述符管理器 / String descriptor manager

  struct
  {
    EndpointPool& pool;        ///< 端点池引用 / Endpoint pool reference
    Endpoint* in0 = nullptr;   ///< EP0 IN 端点 / EP0 IN endpoint
    Endpoint* out0 = nullptr;  ///< EP0 OUT 端点 / EP0 OUT endpoint
    LibXR::Callback<LibXR::ConstRawData&> ep0_in_cb;  ///< EP0 IN 回调 / EP0 IN callback
    LibXR::Callback<LibXR::ConstRawData&>
        ep0_out_cb;  ///< EP0 OUT 回调 / EP0 OUT callback
  } endpoint_;

  struct
  {
    bool inited = false;                    ///< 是否已初始化 / Whether initialized
    Speed speed = Speed::FULL;              ///< 设备速度 / Device speed
    Context in0 = Context::UNKNOWN;         ///< EP0 IN 上下文 / EP0 IN context
    Context out0 = Context::UNKNOWN;        ///< EP0 OUT 上下文 / EP0 OUT context
    ConstRawData write_remain{nullptr, 0};  ///< IN 剩余待发送 / Remaining IN payload
    RawData read_remain{nullptr, 0};        ///< OUT 剩余待接收 / Remaining OUT buffer
    uint8_t pending_addr = 0xFF;            ///< 待生效地址 / Pending address
    uint8_t* out0_buffer = nullptr;         ///< EP0 OUT 缓冲区 / EP0 OUT buffer
    bool need_write_zlp = false;            ///< 是否需要发送 ZLP / Whether to send ZLP
  } state_;

  struct
  {
    bool write = false;  ///< 是否存在 IN 数据阶段 / Whether IN data stage exists
    bool read = false;   ///< 是否存在 OUT 数据阶段 / Whether OUT data stage exists
    DeviceClass* class_ptr = nullptr;  ///< 当前处理类 / Current class handler
    uint8_t b_request = 0;             ///< 当前请求码 / Current request code
    ConstRawData data{nullptr, 0};     ///< 数据阶段数据 / Data stage payload
  } class_req_;
};

}  // namespace LibXR::USB
