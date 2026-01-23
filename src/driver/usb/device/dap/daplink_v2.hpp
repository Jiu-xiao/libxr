#pragma once

#include <cstddef>
#include <cstdint>

#include "daplink_v2_def.hpp"  // LibXR::USB::DapLinkV2Def
#include "debug/swd.hpp"       // LibXR::Debug::Swd
#include "dev_core.hpp"
#include "gpio.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "timebase.hpp"
#include "usb/core/desc_cfg.hpp"
#include "winusb_msos20.hpp"  // LibXR::USB::WinUsbMsOs20

namespace LibXR::USB
{
/**
 * @brief CMSIS-DAP v2（Bulk）USB 类（仅 SWD，nRESET 可选）
 *        CMSIS-DAP v2 (Bulk) USB class (SWD-only, optional nRESET control).
 *
 * - CMSIS-DAP v2 Bulk 传输（2x Bulk EP: IN/OUT）
 *   CMSIS-DAP v2 Bulk transport (2x Bulk EP: IN/OUT).
 * - SWD 后端由注入的 LibXR::Debug::Swd 实例提供
 *   SWD backend via injected LibXR::Debug::Swd instance.
 * - SWJ_Pins (0x10) 支持 SWCLK/SWDIO/nRESET：
 *   SWJ_Pins (0x10) supports SWCLK/SWDIO/nRESET:
 *   - 对可读引脚返回真实电平；不可读/不存在引脚返回 shadow 状态
 *     For readable pins, return real level; for absent/unreadable pins, return shadow
 * state.
 * - SWJ_Clock 默认 1MHz
 *   SWJ_Clock default 1MHz.
 *
 * WinUSB（MS OS 2.0）支持：
 * WinUSB (MS OS 2.0) support:
 * - 通过 BOS 提供 Platform Capability: MS OS 2.0
 *   Provide BOS (Platform Capability: MS OS 2.0).
 * - 提供 MS OS 2.0 descriptor set，CompatibleID="WINUSB"
 *   Provide MS OS 2.0 descriptor set with CompatibleID "WINUSB".
 * - 提供 DeviceInterfaceGUIDs（REG_MULTI_SZ）供用户态枚举/打开
 *   Provide DeviceInterfaceGUIDs (REG_MULTI_SZ) for user-mode enumeration/open.
 */
class DapLinkV2Class : public DeviceClass
{
 public:
  /**
   * @brief Info 字符串集合 / Info string set
   */
  struct InfoStrings
  {
    const char* vendor = nullptr;        ///< Vendor 字符串 / Vendor string
    const char* product = nullptr;       ///< Product 字符串 / Product string
    const char* serial = nullptr;        ///< Serial 字符串 / Serial string
    const char* firmware_ver = nullptr;  ///< Firmware version / Firmware version

    const char* device_vendor = nullptr;   ///< Device vendor / Device vendor
    const char* device_name = nullptr;     ///< Device name / Device name
    const char* board_vendor = nullptr;    ///< Board vendor / Board vendor
    const char* board_name = nullptr;      ///< Board name / Board name
    const char* product_fw_ver = nullptr;  ///< Product FW version / Product FW version
  };

 public:
  /**
   * @brief 构造函数 / Constructor
   *
   * @param swd_link        SWD 链路对象引用 / SWD link reference
   * @param nreset_gpio     可选 nRESET GPIO / Optional nRESET GPIO
   * @param data_in_ep_num  Bulk IN 端点号（可自动分配）/ Bulk IN EP number (auto allowed)
   * @param data_out_ep_num Bulk OUT 端点号（可自动分配）/ Bulk OUT EP number (auto
   * allowed)
   */
  explicit DapLinkV2Class(
      LibXR::Debug::Swd& swd_link, LibXR::GPIO* nreset_gpio = nullptr,
      Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
      Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO);

  /**
   * @brief 虚析构函数 / Virtual destructor
   */
  ~DapLinkV2Class() override = default;

  DapLinkV2Class(const DapLinkV2Class&) = delete;
  DapLinkV2Class& operator=(const DapLinkV2Class&) = delete;

 public:
  /**
   * @brief 设置 Info 字符串 / Set info strings
   * @param info 字符串集合 / String set
   */
  void SetInfoStrings(const InfoStrings& info);

  /**
   * @brief 获取内部状态 / Get internal state
   * @return State 引用 / State reference
   */
  const LibXR::USB::DapLinkV2Def::State& GetState() const;

  /**
   * @brief 是否已初始化 / Whether initialized
   * @return true 已初始化 / Initialized
   */
  bool IsInited() const;

 protected:
  /**
   * @brief 绑定端点资源 / Bind endpoint resources
   * @param endpoint_pool 端点池 / Endpoint pool
   * @param start_itf_num 起始接口号 / Start interface number
   */
  void BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num) override;

  /**
   * @brief 解绑端点资源 / Unbind endpoint resources
   * @param endpoint_pool 端点池 / Endpoint pool
   */
  void UnbindEndpoints(EndpointPool& endpoint_pool) override;

  /**
   * @brief 接口数量 / Number of interfaces contributed
   * @return 接口数量 / Interface count
   */
  size_t GetInterfaceCount() override;

  /**
   * @brief 是否包含 IAD / Whether an IAD is used
   * @return true 使用 IAD / Uses IAD
   */
  bool HasIAD() override;

  /**
   * @brief 端点归属判定 / Endpoint ownership
   * @param ep_addr 端点地址 / Endpoint address
   * @return true 属于本类 / Owned by this class
   */
  bool OwnsEndpoint(uint8_t ep_addr) const override;

  /**
   * @brief 最大配置描述符占用 / Maximum bytes required in configuration descriptor
   * @return 最大字节数 / Maximum bytes
   */
  size_t GetMaxConfigSize() override;

  /**
   * @brief 获取 WinUSB MS OS 2.0 描述符集合 / Get WinUSB MS OS 2.0 descriptor set
   * @return ConstRawData 描述符集合 / Descriptor set bytes
   */
  ConstRawData GetWinUsbMsOs20DescriptorSet() const;

  /**
   * @brief BOS 能力数量 / BOS capability count
   * @return 能力数量 / Capability count
   */
  size_t GetBosCapabilityCount() override { return 1; }

  /**
   * @brief 获取 BOS 能力对象 / Get BOS capability
   * @param index 索引 / Index
   * @return BosCapability* 能力对象指针 / Capability pointer
   */
  BosCapability* GetBosCapability(size_t index) override
  {
    if (index == 0)
    {
      return &winusb_msos20_cap_;
    }
    return nullptr;
  }

 private:
  /**
   * @brief OUT 完成回调静态入口 / OUT complete callback static entry
   */
  static void OnDataOutCompleteStatic(bool in_isr, DapLinkV2Class* self,
                                      LibXR::ConstRawData& data);

  /**
   * @brief IN 完成回调静态入口 / IN complete callback static entry
   */
  static void OnDataInCompleteStatic(bool in_isr, DapLinkV2Class* self,
                                     LibXR::ConstRawData& data);

  /**
   * @brief OUT 完成回调（实例方法）/ OUT complete callback (instance)
   */
  void OnDataOutComplete(bool in_isr, LibXR::ConstRawData& data);

  /**
   * @brief IN 完成回调（实例方法）/ IN complete callback (instance)
   */
  void OnDataInComplete(bool in_isr, LibXR::ConstRawData& data);

 private:
  /**
   * @brief 若 OUT 空闲则 arm 一次接收 / Arm OUT transfer if idle
   */
  void ArmOutTransferIfIdle();

 private:
  /**
   * @brief 处理一条命令 / Process one command
   *
   * @param in_isr   ISR 上下文标志 / ISR context flag
   * @param req      请求缓冲 / Request buffer
   * @param req_len  请求长度 / Request length
   * @param resp     响应缓冲 / Response buffer
   * @param resp_cap 响应容量 / Response capacity
   * @param out_len  输出：响应长度 / Output: response length
   * @return 错误码 / Error code
   */
  ErrorCode ProcessOneCommand(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  /**
   * @brief 构建不支持响应 / Build NOT_SUPPORT response
   *
   * @param resp     响应缓冲 / Response buffer
   * @param resp_cap 响应容量 / Response capacity
   * @param out_len  输出：响应长度 / Output: response length
   */
  void BuildNotSupportResponse(uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

 private:
  ErrorCode HandleInfo(bool in_isr, const uint8_t* req, uint16_t req_len, uint8_t* resp,
                       uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleHostStatus(bool in_isr, const uint8_t* req, uint16_t req_len,
                             uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleConnect(bool in_isr, const uint8_t* req, uint16_t req_len,
                          uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleDisconnect(bool in_isr, const uint8_t* req, uint16_t req_len,
                             uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleTransferConfigure(bool in_isr, const uint8_t* req, uint16_t req_len,
                                    uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleTransfer(bool in_isr, const uint8_t* req, uint16_t req_len,
                           uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleTransferBlock(bool in_isr, const uint8_t* req, uint16_t req_len,
                                uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleTransferAbort(bool in_isr, const uint8_t* req, uint16_t req_len,
                                uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleWriteABORT(bool in_isr, const uint8_t* req, uint16_t req_len,
                             uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleDelay(bool in_isr, const uint8_t* req, uint16_t req_len, uint8_t* resp,
                        uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleResetTarget(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleSWJPins(bool in_isr, const uint8_t* req, uint16_t req_len,
                          uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleSWJClock(bool in_isr, const uint8_t* req, uint16_t req_len,
                           uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleSWJSequence(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleSWDConfigure(bool in_isr, const uint8_t* req, uint16_t req_len,
                               uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleSWDSequence(bool in_isr, const uint8_t* req, uint16_t req_len,
                              uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleQueueCommands(bool in_isr, const uint8_t* req, uint16_t req_len,
                                uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

  ErrorCode HandleExecuteCommands(bool in_isr, const uint8_t* req, uint16_t req_len,
                                  uint8_t* resp, uint16_t resp_cap, uint16_t& out_len);

 private:
  ErrorCode BuildInfoStringResponse(uint8_t cmd, const char* str, uint8_t* resp,
                                    uint16_t resp_cap, uint16_t& out_len);

  ErrorCode BuildInfoU8Response(uint8_t cmd, uint8_t val, uint8_t* resp,
                                uint16_t resp_cap, uint16_t& out_len);

  ErrorCode BuildInfoU16Response(uint8_t cmd, uint16_t val, uint8_t* resp,
                                 uint16_t resp_cap, uint16_t& out_len);

  ErrorCode BuildInfoU32Response(uint8_t cmd, uint32_t val, uint8_t* resp,
                                 uint16_t resp_cap, uint16_t& out_len);

 private:
  /**
   * @brief 将 SWD ACK 映射为 DAP 响应码 / Map SWD ACK to DAP response code
   */
  uint8_t MapAckToDapResp(LibXR::Debug::SwdProtocol::Ack ack) const;

  /**
   * @brief 设置 TransferAbort 标志 / Set TransferAbort flag
   */
  void SetTransferAbortFlag(bool on);

 private:
  /**
   * @brief 驱动复位引脚 / Drive reset pin
   * @param release true=释放高（不复位），false=拉低（复位）/ true=release, false=assert
   * reset
   */
  void DriveReset(bool release);

  /**
   * @brief 条件延时（若允许）/ Conditional delay (if allowed)
   * @param in_isr ISR 上下文标志 / ISR context flag
   * @param us 延时（us）/ Delay (us)
   */
  void DelayUsIfAllowed(bool in_isr, uint32_t us);

 private:
  static constexpr uint8_t WINUSB_VENDOR_CODE =
      0x20;  ///< WinUSB vendor code / WinUSB vendor code

  // REG_MULTI_SZ: "<GUID>\0\0" (UTF-16LE). GUID_STR_UTF16_BYTES should already include
  // the first UTF-16 NUL.
  static constexpr uint16_t GUID_MULTI_SZ_UTF_16_BYTES =
      static_cast<uint16_t>(LibXR::USB::WinUsbMsOs20::GUID_STR_UTF16_BYTES +
                            2);  ///< extra UTF-16 NUL for REG_MULTI_SZ end

  /**
   * @brief 初始化 WinUSB 描述符常量部分 / Initialize constant parts of WinUSB descriptors
   */
  void InitWinUsbDescriptors();

  /**
   * @brief 更新与接口号相关字段 / Patch interface-dependent fields
   */
  void UpdateWinUsbInterfaceFields();

#pragma pack(push, 1)
  /**
   * @brief MS OS 2.0 描述符集合布局 / MS OS 2.0 descriptor set layout
   */
  struct WinUsbMsOs20DescSet
  {
    LibXR::USB::WinUsbMsOs20::MsOs20SetHeader set;  ///< Set header / Set header
    LibXR::USB::WinUsbMsOs20::MsOs20SubsetHeaderConfiguration
        cfg;  ///< Config subset / Config subset
    LibXR::USB::WinUsbMsOs20::MsOs20SubsetHeaderFunction
        func;  ///< Function subset / Function subset
    LibXR::USB::WinUsbMsOs20::MsOs20FeatureCompatibleId
        compat;  ///< CompatibleId feature / CompatibleId feature

    /**
     * @brief DeviceInterfaceGUIDs 注册属性 / DeviceInterfaceGUIDs registry property
     */
    struct RegProp
    {
      LibXR::USB::WinUsbMsOs20::MsOs20FeatureRegPropertyHeader
          header;  ///< RegProperty header / RegProperty header
      uint8_t name[LibXR::USB::WinUsbMsOs20::
                       PROP_NAME_DEVICE_INTERFACE_GUIDS_BYTES];  ///< Property name /
                                                                 ///< Property name
      uint16_t wPropertyDataLength;  ///< UTF-16 字节长度 / UTF-16 byte length
      uint8_t
          data[GUID_MULTI_SZ_UTF_16_BYTES];  ///< REG_MULTI_SZ 数据 / REG_MULTI_SZ data
    } prop;
  } winusb_msos20_{};
#pragma pack(pop)

 private:
  LibXR::Debug::Swd& swd_;  ///< SWD 链路 / SWD link

  LibXR::GPIO* nreset_gpio_ = nullptr;  ///< 可选 nRESET GPIO / Optional nRESET GPIO

  uint8_t swj_shadow_ = static_cast<uint8_t>(
      DapLinkV2Def::DAP_SWJ_SWDIO_TMS |
      DapLinkV2Def::DAP_SWJ_NRESET);  ///< Shadow SWJ pin levels / Shadow SWJ pin levels

  bool last_nreset_level_high_ =
      true;  ///< 最近一次 nRESET 电平（高=释放）/ Last nRESET level (high=release)

  LibXR::USB::DapLinkV2Def::State dap_state_{};  ///< DAP 状态 / DAP state
  InfoStrings info_{
      "XRobot", "DAPLinkV2", "00000001", "2.0.0", "XRUSB",
      "XRDAP",  "XRobot",    "DAP_DEMO", "0.1.0"};  ///< Info 字符串 / Info strings

  uint32_t swj_clock_hz_ = 1000000u;  ///< SWJ 时钟（Hz）/ SWJ clock (Hz)

  Endpoint::EPNumber data_in_ep_num_;   ///< Bulk IN EP number / Bulk IN EP number
  Endpoint::EPNumber data_out_ep_num_;  ///< Bulk OUT EP number / Bulk OUT EP number

  Endpoint* ep_data_in_ = nullptr;   ///< Bulk IN endpoint / Bulk IN endpoint
  Endpoint* ep_data_out_ = nullptr;  ///< Bulk OUT endpoint / Bulk OUT endpoint

  bool inited_ = false;        ///< 初始化标志 / Initialized flag
  uint8_t interface_num_ = 0;  ///< 接口号 / Interface number

#pragma pack(push, 1)
  /**
   * @brief 配置描述符块（Interface + 2x Endpoint）/ Descriptor block (Interface + 2x
   * Endpoint)
   */
  struct DapLinkV2DescBlock
  {
    InterfaceDescriptor intf;   ///< Interface descriptor / Interface descriptor
    EndpointDescriptor ep_out;  ///< OUT endpoint descriptor / OUT endpoint descriptor
    EndpointDescriptor ep_in;   ///< IN endpoint descriptor / IN endpoint descriptor
  } desc_block_{};
#pragma pack(pop)

 private:
  static constexpr uint16_t MAX_REQ =
      LibXR::USB::DapLinkV2Def::MAX_REQUEST_SIZE;  ///< 最大请求 / Max request size
  static constexpr uint16_t MAX_RESP =
      LibXR::USB::DapLinkV2Def::MAX_RESPONSE_SIZE;  ///< 最大响应 / Max response size

  LibXR::USB::WinUsbMsOs20::MsOs20BosCapability winusb_msos20_cap_{
      LibXR::ConstRawData{nullptr, 0},
      WINUSB_VENDOR_CODE};  ///< WinUSB BOS capability / WinUSB BOS capability

  uint32_t match_mask_ = 0xFFFFFFFFu;  ///< Match mask / Match mask

  uint8_t rx_buf_[MAX_REQ]{};   ///< RX 缓冲 / RX buffer
  uint8_t tx_buf_[MAX_RESP]{};  ///< TX 缓冲 / TX buffer
  bool tx_busy_ = false;  ///< OUT->IN->OUT 串行化（避免覆盖 tx_buf_）/ Serialize to avoid
                          ///< tx_buf_ overwrite

  LibXR::Callback<LibXR::ConstRawData&> on_data_out_cb_ =
      LibXR::Callback<LibXR::ConstRawData&>::Create(OnDataOutCompleteStatic, this);

  LibXR::Callback<LibXR::ConstRawData&> on_data_in_cb_ =
      LibXR::Callback<LibXR::ConstRawData&>::Create(OnDataInCompleteStatic, this);
};

}  // namespace LibXR::USB
