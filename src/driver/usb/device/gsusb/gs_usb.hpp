#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "can.hpp"  // 里面有 CAN & FDCAN 抽象
#include "dev_core.hpp"
#include "gpio.hpp"  // 提供 LibXR::GPIO
#include "gs_usb_protocol.hpp"
#include "usb/core/desc_cfg.hpp"

namespace LibXR::USB
{

/**
 * @brief GsUsb 设备类，实现 Linux gs_usb 协议（经典 CAN + CAN FD）
 *        数据通道使用 1 个接口 + 2 个 BULK 端点
 */
class GsUsbClass : public DeviceClass
{
 public:
  // ============== 构造 ==============

  /// 经典 CAN：多通道
  GsUsbClass(std::initializer_list<LibXR::CAN *> cans,
             Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
             Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
             LibXR::GPIO *identify_gpio = nullptr,
             LibXR::GPIO *termination_gpio = nullptr);

  /// FDCAN：多通道
  GsUsbClass(std::initializer_list<LibXR::FDCAN *> fd_cans,
             Endpoint::EPNumber data_in_ep_num = Endpoint::EPNumber::EP_AUTO,
             Endpoint::EPNumber data_out_ep_num = Endpoint::EPNumber::EP_AUTO,
             LibXR::GPIO *identify_gpio = nullptr,
             LibXR::GPIO *termination_gpio = nullptr);

  /// 当前 Host 是否已完成 endian 握手（HOST_FORMAT）
  bool IsHostFormatOK() const { return host_format_ok_; }

 protected:
  // ================= ConfigDescriptorItem 覆盖 =================

  void Init(EndpointPool &endpoint_pool, uint8_t start_itf_num) override;

  void Deinit(EndpointPool &endpoint_pool) override;

  size_t GetInterfaceNum() override { return 1; }

  bool HasIAD() override { return false; }

  bool OwnsEndpoint(uint8_t ep_addr) const override;

  size_t GetMaxConfigSize() override { return sizeof(desc_block_); }

  // ================= 控制请求处理 =================

  /// 不使用 Class 请求，全部走 Vendor 请求
  ErrorCode OnClassRequest(bool, uint8_t, uint16_t, uint16_t, uint16_t,
                           DeviceClass::RequestResult &) override
  {
    return ErrorCode::NOT_SUPPORT;
  }

  /// Vendor Request 的 SETUP 阶段
  ErrorCode OnVendorRequest(bool in_isr, uint8_t bRequest, uint16_t wValue,
                            uint16_t wLength, uint16_t wIndex,
                            DeviceClass::RequestResult &result) override;

  /// 控制传输 DATA 阶段（Class + Vendor 共用），这里用 bRequest 区分
  ErrorCode OnClassData(bool in_isr, uint8_t bRequest,
                        LibXR::ConstRawData &data) override;

  // ================= Bulk 端点回调 =================

  static void OnDataOutCompleteStatic(bool in_isr, GsUsbClass *self, ConstRawData &data)
  {
    if (!self->inited_)
    {
      return;
    }
    self->OnDataOutComplete(in_isr, data);
  }

  static void OnDataInCompleteStatic(bool in_isr, GsUsbClass *self, ConstRawData &data)
  {
    if (!self->inited_)
    {
      return;
    }
    self->OnDataInComplete(in_isr, data);
  }

  void OnDataOutComplete(bool in_isr, ConstRawData &data);
  void OnDataInComplete(bool in_isr, ConstRawData &data);

 private:
  // 最多支持的 CAN 通道数（USB 协议上）
  static constexpr uint8_t MAX_CAN_CH = 4;

  // TX pool 槽数（HostFrame 个数）
  static constexpr uint8_t TX_POOL_SIZE = 32;

  // ================= 成员 =================

  // 通道数组（基类 CAN 指针）
  LibXR::CAN *cans_[MAX_CAN_CH] = {nullptr};
  // 如果是 FDCAN 模式，fdcans_ 指向同一批对象
  LibXR::FDCAN *fdcans_[MAX_CAN_CH] = {nullptr};
  bool fd_supported_ = false;

  uint8_t can_count_ = 0;

  Endpoint::EPNumber data_in_ep_num_;
  Endpoint::EPNumber data_out_ep_num_;

  Endpoint *ep_data_in_ = nullptr;
  Endpoint *ep_data_out_ = nullptr;

  bool inited_ = false;
  uint8_t interface_num_ = 0;

  LibXR::GPIO *identify_gpio_ = nullptr;     // Identify LED（可空）
  LibXR::GPIO *termination_gpio_ = nullptr;  // 终端电阻控制 GPIO（可空，全局）

  // EP 回调
  LibXR::Callback<LibXR::ConstRawData &> on_data_out_cb_ =
      LibXR::Callback<LibXR::ConstRawData &>::Create(OnDataOutCompleteStatic, this);

  LibXR::Callback<LibXR::ConstRawData &> on_data_in_cb_ =
      LibXR::Callback<LibXR::ConstRawData &>::Create(OnDataInCompleteStatic, this);

  // GsUsb 协议相关状态
  GsUsb::DeviceConfig dev_cfg_{};
  GsUsb::DeviceBTConst bt_const_{};
  GsUsb::DeviceBTConstExtended bt_const_ext_{};  // FD 能力

  union
  {
    GsUsb::HostConfig host_cfg;
    GsUsb::DeviceBitTiming bt;
    GsUsb::DeviceMode mode;
    uint32_t berr_on;
    GsUsb::Identify identify;
    uint32_t timestamp_us;
    GsUsb::DeviceTerminationState term;
    GsUsb::DeviceState dev_state;
    uint32_t user_id;
  } ctrl_buf_{};

  // 每个通道一份 CAN 配置
  LibXR::CAN::Configuration config_[MAX_CAN_CH]{};

  // FD 模式下，每个通道一份 FDCAN 配置
  LibXR::FDCAN::Configuration fd_config_[MAX_CAN_CH]{};

  bool host_format_ok_ = false;
  bool can_enabled_[MAX_CAN_CH] = {false};
  bool berr_enabled_[MAX_CAN_CH] = {false};
  bool fd_enabled_[MAX_CAN_CH] = {false};
  bool timestamps_enabled_ = false;
  bool pad_pkts_to_max_pkt_size_ = false;

  GsUsb::TerminationState term_state_[MAX_CAN_CH] = {
      GsUsb::TerminationState::OFF,
  };

  // 当前正在处理控制请求的目标通道（由 wValue 决定）
  uint8_t ctrl_target_channel_ = 0;

#pragma pack(push, 1)
  /// GsUsb 配置描述符块：1 个接口 + 2 个 BULK 端点
  struct GsUsbDescBlock
  {
    InterfaceDescriptor intf;
    EndpointDescriptor ep_out;
    EndpointDescriptor ep_in;
  } desc_block_;
#pragma pack(pop)

  // ================= CAN RX 回调 & BULK IN 发送队列 =================

  struct CanRxCtx
  {
    GsUsbClass *self;
    uint8_t ch;
  };

  struct FdCanRxCtx
  {
    GsUsbClass *self;
    uint8_t ch;
  };

  bool can_rx_registered_ = false;
  CanRxCtx can_rx_ctx_[MAX_CAN_CH]{};
  LibXR::CAN::Callback can_rx_cb_[MAX_CAN_CH]{};

  bool fd_can_rx_registered_ = false;
  FdCanRxCtx fd_can_rx_ctx_[MAX_CAN_CH]{};
  LibXR::FDCAN::CallbackFD fd_can_rx_cb_[MAX_CAN_CH]{};

  // 静态 trampoline
  static void OnCanRxStatic(bool in_isr, CanRxCtx *ctx,
                            const LibXR::CAN::ClassicPack &pack)
  {
    if (!ctx || !ctx->self || !ctx->self->inited_)
    {
      return;
    }
    ctx->self->OnCanRx(in_isr, ctx->ch, pack);
  }

  static void OnFdCanRxStatic(bool in_isr, FdCanRxCtx *ctx,
                              const LibXR::FDCAN::FDPack &pack)
  {
    if (!ctx || !ctx->self || !ctx->self->inited_)
    {
      return;
    }
    ctx->self->OnFdCanRx(in_isr, ctx->ch, pack);
  }

  // 真正的 CAN RX 处理函数
  void OnCanRx(bool in_isr, uint8_t ch, const LibXR::CAN::ClassicPack &pack);
  void OnFdCanRx(bool in_isr, uint8_t ch, const LibXR::FDCAN::FDPack &pack);

  // TX 使用 LockFreePool
  LibXR::LockFreePool<GsUsb::HostFrame> tx_pool_{TX_POOL_SIZE};
  std::atomic<bool> tx_in_progress_{false};
  uint32_t tx_put_index_ = 0;
  uint32_t tx_get_index_ = 0;

  /// 入队一个要发给 Host 的 HostFrame（CAN RX / 错误帧 / TX echo 共用）
  bool EnqueueHostFrame(const GsUsb::HostFrame &hf, bool in_isr);

  /// 尝试启动下一次 BULK IN 传输
  void TryKickTx(bool in_isr);

  // ================= 业务处理函数 =================

  ErrorCode HandleHostFormat(const GsUsb::HostConfig &cfg);
  ErrorCode HandleBitTiming(uint8_t ch, const GsUsb::DeviceBitTiming &bt);
  ErrorCode HandleDataBitTiming(uint8_t ch, const GsUsb::DeviceBitTiming &bt);
  ErrorCode HandleMode(uint8_t ch, const GsUsb::DeviceMode &mode);
  ErrorCode HandleBerr(uint8_t ch, uint32_t berr_on);
  ErrorCode HandleIdentify(uint8_t ch, const GsUsb::Identify &id);
  ErrorCode HandleSetTermination(uint8_t ch, const GsUsb::DeviceTerminationState &st);
  ErrorCode HandleGetState(uint8_t ch);

  // HostFrame <-> ClassicPack / FDPack 映射
  void HostFrameToClassicPack(const GsUsb::HostFrame &hf, LibXR::CAN::ClassicPack &pack);
  void ClassicPackToHostFrame(const LibXR::CAN::ClassicPack &pack, GsUsb::HostFrame &hf);

  void HostFrameToFdPack(const GsUsb::HostFrame &hf, LibXR::FDCAN::FDPack &pack);
  void FdPackToHostFrame(const LibXR::FDCAN::FDPack &pack, GsUsb::HostFrame &hf);

  // CAN 错误帧映射：ClassicPack(Type::ERROR) -> Linux 风格错误帧 HostFrame
  bool ErrorPackToHostErrorFrame(uint8_t ch, const LibXR::CAN::ClassicPack &err_pack,
                                 GsUsb::HostFrame &hf);

  // DLC <-> 长度 映射（CAN FD）
  static uint8_t DlcToLen(uint8_t dlc);
  static uint8_t LenToDlc(uint8_t len);
};

}  // namespace LibXR::USB
