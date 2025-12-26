#pragma once
#include <cstring>

#include "libxr_type.hpp"
#include "usb/core/core.hpp"
#include "usb/core/desc_cfg.hpp"
#include "usb/core/ep_pool.hpp"

namespace LibXR::USB
{
class DeviceCore;
/**
 * @class DeviceClass
 * @brief USB 设备类接口基类，所有自定义 USB 类（如 HID、CDC、MSC）都需派生自本类。
 *        USB device class base interface, all custom device classes (HID, CDC, MSC,
 * etc.) should derive from this.
 *
 * @note 仅供 DeviceCore 内部驱动和派生类扩展，普通用户无需直接使用。
 *       Only for internal drivers and derived classes, users do not need to use it
 * directly.
 */
class DeviceClass : public ConfigDescriptorItem
{
 protected:
  /**
   * @brief 控制请求结果结构体 / Structure for control transfer results
   */
  struct RequestResult
  {
    RawData read_data{nullptr, 0};  ///< 设备返回给主机的数据 / Data to read (to host)
    ConstRawData write_data{nullptr,
                            0};  ///< 主机写入设备的数据 / Data to write (from host)
    bool read_zlp = false;       ///< 读操作是否需要发送 0 长度包 / Send ZLP after read
    bool write_zlp = false;      ///< 写操作是否需要发送 0 长度包 / Send ZLP after write
  };

  /**
   * @brief 处理标准请求 GET_DESCRIPTOR
   *        Handle standard request GET_DESCRIPTOR
   * @param in_isr 是否在中断中 / In ISR
   * @param bRequest 请求码 / Request code
   * @param wValue 请求值 / wValue field
   * @param wLength 请求长度 / Requested length
   * @param need_write 返回数据指针 / Output data
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode OnGetDescriptor(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                    uint16_t wLength, ConstRawData &need_write)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(wValue);
    UNUSED(wLength);
    UNUSED(need_write);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 处理类特定请求（Class-specific Request）
   *        Handle class-specific request
   * @param in_isr 是否在中断中 / In ISR
   * @param bRequest 请求码 / Request code
   * @param wValue 请求值 / wValue field
   * @param wLength 请求长度 / Requested length
   * @param wIndex 请求索引 / Request index
   * @param result 读写数据结构体 / Data result struct
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode OnClassRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                   uint16_t wLength, uint16_t wIndex,
                                   RequestResult &result)
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
   * @brief 处理类请求的数据阶段
   *        Handle data stage for class request
   * @param in_isr 是否在中断中 / In ISR
   * @param bRequest 请求码 / Request code
   * @param data 主机写入数据 / Data from host
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode OnClassData(bool in_isr, uint8_t bRequest, LibXR::ConstRawData &data)
  {
    UNUSED(in_isr);
    UNUSED(bRequest);
    UNUSED(data);
    return ErrorCode::NOT_SUPPORT;
  }

  /**
   * @brief 处理厂商自定义请求 / Handle vendor request
   *
   * @param in_isr 是否在中断中 / In ISR
   * @param bRequest 请求码 / Request code
   * @param wValue 请求值 / wValue field
   * @param wLength 请求长度 / Requested length
   * @param wIndex 请求索引 / Request index
   * @param result 读写数据结构体 / Data result struct
   * @return ErrorCode
   */
  virtual ErrorCode OnVendorRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                                    uint16_t wLength, uint16_t wIndex,
                                    RequestResult &result)
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
};

/**
 * @class DeviceCore
 * @brief USB 设备协议栈核心类，负责端点 0 控制传输及配置、描述符、标准请求处理等。
 *        USB device protocol stack core class. Handles EP0 control transfers,
 * descriptors, configurations, and standard/class/vendor requests.
 *
 * @note 用户无需直接操作，一般由派生类自动管理。
 *       Users do not need to operate it directly, it is automatically managed by derived
 * classes.
 */
class DeviceCore
{
 public:
  /**
   * @brief 控制传输状态枚举 / Control transfer context enum
   */
  enum Context : uint8_t
  {
    UNKNOW,      ///< 未知 / Unknown
    SETUP,       ///< SETUP 阶段 / Setup stage
    DATA_OUT,    ///< 数据 OUT 阶段 / Data out stage
    STATUS_OUT,  ///< 状态 OUT 阶段 / Status out stage
    DATA_IN,     ///< 数据 IN 阶段 / Data in stage
    STATUS_IN,   ///< 状态 IN 阶段 / Status in stage
    ZLP          ///< 0 长度包 / Zero-length packet
  };

  /**
   * @brief 构造函数 / Constructor
   * @param ep_pool 端点池 / Endpoint pool
   * @param spec USB 规范版本 / USB spec version
   * @param speed 速度等级 / Device speed
   * @param packet_size EP0 包大小 / EP0 packet size
   * @param vid 厂商 ID / Vendor ID
   * @param pid 产品 ID / Product ID
   * @param bcd 设备版本号 / Device version (BCD)
   * @param lang_list 语言包列表 / Language packs
   * @param configs 配置描述符列表 / Config descriptor items
   * @param uid UID / Unique ID
   */
  DeviceCore(
      EndpointPool &ep_pool, USBSpec spec, Speed speed,
      DeviceDescriptor::PacketSize0 packet_size, uint16_t vid, uint16_t pid, uint16_t bcd,
      const std::initializer_list<const DescriptorStrings::LanguagePack *> &lang_list,
      const std::initializer_list<const std::initializer_list<ConfigDescriptorItem *>>
          &configs,
      ConstRawData uid = {nullptr, 0});

  /**
   * @brief 初始化 USB 设备 / Initialize USB device
   * @return void
   */
  virtual void Init();

  /**
   * @brief 反初始化 USB 设备 / Deinitialize USB device
   *
   */
  virtual void Deinit();

  /**
   * @brief 启动 USB 设备 / Start USB device
   *
   */
  virtual void Start() = 0;

  /**
   * @brief 停止 USB 设备 / Stop USB device
   *
   */
  virtual void Stop() = 0;

  /**
   * @brief 处理主机发送的 SETUP 包
   *        Handle USB setup packet from host
   * @param in_isr 是否在中断中 / In ISR
   * @param setup SETUP 包指针 / Setup packet pointer
   * @return void
   */
  void OnSetupPacket(bool in_isr, const SetupPacket *setup);

 private:
  /**
   * @brief EP0 OUT 端点传输完成回调（静态）
   *        EP0 OUT endpoint transfer complete (static)
   */
  static void OnEP0OutCompleteStatic(bool in_isr, DeviceCore *self,
                                     LibXR::ConstRawData &data);

  /**
   * @brief EP0 IN 端点传输完成回调（静态）
   *        EP0 IN endpoint transfer complete (static)
   */
  static void OnEP0InCompleteStatic(bool in_isr, DeviceCore *self,
                                    LibXR::ConstRawData &data);

  /**
   * @brief 检查 USB 组合是否合法 / Check if USB combination is valid
   */
  static bool IsValidUSBCombination(USBSpec spec, Speed speed,
                                    DeviceDescriptor::PacketSize0 packet_size);

  /**
   * @brief EP0 OUT 端点传输完成回调 / EP0 OUT endpoint transfer complete
   */
  void OnEP0OutComplete(bool in_isr, LibXR::ConstRawData &data);

  /**
   * @brief EP0 IN 端点传输完成回调 / EP0 IN endpoint transfer complete
   */
  void OnEP0InComplete(bool in_isr, LibXR::ConstRawData &data);

  /**
   * @brief 接收 0 长度包 / Receive zero-length packet (ZLP)
   * @param context 传输上下文 / Transfer context
   */
  void ReadZLP(Context context = Context::ZLP);

  /**
   * @brief 发送 0 长度包 / Send zero-length packet (ZLP)
   * @param context 传输上下文 / Transfer context
   */
  void WriteZLP(Context context = Context::ZLP);

  /**
   * @brief 向主机发送 EP0 数据包
   *        Send data packet to host via EP0
   * @param data 数据指针和长度 / Data and length
   * @param packet_max_length 最大包长度 / Max packet size
   * @param request_size 请求长度（可选）/ Request size (optional)
   * @param early_read_zlp 是否提前读取 ZLP（可选）/ Read ZLP early (optional)
   */
  void DevWriteEP0Data(LibXR::ConstRawData data, size_t packet_max_length,
                       size_t request_size = 0, bool early_read_zlp = false);

  /**
   * @brief 接收主机发送的 EP0 数据包
   *        Receive data packet from host via EP0
   * @param data 数据指针和长度 / Data and length
   * @param packet_max_length 最大包长度 / Max packet size
   */
  void DevReadEP0Data(LibXR::RawData data, size_t packet_max_length);

  /**
   * @brief 处理标准请求
   *        Process standard USB requests
   */
  ErrorCode ProcessStandardRequest(bool in_isr, const SetupPacket *&setup,
                                   RequestDirection direction, Recipient recipient);

  /**
   * @brief 返回状态响应 / Respond with status stage
   */
  ErrorCode RespondWithStatus(const SetupPacket *setup, Recipient recipient);

  /**
   * @brief 清除功能特性 / Clear USB feature
   */
  ErrorCode ClearFeature(const SetupPacket *setup, Recipient recipient);

  /**
   * @brief 启用功能特性 / Apply USB feature
   */
  ErrorCode ApplyFeature(const SetupPacket *setup, Recipient recipient);

  /**
   * @brief 发送描述符 / Send USB descriptor
   */
  ErrorCode SendDescriptor(bool in_isr, const SetupPacket *setup, Recipient recipient);

  /**
   * @brief 预设地址变更 / Prepare address change
   */
  ErrorCode PrepareAddressChange(uint16_t address);

  /**
   * @brief 切换配置 / Switch USB configuration
   */
  ErrorCode SwitchConfiguration(uint16_t value);

  /**
   * @brief 发送配置响应 / Send configuration
   */
  ErrorCode SendConfiguration();

  /**
   * @brief 设置控制端点为 STALL / Stall control endpoint
   */
  void StallControlEndpoint();

  /**
   * @brief 清除控制端点 STALL / Clear control endpoint stall
   */
  void ClearControlEndpointStall();

  /**
   * @brief 处理类请求 / Process class-specific request
   */
  ErrorCode ProcessClassRequest(bool in_isr, const SetupPacket *setup,
                                RequestDirection direction, Recipient recipient);

  /**
   * @brief 处理厂商请求 / Process vendor-specific request
   */
  ErrorCode ProcessVendorRequest(bool in_isr, const SetupPacket *&setup,
                                 RequestDirection direction, Recipient recipient);

  /**
   * @brief 设置设备地址（必须由子类实现）
   *        Set device address (must be implemented by subclass)
   * @param address 新地址 / New device address
   * @param state 当前状态 / Current state
   * @return ErrorCode 错误码 / Error code
   */
  virtual ErrorCode SetAddress(uint8_t address, Context state) = 0;

  /**
   * @brief 启用远程唤醒功能（SetFeature: DEVICE_REMOTE_WAKEUP）
   *        Enable remote wakeup (via SetFeature)
   */
  virtual void EnableRemoteWakeup() {}

  /**
   * @brief 禁用远程唤醒功能（ClearFeature: DEVICE_REMOTE_WAKEUP）
   *        Disable remote wakeup (via ClearFeature)
   */
  virtual void DisableRemoteWakeup() {}

  /**
   * @brief 判断当前是否允许远程唤醒
   *        Query if remote wakeup is enabled
   */
  virtual bool IsRemoteWakeupEnabled() const { return false; }

  /**
   * @brief 获取当前 USB 速度 / Get current USB speed
   * @return Speed 当前速度枚举 / Current speed
   */
  Speed GetSpeed() const;

 private:
  ConfigDescriptor config_desc_;  ///< 配置描述符 / Config descriptor
  DeviceDescriptor device_desc_;  ///< 设备描述符 / Device descriptor
  DescriptorStrings strings_;     ///< 字符串描述符管理 / String descriptors

  struct
  {
    EndpointPool &pool;        ///< 端点池 / Endpoint pool
    Endpoint *in0 = nullptr;   ///< 控制 IN 端点指针 / Control IN endpoint
    Endpoint *out0 = nullptr;  ///< 控制 OUT 端点指针 / Control OUT endpoint
    LibXR::Callback<LibXR::ConstRawData &> ep0_in_cb;  ///< EP0 IN 回调 / EP0 IN callback
    LibXR::Callback<LibXR::ConstRawData &>
        ep0_out_cb;  ///< EP0 OUT 回调 / EP0 OUT callback
  } endpoint_;

  struct
  {
    bool inited = false;             ///< 是否初始化 / Initialized
    Speed speed = Speed::FULL;       ///< 当前速度 / Current speed
    Context in0;                     ///< IN0 状态 / IN0 context
    Context out0;                    ///< OUT0 状态 / OUT0 context
    ConstRawData write_remain;       ///< 剩余写数据 / Remaining write data
    RawData read_remain;             ///< 剩余读数据 / Remaining read data
    uint8_t pending_addr = 0xFF;     ///< 待设置的新地址 / Pending device address
    uint8_t *out0_buffer = nullptr;  ///< OUT0 缓冲区 / OUT0 buffer
    bool need_write_zlp = false;     ///< 是否需要写 ZLP / Need to write ZLP
  } state_;

  struct
  {
    bool write = false;                ///< 是否写操作 / Write operation
    bool read = false;                 ///< 是否读操作 / Read operation
    DeviceClass *class_ptr = nullptr;  ///< 当前类指针 / Current device class pointer
    uint8_t b_request = 0;             ///< 当前请求码 / Current request code
    ConstRawData data;                 ///< 当前数据 / Current data
  } class_req_;
};
}  // namespace LibXR::USB
