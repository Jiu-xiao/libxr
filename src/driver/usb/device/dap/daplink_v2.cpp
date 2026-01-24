#include "daplink_v2.hpp"

#include <cstdint>
#include <cstring>

#include "daplink_v2_def.hpp"
#include "dev_core.hpp"
#include "libxr_def.hpp"
#include "timebase.hpp"

namespace LibXR::USB
{

namespace
{
// 枚举/整型转 uint8_t。Cast enum/integer to uint8_t.
template <typename E>
static constexpr uint8_t to_u8(E e)
{
  return static_cast<uint8_t>(e);
}

// CMSIS-DAP status bytes
static constexpr uint8_t DAP_OK = 0x00u;     ///< DAP_OK / DAP_OK
static constexpr uint8_t DAP_ERROR = 0xFFu;  ///< DAP_ERROR / DAP_ERROR

// 未知命令响应：单字节 0xFF。Unknown command response: single byte 0xFF.
static inline ErrorCode build_unknow_cmd_response(uint8_t* resp, uint16_t cap,
                                                  uint16_t& out_len)
{
  if (!resp || cap < 1u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = 0xFFu;
  out_len = 1u;
  return ErrorCode::OK;
}

// 命令状态响应：<cmd, status>。Command status response: <cmd, status>.
static inline ErrorCode build_cmd_status_response(uint8_t cmd, uint8_t status,
                                                  uint8_t* resp, uint16_t cap,
                                                  uint16_t& out_len)
{
  if (!resp || cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = cmd;
  resp[1] = status;
  out_len = 2u;
  return ErrorCode::OK;
}

}  // namespace

void DapLinkV2Class::InitWinUsbDescriptors()
{
  winusb_msos20_.set.wLength = static_cast<uint16_t>(sizeof(winusb_msos20_.set));
  winusb_msos20_.set.wDescriptorType =
      LibXR::USB::WinUsbMsOs20::MS_OS_20_SET_HEADER_DESCRIPTOR;
  winusb_msos20_.set.dwWindowsVersion = 0x06030000;  // Win 8.1+
  winusb_msos20_.set.wTotalLength = static_cast<uint16_t>(sizeof(winusb_msos20_));

  winusb_msos20_.cfg.wLength = static_cast<uint16_t>(sizeof(winusb_msos20_.cfg));
  winusb_msos20_.cfg.wDescriptorType =
      LibXR::USB::WinUsbMsOs20::MS_OS_20_SUBSET_HEADER_CONFIGURATION;

  winusb_msos20_.cfg.bConfigurationValue = 0;
  winusb_msos20_.cfg.bReserved = 0;
  winusb_msos20_.cfg.wTotalLength =
      static_cast<uint16_t>(sizeof(winusb_msos20_) - offsetof(WinUsbMsOs20DescSet, cfg));

  winusb_msos20_.func.wLength = static_cast<uint16_t>(sizeof(winusb_msos20_.func));
  winusb_msos20_.func.wDescriptorType =
      LibXR::USB::WinUsbMsOs20::MS_OS_20_SUBSET_HEADER_FUNCTION;
  winusb_msos20_.func.bReserved = 0;
  winusb_msos20_.func.wTotalLength =
      static_cast<uint16_t>(sizeof(winusb_msos20_) - offsetof(WinUsbMsOs20DescSet, func));

  winusb_msos20_.compat.wLength = static_cast<uint16_t>(sizeof(winusb_msos20_.compat));
  winusb_msos20_.compat.wDescriptorType =
      LibXR::USB::WinUsbMsOs20::MS_OS_20_FEATURE_COMPATIBLE_ID;

  winusb_msos20_.prop.header.wDescriptorType =
      LibXR::USB::WinUsbMsOs20::MS_OS_20_FEATURE_REG_PROPERTY;
  winusb_msos20_.prop.header.wPropertyDataType = LibXR::USB::WinUsbMsOs20::REG_MULTI_SZ;
  winusb_msos20_.prop.header.wPropertyNameLength =
      LibXR::USB::WinUsbMsOs20::PROP_NAME_DEVICE_INTERFACE_GUIDS_BYTES;

  Memory::FastCopy(winusb_msos20_.prop.name,
                   LibXR::USB::WinUsbMsOs20::PROP_NAME_DEVICE_INTERFACE_GUIDS_UTF16,
                   LibXR::USB::WinUsbMsOs20::PROP_NAME_DEVICE_INTERFACE_GUIDS_BYTES);

  // DeviceInterfaceGUIDs: REG_MULTI_SZ UTF-16LE, include double-NUL terminator
  // 注意：此处为“单 GUID + 双 NUL 结束”的 REG_MULTI_SZ / Single GUID + double-NUL end.
  const char GUID_STR[] = "{CDB3B5AD-293B-4663-AA36-1AAE46463776}";
  const size_t GUID_LEN = sizeof(GUID_STR) - 1;

  for (size_t i = 0; i < GUID_LEN; ++i)
  {
    winusb_msos20_.prop.data[i * 2] = static_cast<uint8_t>(GUID_STR[i]);
    winusb_msos20_.prop.data[i * 2 + 1] = 0x00;
  }

  // Append UTF-16 NUL + extra UTF-16 NUL (REG_MULTI_SZ end)
  winusb_msos20_.prop.data[GUID_LEN * 2 + 0] = 0x00;
  winusb_msos20_.prop.data[GUID_LEN * 2 + 1] = 0x00;
  winusb_msos20_.prop.data[GUID_LEN * 2 + 2] = 0x00;
  winusb_msos20_.prop.data[GUID_LEN * 2 + 3] = 0x00;

  winusb_msos20_.prop.wPropertyDataLength = static_cast<uint16_t>((GUID_LEN * 2) + 4);
  winusb_msos20_.prop.header.wLength = static_cast<uint16_t>(sizeof(winusb_msos20_.prop));

  // Sync to BOS capability object
  winusb_msos20_cap_.SetVendorCode(WINUSB_VENDOR_CODE);
  winusb_msos20_cap_.SetDescriptorSet(GetWinUsbMsOs20DescriptorSet());
}

void DapLinkV2Class::UpdateWinUsbInterfaceFields()
{
  // Function subset interface number
  winusb_msos20_.func.bFirstInterface = interface_num_;

  // 内容变化但总长度不变；同步一次保持一致性
  // Content changes but total size stays the same; resync for consistency.
  winusb_msos20_cap_.SetDescriptorSet(GetWinUsbMsOs20DescriptorSet());
}

ConstRawData DapLinkV2Class::GetWinUsbMsOs20DescriptorSet() const
{
  return ConstRawData{reinterpret_cast<const uint8_t*>(&winusb_msos20_),
                      sizeof(winusb_msos20_)};
}

// ============================================================================
// Ctor / public APIs
// ============================================================================

DapLinkV2Class::DapLinkV2Class(LibXR::Debug::Swd& swd_link, LibXR::GPIO* nreset_gpio,
                               Endpoint::EPNumber data_in_ep_num,
                               Endpoint::EPNumber data_out_ep_num, uint8_t packet_count)
    : DeviceClass({&winusb_msos20_cap_}),
      swd_(swd_link),
      nreset_gpio_(nreset_gpio),
      data_in_ep_num_(data_in_ep_num),
      data_out_ep_num_(data_out_ep_num),
      packet_count_(packet_count),
      slots_(new PacketSlot[packet_count_]),
      free_slots_storage_(new uint8_t[packet_count_]),
      tx_slots_storage_(new uint8_t[packet_count_]),
      free_slots_(packet_count_, free_slots_storage_),
      tx_slots_(packet_count_, tx_slots_storage_)
{
  (void)swd_.SetClockHz(swj_clock_hz_);

  // Init WinUSB descriptor templates (constant parts)
  InitWinUsbDescriptors();
}

void DapLinkV2Class::SetInfoStrings(const InfoStrings& info) { info_ = info; }

const LibXR::USB::DapLinkV2Def::State& DapLinkV2Class::GetState() const
{
  return dap_state_;
}

bool DapLinkV2Class::IsInited() const { return inited_; }

// ============================================================================
// DeviceClass overrides
// ============================================================================

size_t DapLinkV2Class::GetInterfaceCount() { return 1; }

bool DapLinkV2Class::HasIAD() { return false; }

bool DapLinkV2Class::OwnsEndpoint(uint8_t ep_addr) const
{
  if (!inited_)
  {
    return false;
  }
  return (ep_data_in_ && ep_addr == ep_data_in_->GetAddress()) ||
         (ep_data_out_ && ep_addr == ep_data_out_->GetAddress());
}

size_t DapLinkV2Class::GetMaxConfigSize() { return sizeof(desc_block_); }

void DapLinkV2Class::BindEndpoints(EndpointPool& endpoint_pool, uint8_t start_itf_num)
{
  inited_ = false;

  out_slot_in_flight_ = -1;
  in_slot_in_flight_ = -1;

  free_slots_.Reset();
  tx_slots_.Reset();

  for (uint8_t i = 0; i < packet_count_; ++i)
  {
    slots_[i].rx_len = 0;
    slots_[i].tx_len = 0;
    slots_[i].state = PacketSlot::State::FREE;

    // 初始化 free-slot 队列：0..packet_count_-1
    (void)free_slots_.Push(i);
  }

  interface_num_ = start_itf_num;

  // Patch WinUSB function subset to match this interface number
  UpdateWinUsbInterfaceFields();

  // Allocate endpoints
  auto ans = endpoint_pool.Get(ep_data_out_, Endpoint::Direction::OUT, data_out_ep_num_);
  ASSERT(ans == ErrorCode::OK);

  ans = endpoint_pool.Get(ep_data_in_, Endpoint::Direction::IN, data_in_ep_num_);
  ASSERT(ans == ErrorCode::OK);

  // Configure endpoints
  // - Use upper bound; core will choose a valid max packet size <= this limit.
  // - Keep double_buffer=false; multi PACKET_COUNT is implemented in software slots.
  ep_data_out_->Configure(
      {Endpoint::Direction::OUT, Endpoint::Type::BULK, UINT16_MAX, false});
  ep_data_in_->Configure(
      {Endpoint::Direction::IN, Endpoint::Type::BULK, UINT16_MAX, false});

  // Hook callbacks
  ep_data_out_->SetOnTransferCompleteCallback(on_data_out_cb_);
  ep_data_in_->SetOnTransferCompleteCallback(on_data_in_cb_);

  // Interface descriptor (vendor specific, 2 endpoints)
  desc_block_.intf = {9,
                      static_cast<uint8_t>(DescriptorType::INTERFACE),
                      interface_num_,
                      0,
                      2,
                      0xFF,  // vendor specific
                      0x00,
                      0x00,
                      0};

  desc_block_.ep_out = {7,
                        static_cast<uint8_t>(DescriptorType::ENDPOINT),
                        static_cast<uint8_t>(ep_data_out_->GetAddress()),
                        static_cast<uint8_t>(Endpoint::Type::BULK),
                        ep_data_out_->MaxPacketSize(),
                        0};

  desc_block_.ep_in = {7,
                       static_cast<uint8_t>(DescriptorType::ENDPOINT),
                       static_cast<uint8_t>(ep_data_in_->GetAddress()),
                       static_cast<uint8_t>(Endpoint::Type::BULK),
                       ep_data_in_->MaxPacketSize(),
                       0};

  SetData(RawData{reinterpret_cast<uint8_t*>(&desc_block_), sizeof(desc_block_)});

  // Runtime defaults
  dap_state_ = {};
  dap_state_.debug_port = LibXR::USB::DapLinkV2Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;

  swj_clock_hz_ = 1'000'000u;
  (void)swd_.SetClockHz(swj_clock_hz_);

  // SWJ shadow defaults: SWDIO=1, nRESET=1, SWCLK=0
  last_nreset_level_high_ = true;
  swj_shadow_ = static_cast<uint8_t>(DapLinkV2Def::DAP_SWJ_SWDIO_TMS |
                                     DapLinkV2Def::DAP_SWJ_NRESET);

  inited_ = true;
  ArmOutTransferIfIdle();
}

void DapLinkV2Class::UnbindEndpoints(EndpointPool& endpoint_pool)
{
  inited_ = false;

  out_slot_in_flight_ = -1;
  in_slot_in_flight_ = -1;

  free_slots_.Reset();
  tx_slots_.Reset();

  dap_state_.debug_port = LibXR::USB::DapLinkV2Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;

  if (ep_data_in_)
  {
    ep_data_in_->Close();
    ep_data_in_->SetActiveLength(0);
    endpoint_pool.Release(ep_data_in_);
    ep_data_in_ = nullptr;
  }

  if (ep_data_out_)
  {
    ep_data_out_->Close();
    ep_data_out_->SetActiveLength(0);
    endpoint_pool.Release(ep_data_out_);
    ep_data_out_ = nullptr;
  }

  swd_.Close();

  // Reset shadow defaults
  last_nreset_level_high_ = true;
  swj_shadow_ = static_cast<uint8_t>(DapLinkV2Def::DAP_SWJ_SWDIO_TMS |
                                     DapLinkV2Def::DAP_SWJ_NRESET);
}

// ============================================================================
// USB callbacks
// ============================================================================

void DapLinkV2Class::OnDataOutCompleteStatic(bool in_isr, DapLinkV2Class* self,
                                             LibXR::ConstRawData& data)
{
  if (self && self->inited_)
  {
    self->OnDataOutComplete(in_isr, data);
  }
}

void DapLinkV2Class::OnDataInCompleteStatic(bool in_isr, DapLinkV2Class* self,
                                            LibXR::ConstRawData& data)
{
  if (self && self->inited_)
  {
    self->OnDataInComplete(in_isr, data);
  }
}

void DapLinkV2Class::OnDataOutComplete(bool in_isr, LibXR::ConstRawData& data)
{
  if (!inited_ || !ep_data_in_ || !ep_data_out_)
  {
    return;
  }

  const int8_t SLOT_I8 = out_slot_in_flight_;
  out_slot_in_flight_ = -1;

  if (SLOT_I8 < 0 || SLOT_I8 >= static_cast<int8_t>(packet_count_))
  {
    ArmOutTransferIfIdle();
    return;
  }

  const uint8_t SLOT = static_cast<uint8_t>(SLOT_I8);

  // 优先持续接收：先尝试 arm 下一次 OUT（用 free_slots_ 中的其它 slot）
  ArmOutTransferIfIdle();

  const auto* req = static_cast<const uint8_t*>(data.addr_);
  const uint16_t REQ_LEN = static_cast<uint16_t>(data.size_);

  // 空包：释放 slot 回 free
  if (!req || REQ_LEN == 0u)
  {
    slots_[SLOT].rx_len = 0u;
    slots_[SLOT].tx_len = 0u;
    slots_[SLOT].state = PacketSlot::State::FREE;
    (void)free_slots_.Push(SLOT);

    StartInIfIdle();
    return;
  }

  // data.addr_ 应为 slots_[slot].rx（由 TransferMultiBulk(rx) 传入）
  slots_[SLOT].rx_len = REQ_LEN;

  uint16_t out_len = 0u;
  (void)ProcessOneCommand(in_isr, slots_[SLOT].rx, slots_[SLOT].rx_len, slots_[SLOT].tx,
                          MAX_RESP, out_len);

  if (out_len == 0u)
  {
    slots_[SLOT].tx[0] = 0xFFu;
    out_len = 1u;
  }

  slots_[SLOT].tx_len = out_len;
  slots_[SLOT].state = PacketSlot::State::RESP_READY;

  // 将 slot 放入响应队列（FIFO）
  (void)tx_slots_.Push(SLOT);

  StartInIfIdle();
}

void DapLinkV2Class::OnDataInComplete(bool /*in_isr*/, LibXR::ConstRawData& /*data*/)
{
  const int8_t SLOT_I8 = in_slot_in_flight_;
  in_slot_in_flight_ = -1;

  if (SLOT_I8 >= 0 && SLOT_I8 < static_cast<int8_t>(packet_count_))
  {
    const uint8_t SLOT = static_cast<uint8_t>(SLOT_I8);
    slots_[SLOT].rx_len = 0u;
    slots_[SLOT].tx_len = 0u;
    slots_[SLOT].state = PacketSlot::State::FREE;

    // 发送完成，slot 回收进 free_slots_
    (void)free_slots_.Push(SLOT);
  }

  ArmOutTransferIfIdle();
  StartInIfIdle();
}

// ============================================================================
// Multi PACKET_COUNT helpers (Queue-based)
// ============================================================================

void DapLinkV2Class::StartInIfIdle()
{
  if (!inited_ || !ep_data_in_)
  {
    return;
  }

  uint8_t slot = 0u;
  if (tx_slots_.Peek(slot) != ErrorCode::OK)
  {
    return;
  }

  if (slot >= packet_count_)
  {
    // 队列里出现非法 slot：丢弃
    (void)tx_slots_.Pop(slot);
    return;
  }

  if (slots_[slot].state != PacketSlot::State::RESP_READY)
  {
    // 队列/状态不同步：丢弃
    (void)tx_slots_.Pop(slot);
    return;
  }

  uint16_t len = slots_[slot].tx_len;
  if (len == 0u)
  {
    slots_[slot].tx[0] = 0xFFu;
    len = 1u;
    slots_[slot].tx_len = 1u;
  }

  LibXR::RawData tx(slots_[slot].tx, len);
  if (ep_data_in_->TransferMultiBulk(tx) == ErrorCode::OK)
  {
    (void)tx_slots_.Pop(slot);
    in_slot_in_flight_ = static_cast<int8_t>(slot);
    slots_[slot].state = PacketSlot::State::IN_IN_FLIGHT;
  }
}

// ============================================================================
// Arm OUT
// ============================================================================

void DapLinkV2Class::ArmOutTransferIfIdle()
{
  if (!inited_ || !ep_data_out_)
  {
    return;
  }

  if (ep_data_out_->GetState() != Endpoint::State::IDLE)
  {
    return;
  }

  // OUT 同时只能有一个 transfer in-flight
  if (out_slot_in_flight_ != -1)
  {
    return;
  }

  uint8_t slot = 0u;
  if (free_slots_.Pop(slot) != ErrorCode::OK)
  {
    // 无空槽：背压（不 re-arm OUT，主机会 NAK 后重试）
    return;
  }

  if (slot >= packet_count_)
  {
    return;
  }

  slots_[slot].rx_len = 0u;
  slots_[slot].tx_len = 0u;
  slots_[slot].state = PacketSlot::State::OUT_IN_FLIGHT;

  out_slot_in_flight_ = static_cast<int8_t>(slot);

  LibXR::RawData rx(slots_[slot].rx, MAX_REQ);
  (void)ep_data_out_->TransferMultiBulk(rx);
}

// ============================================================================
// Command dispatch
// ============================================================================

ErrorCode DapLinkV2Class::ProcessOneCommand(bool in_isr, const uint8_t* req,
                                            uint16_t req_len, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  out_len = 0;

  if (!req || !resp || req_len < 1u || resp_cap < 1u)
  {
    if (resp && resp_cap >= 1u)
    {
      BuildNotSupportResponse(resp, resp_cap, out_len);
    }
    return ErrorCode::ARG_ERR;
  }

  const uint8_t CMD = req[0];

  switch (CMD)
  {
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::INFO):
      return HandleInfo(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::HOST_STATUS):
      return HandleHostStatus(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::CONNECT):
      return HandleConnect(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::DISCONNECT):
      return HandleDisconnect(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::TRANSFER_CONFIGURE):
      return HandleTransferConfigure(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::TRANSFER):
      return HandleTransfer(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::TRANSFER_BLOCK):
      return HandleTransferBlock(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::TRANSFER_ABORT):
      return HandleTransferAbort(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::WRITE_ABORT):
      return HandleWriteABORT(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::DELAY):
      return HandleDelay(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::RESET_TARGET):
      return HandleResetTarget(in_isr, req, req_len, resp, resp_cap, out_len);

    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_PINS):
      return HandleSWJPins(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_CLOCK):
      return HandleSWJClock(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_SEQUENCE):
      return HandleSWJSequence(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::SWD_CONFIGURE):
      return HandleSWDConfigure(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::SWD_SEQUENCE):
      return HandleSWDSequence(in_isr, req, req_len, resp, resp_cap, out_len);

    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::QUEUE_COMMANDS):
      return HandleQueueCommands(in_isr, req, req_len, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::CommandId::EXECUTE_COMMANDS):
      return HandleExecuteCommands(in_isr, req, req_len, resp, resp_cap, out_len);

    default:
      (void)build_unknow_cmd_response(resp, resp_cap, out_len);
      return ErrorCode::NOT_SUPPORT;
  }
}

void DapLinkV2Class::BuildNotSupportResponse(uint8_t* resp, uint16_t resp_cap,
                                             uint16_t& out_len)
{
  if (!resp || resp_cap < 1u)
  {
    out_len = 0u;
    return;
  }
  resp[0] = 0xFFu;
  out_len = 1u;
}

// ============================================================================
// DAP_Info
// ============================================================================

ErrorCode DapLinkV2Class::HandleInfo(bool /*in_isr*/, const uint8_t* req,
                                     uint16_t req_len, uint8_t* resp, uint16_t resp_cap,
                                     uint16_t& out_len)
{
  if (req_len < 2u)
  {
    resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::INFO);
    resp[1] = 0u;
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t INFO_ID = req[1];

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::INFO);

  switch (INFO_ID)
  {
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::VENDOR):
      return BuildInfoStringResponse(resp[0], info_.vendor, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::PRODUCT):
      return BuildInfoStringResponse(resp[0], info_.product, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::SERIAL_NUMBER):
      return BuildInfoStringResponse(resp[0], info_.serial, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::FIRMWARE_VERSION):
      return BuildInfoStringResponse(resp[0], info_.firmware_ver, resp, resp_cap,
                                     out_len);

    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::DEVICE_VENDOR):
      return BuildInfoStringResponse(resp[0], info_.device_vendor, resp, resp_cap,
                                     out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::DEVICE_NAME):
      return BuildInfoStringResponse(resp[0], info_.device_name, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::BOARD_VENDOR):
      return BuildInfoStringResponse(resp[0], info_.board_vendor, resp, resp_cap,
                                     out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::BOARD_NAME):
      return BuildInfoStringResponse(resp[0], info_.board_name, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::PRODUCT_FIRMWARE_VERSION):
      return BuildInfoStringResponse(resp[0], info_.product_fw_ver, resp, resp_cap,
                                     out_len);

    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::CAPABILITIES):
      return BuildInfoU8Response(resp[0], LibXR::USB::DapLinkV2Def::DAP_CAP_SWD, resp,
                                 resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::PACKET_COUNT):
      return BuildInfoU8Response(resp[0], packet_count_, resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::PACKET_SIZE):
      return BuildInfoU16Response(resp[0], ep_data_in_ ? ep_data_in_->MaxPacketSize() : 0,
                                  resp, resp_cap, out_len);
    case to_u8(LibXR::USB::DapLinkV2Def::InfoId::TIMESTAMP_CLOCK):
      return BuildInfoU32Response(resp[0], 1000000U, resp, resp_cap, out_len);

    default:
      resp[1] = 0u;
      out_len = 2u;
      return ErrorCode::OK;
  }
}

ErrorCode DapLinkV2Class::BuildInfoStringResponse(uint8_t cmd, const char* str,
                                                  uint8_t* resp, uint16_t resp_cap,
                                                  uint16_t& out_len)
{
  resp[0] = cmd;
  resp[1] = 0u;

  if (!str)
  {
    out_len = 2u;
    return ErrorCode::OK;
  }

  const size_t N_WITH_NUL = std::strlen(str) + 1;  // include '\0'
  const size_t MAX_PAYLOAD = (resp_cap >= 2u) ? (resp_cap - 2u) : 0u;
  if (MAX_PAYLOAD == 0u)
  {
    out_len = 2u;
    return ErrorCode::OK;
  }

  const size_t COPY_N = (N_WITH_NUL > MAX_PAYLOAD) ? MAX_PAYLOAD : N_WITH_NUL;
  Memory::FastCopy(&resp[2], str, COPY_N);

  // Ensure termination even when truncated
  resp[2 + COPY_N - 1] = 0x00;

  resp[1] = static_cast<uint8_t>(COPY_N);
  out_len = static_cast<uint16_t>(2u + COPY_N);
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::BuildInfoU8Response(uint8_t cmd, uint8_t val, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 3u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = cmd;
  resp[1] = 1u;
  resp[2] = val;
  out_len = 3u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::BuildInfoU16Response(uint8_t cmd, uint16_t val, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 4u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = cmd;
  resp[1] = 2u;

  // Little-endian device only: direct memcpy to payload (alignment-safe).
  Memory::FastCopy(&resp[2], &val, sizeof(val));

  out_len = 4u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::BuildInfoU32Response(uint8_t cmd, uint32_t val, uint8_t* resp,
                                               uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 6u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = cmd;
  resp[1] = 4u;

  // Little-endian device only: direct memcpy to payload (alignment-safe).
  Memory::FastCopy(&resp[2], &val, sizeof(val));

  out_len = 6u;
  return ErrorCode::OK;
}

// ============================================================================
// Simple control handlers
// ============================================================================

ErrorCode DapLinkV2Class::HandleHostStatus(bool /*in_isr*/, const uint8_t* /*req*/,
                                           uint16_t /*req_len*/, uint8_t* resp,
                                           uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }
  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::HOST_STATUS);
  resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleConnect(bool /*in_isr*/, const uint8_t* req,
                                        uint16_t req_len, uint8_t* resp,
                                        uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::CONNECT);

  uint8_t port = 0u;
  if (req_len >= 2u)
  {
    port = req[1];
  }

  // SWD-only
  if (port == 0u || port == to_u8(LibXR::USB::DapLinkV2Def::Port::SWD))
  {
    (void)swd_.EnterSwd();
    (void)swd_.SetClockHz(swj_clock_hz_);

    dap_state_.debug_port = LibXR::USB::DapLinkV2Def::DebugPort::SWD;
    dap_state_.transfer_abort = false;

    resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Port::SWD);
  }
  else
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Port::DISABLED);
  }

  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleDisconnect(bool /*in_isr*/, const uint8_t* /*req*/,
                                           uint16_t /*req_len*/, uint8_t* resp,
                                           uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  swd_.Close();
  dap_state_.debug_port = LibXR::USB::DapLinkV2Def::DebugPort::DISABLED;
  dap_state_.transfer_abort = false;

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::DISCONNECT);
  resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleTransferConfigure(bool /*in_isr*/, const uint8_t* req,
                                                  uint16_t req_len, uint8_t* resp,
                                                  uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::TRANSFER_CONFIGURE);

  // Req: [0]=0x04 [1]=idle_cycles [2..3]=wait_retry [4..5]=match_retry
  if (req_len < 6u)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t IDLE = req[1];

  uint16_t wait_retry = 0u;
  uint16_t match_retry = 0u;
  Memory::FastCopy(&wait_retry, &req[2], sizeof(wait_retry));
  Memory::FastCopy(&match_retry, &req[4], sizeof(match_retry));

  dap_state_.transfer_cfg.idle_cycles = IDLE;
  dap_state_.transfer_cfg.retry_count = wait_retry;
  dap_state_.transfer_cfg.match_retry = match_retry;

  // Map to SWD transaction policy
  LibXR::Debug::Swd::TransferPolicy pol = swd_.GetTransferPolicy();
  pol.idle_cycles = IDLE;
  pol.wait_retry = wait_retry;
  swd_.SetTransferPolicy(pol);

  resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleTransferAbort(bool /*in_isr*/, const uint8_t* /*req*/,
                                              uint16_t /*req_len*/, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  SetTransferAbortFlag(true);

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::TRANSFER_ABORT);
  resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleWriteABORT(bool /*in_isr*/, const uint8_t* req,
                                           uint16_t req_len, uint8_t* resp,
                                           uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::WRITE_ABORT);

  if (req_len < 6u)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  uint32_t flags = 0u;
  Memory::FastCopy(&flags, &req[2], sizeof(flags));

  LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;

  const ErrorCode EC = swd_.WriteAbortTxn(flags, ack);
  resp[1] = (EC == ErrorCode::OK && ack == LibXR::Debug::SwdProtocol::Ack::OK)
                ? to_u8(LibXR::USB::DapLinkV2Def::Status::OK)
                : to_u8(LibXR::USB::DapLinkV2Def::Status::ERROR);

  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleDelay(bool /*in_isr*/, const uint8_t* req,
                                      uint16_t req_len, uint8_t* resp, uint16_t resp_cap,
                                      uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::DELAY);

  if (req_len < 3u)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  uint16_t us = 0u;
  Memory::FastCopy(&us, &req[1], sizeof(us));

  LibXR::Timebase::DelayMicroseconds(us);

  resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleResetTarget(bool in_isr, const uint8_t* /*req*/,
                                            uint16_t /*req_len*/, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  if (!resp || resp_cap < 3u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::RESET_TARGET);

  uint8_t execute = 0u;

  if (nreset_gpio_ != nullptr)
  {
    DriveReset(false);
    DelayUsIfAllowed(in_isr, 1000u);
    DriveReset(true);
    DelayUsIfAllowed(in_isr, 1000u);
    execute = 1u;
  }

  // 关键：无论是否实现 reset，都返回 DAP_OK；未实现则 Execute=0
  resp[1] = DAP_OK;
  resp[2] = execute;
  out_len = 3u;
  return ErrorCode::OK;
}

// ============================================================================
// SWJ / SWD handlers
// ============================================================================

ErrorCode DapLinkV2Class::HandleSWJPins(bool /*in_isr*/, const uint8_t* req,
                                        uint16_t req_len, uint8_t* resp,
                                        uint16_t resp_cap, uint16_t& out_len)
{
  if (!resp || resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_PINS);

  // Req: [0]=0x10 [1]=PinOut [2]=PinSelect [3..6]=PinWait(us)
  if (!req || req_len < 7u)
  {
    resp[1] = 0u;
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t PIN_OUT = req[1];
  const uint8_t PIN_SEL = req[2];

  uint32_t wait_us = 0u;
  Memory::FastCopy(&wait_us, &req[3], sizeof(wait_us));

  // 0) Latch requested states into shadow for ALL selected pins (even if unsupported)
  swj_shadow_ = static_cast<uint8_t>((swj_shadow_ & static_cast<uint8_t>(~PIN_SEL)) |
                                     (PIN_OUT & PIN_SEL));

  // 1) Only physically support nRESET (best-effort)
  if ((PIN_SEL & LibXR::USB::DapLinkV2Def::DAP_SWJ_NRESET) != 0u)
  {
    const bool LEVEL_HIGH = ((PIN_OUT & LibXR::USB::DapLinkV2Def::DAP_SWJ_NRESET) != 0u);
    DriveReset(LEVEL_HIGH);
  }

  auto read_pins = [&]() -> uint8_t
  {
    uint8_t pin_in = swj_shadow_;

    if (nreset_gpio_ != nullptr)
    {
      if (nreset_gpio_->Read())
      {
        pin_in |= LibXR::USB::DapLinkV2Def::DAP_SWJ_NRESET;
      }
      else
      {
        pin_in = static_cast<uint8_t>(
            pin_in & static_cast<uint8_t>(~LibXR::USB::DapLinkV2Def::DAP_SWJ_NRESET));
      }
    }

    return pin_in;
  };

  uint8_t pin_in = 0u;

  if (wait_us == 0u || PIN_SEL == 0u)
  {
    pin_in = read_pins();
  }
  else
  {
    const uint64_t START = LibXR::Timebase::GetMicroseconds();
    const uint8_t EXPECT = static_cast<uint8_t>(PIN_OUT & PIN_SEL);

    do
    {
      pin_in = read_pins();
      if ((pin_in & PIN_SEL) == EXPECT)
      {
        break;
      }
    } while ((static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds()) - START) <
             wait_us);
  }

  resp[1] = pin_in;
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleSWJClock(bool /*in_isr*/, const uint8_t* req,
                                         uint16_t req_len, uint8_t* resp,
                                         uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_CLOCK);

  if (req_len < 5u)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::ERROR);
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  uint32_t hz = 0u;
  Memory::FastCopy(&hz, &req[1], sizeof(hz));

  swj_clock_hz_ = hz;
  (void)swd_.SetClockHz(hz);

  resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleSWJSequence(bool /*in_isr*/, const uint8_t* req,
                                            uint16_t req_len, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  if (!resp || resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::SWJ_SEQUENCE);
  resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;

  // Req: [0]=0x12 [1]=bit_count(0=>256) [2..]=data (LSB-first)
  if (!req || req_len < 2u)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::ERROR);
    return ErrorCode::ARG_ERR;
  }

  const uint8_t RAW_COUNT = req[1];
  const uint32_t BIT_COUNT = (RAW_COUNT == 0u) ? 256u : static_cast<uint32_t>(RAW_COUNT);
  const uint32_t BYTE_COUNT = (BIT_COUNT + 7u) / 8u;

  if (2u + BYTE_COUNT > req_len)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::ERROR);
    return ErrorCode::ARG_ERR;
  }

  const uint8_t* data_bits = &req[2];

  const ErrorCode EC = swd_.SeqWriteBits(BIT_COUNT, data_bits);
  if (EC != ErrorCode::OK)
  {
    resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::ERROR);
    return ErrorCode::OK;
  }

  swj_shadow_ = static_cast<uint8_t>(
      swj_shadow_ & static_cast<uint8_t>(~LibXR::USB::DapLinkV2Def::DAP_SWJ_SWCLK_TCK));

  bool last_swdio = false;
  if (BIT_COUNT != 0u)
  {
    const uint32_t LAST_I = BIT_COUNT - 1u;
    last_swdio = (((data_bits[LAST_I / 8u] >> (LAST_I & 7u)) & 0x01u) != 0u);
  }

  if (last_swdio)
  {
    swj_shadow_ |= LibXR::USB::DapLinkV2Def::DAP_SWJ_SWDIO_TMS;
  }
  else
  {
    swj_shadow_ = static_cast<uint8_t>(
        swj_shadow_ & static_cast<uint8_t>(~LibXR::USB::DapLinkV2Def::DAP_SWJ_SWDIO_TMS));
  }

  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleSWDConfigure(bool /*in_isr*/, const uint8_t* /*req*/,
                                             uint16_t /*req_len*/, uint8_t* resp,
                                             uint16_t resp_cap, uint16_t& out_len)
{
  if (resp_cap < 2u)
  {
    out_len = 0;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::SWD_CONFIGURE);

  resp[1] = to_u8(LibXR::USB::DapLinkV2Def::Status::OK);
  out_len = 2u;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleSWDSequence(bool /*in_isr*/, const uint8_t* req,
                                            uint16_t req_len, uint8_t* resp,
                                            uint16_t resp_cap, uint16_t& out_len)
{
  if (!req || !resp || resp_cap < 2u)
  {
    out_len = 0u;
    return ErrorCode::ARG_ERR;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::SWD_SEQUENCE);
  resp[1] = DAP_OK;
  out_len = 2u;

  if (req_len < 2u)
  {
    resp[1] = DAP_ERROR;
    out_len = 2u;
    return ErrorCode::ARG_ERR;
  }

  const uint8_t SEQ_CNT = req[1];
  uint16_t req_off = 2u;
  uint16_t resp_off = 2u;

  for (uint32_t s = 0; s < SEQ_CNT; ++s)
  {
    if (req_off >= req_len)
    {
      resp[1] = DAP_ERROR;
      out_len = 2u;
      return ErrorCode::ARG_ERR;
    }

    const uint8_t INFO = req[req_off++];

    uint32_t cycles = static_cast<uint32_t>(INFO & 0x3Fu);
    if (cycles == 0u)
    {
      cycles = 64u;
    }

    const bool MODE_IN = ((INFO & 0x80u) != 0u);
    const uint16_t BYTES = static_cast<uint16_t>((cycles + 7u) / 8u);

    if (!MODE_IN)
    {
      if (req_off + BYTES > req_len)
      {
        resp[1] = DAP_ERROR;
        out_len = 2u;
        return ErrorCode::ARG_ERR;
      }

      const uint8_t* data_bits = &req[req_off];
      req_off = static_cast<uint16_t>(req_off + BYTES);

      const ErrorCode EC = swd_.SeqWriteBits(cycles, data_bits);
      if (EC != ErrorCode::OK)
      {
        resp[1] = DAP_ERROR;
        out_len = 2u;
        return ErrorCode::OK;
      }
    }
    else
    {
      if (resp_off + BYTES > resp_cap)
      {
        resp[1] = DAP_ERROR;
        out_len = 2u;
        return ErrorCode::NOT_FOUND;
      }

      Memory::FastSet(&resp[resp_off], 0, BYTES);

      const ErrorCode EC = swd_.SeqReadBits(cycles, &resp[resp_off]);
      if (EC != ErrorCode::OK)
      {
        resp[1] = DAP_ERROR;
        out_len = 2u;
        return ErrorCode::OK;
      }

      resp_off = static_cast<uint16_t>(resp_off + BYTES);
    }
  }

  out_len = resp_off;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleQueueCommands(bool /*in_isr*/, const uint8_t* /*req*/,
                                              uint16_t /*req_len*/, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  return build_cmd_status_response(
      to_u8(LibXR::USB::DapLinkV2Def::CommandId::QUEUE_COMMANDS), DAP_ERROR, resp,
      resp_cap, out_len);
}

ErrorCode DapLinkV2Class::HandleExecuteCommands(bool /*in_isr*/, const uint8_t* /*req*/,
                                                uint16_t /*req_len*/, uint8_t* resp,
                                                uint16_t resp_cap, uint16_t& out_len)
{
  return build_cmd_status_response(
      to_u8(LibXR::USB::DapLinkV2Def::CommandId::EXECUTE_COMMANDS), DAP_ERROR, resp,
      resp_cap, out_len);
}

// ============================================================================
// Transfer helpers
// ============================================================================

uint8_t DapLinkV2Class::MapAckToDapResp(LibXR::Debug::SwdProtocol::Ack ack) const
{
  switch (ack)
  {
    case LibXR::Debug::SwdProtocol::Ack::OK:
      return 1u;
    case LibXR::Debug::SwdProtocol::Ack::WAIT:
      return 2u;
    case LibXR::Debug::SwdProtocol::Ack::FAULT:
      return 4u;
    case LibXR::Debug::SwdProtocol::Ack::NO_ACK:
    case LibXR::Debug::SwdProtocol::Ack::PROTOCOL:
    default:
      return 7u;
  }
}

void DapLinkV2Class::SetTransferAbortFlag(bool on) { dap_state_.transfer_abort = on; }

// ============================================================================
// DAP_Transfer / DAP_TransferBlock
// ============================================================================

ErrorCode DapLinkV2Class::HandleTransfer(bool /*in_isr*/, const uint8_t* req,
                                         uint16_t req_len, uint8_t* resp,
                                         uint16_t resp_cap, uint16_t& out_len)
{
  out_len = 0u;
  if (!req || !resp || resp_cap < 3u)
  {
    return ErrorCode::ARG_ERR;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::TRANSFER);
  resp[1] = 0u;  // response_count = #successful transfers
  resp[2] = 0u;  // response_value (ACK bits / ERROR / MISMATCH)
  uint16_t resp_off = 3u;

  // Req: [0]=CMD [1]=DAP index [2]=count [3..]=transfers...
  if (req_len < 3u)
  {
    resp[2] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    out_len = 3u;
    return ErrorCode::ARG_ERR;
  }

  if (dap_state_.transfer_abort)
  {
    dap_state_.transfer_abort = false;
    resp[2] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    out_len = 3u;
    return ErrorCode::OK;
  }

  const uint8_t COUNT = req[2];
  uint16_t req_off = 3u;

  auto ack_to_dap = [&](LibXR::Debug::SwdProtocol::Ack ack) -> uint8_t
  {
    switch (ack)
    {
      case LibXR::Debug::SwdProtocol::Ack::OK:
        return LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK;  // 0x01
      case LibXR::Debug::SwdProtocol::Ack::WAIT:
        return LibXR::USB::DapLinkV2Def::DAP_TRANSFER_WAIT;  // 0x02
      case LibXR::Debug::SwdProtocol::Ack::FAULT:
        return LibXR::USB::DapLinkV2Def::DAP_TRANSFER_FAULT;  // 0x04
      default:
        return 0x07u;  // protocol error / no-ack: 0b111
    }
  };

  auto push_u32 = [&](uint32_t v) -> bool
  {
    if (resp_off + 4u > resp_cap)
    {
      return false;
    }
    Memory::FastCopy(&resp[resp_off], &v, sizeof(v));
    resp_off = static_cast<uint16_t>(resp_off + 4u);
    return true;
  };

  auto push_timestamp = [&]() -> bool
  {
    const uint32_t T = static_cast<uint32_t>(LibXR::Timebase::GetMicroseconds());
    return push_u32(T);
  };

  auto ensure_space = [&](uint16_t bytes) -> bool
  { return (resp_off + bytes) <= resp_cap; };

  auto bytes_for_read = [&](bool need_ts) -> uint16_t
  { return static_cast<uint16_t>(need_ts ? 8u : 4u); };

  uint8_t response_count = 0u;
  uint8_t response_value = 0u;

  bool check_write = false;

  struct PendingApRead
  {
    bool valid = false;
    bool need_ts = false;
  } pending;

  auto emit_read_with_ts = [&](bool need_ts, uint32_t data) -> bool
  {
    if (need_ts)
    {
      if (!push_timestamp())
      {
        return false;
      }
    }
    if (!push_u32(data))
    {
      return false;
    }
    response_count++;
    return true;
  };

  auto complete_pending_by_rdbuff = [&]() -> bool
  {
    if (!pending.valid)
    {
      return true;
    }

    if (!ensure_space(bytes_for_read(pending.need_ts)))
    {
      response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
      return false;
    }

    uint32_t rdata = 0u;
    LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
    const ErrorCode EC = swd_.DpReadRdbuffTxn(rdata, ack);

    const uint8_t V = ack_to_dap(ack);
    if (V != LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK)
    {
      response_value = V;
      return false;
    }
    if (EC != ErrorCode::OK)
    {
      response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
      return false;
    }

    if (!emit_read_with_ts(pending.need_ts, rdata))
    {
      response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
      return false;
    }

    pending.valid = false;
    pending.need_ts = false;

    check_write = false;
    response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK;
    return true;
  };

  auto flush_pending_if_any = [&]() -> bool
  { return pending.valid ? complete_pending_by_rdbuff() : true; };

  for (uint32_t i = 0; i < COUNT; ++i)
  {
    if (req_off >= req_len)
    {
      response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
      break;
    }

    const uint8_t RQ = req[req_off++];

    const bool AP = LibXR::USB::DapLinkV2Def::req_is_ap(RQ);
    const bool RNW = LibXR::USB::DapLinkV2Def::req_is_read(RQ);
    const uint8_t ADDR2B = LibXR::USB::DapLinkV2Def::req_addr2b(RQ);

    const bool TS = LibXR::USB::DapLinkV2Def::req_need_timestamp(RQ);
    const bool MATCH_VALUE =
        ((RQ & LibXR::USB::DapLinkV2Def::DAP_TRANSFER_MATCH_VALUE) != 0u);
    const bool MATCH_MASK =
        ((RQ & LibXR::USB::DapLinkV2Def::DAP_TRANSFER_MATCH_MASK) != 0u);

    if (TS && (MATCH_VALUE || MATCH_MASK))
    {
      response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
      break;
    }

    LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
    ErrorCode ec = ErrorCode::OK;

    if (!RNW)
    {
      if (!flush_pending_if_any())
      {
        break;
      }

      if (req_off + 4u > req_len)
      {
        response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      uint32_t wdata = 0u;
      Memory::FastCopy(&wdata, &req[req_off], sizeof(wdata));
      req_off = static_cast<uint16_t>(req_off + 4u);

      if (MATCH_MASK)
      {
        match_mask_ = wdata;
        response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK;
        response_count++;
        continue;
      }

      if (AP)
      {
        ec = swd_.ApWriteTxn(ADDR2B, wdata, ack);
      }
      else
      {
        ec = swd_.DpWriteTxn(static_cast<LibXR::Debug::SwdProtocol::DpWriteReg>(ADDR2B),
                             wdata, ack);
      }

      response_value = ack_to_dap(ack);
      if (response_value != LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK)
      {
        break;
      }
      if (ec != ErrorCode::OK)
      {
        response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (TS)
      {
        if (!push_timestamp())
        {
          response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
          break;
        }
      }

      response_count++;
      check_write = true;
    }
    else
    {
      if (MATCH_VALUE)
      {
        if (!flush_pending_if_any())
        {
          break;
        }

        if (req_off + 4u > req_len)
        {
          response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
          break;
        }

        uint32_t match_val = 0u;
        Memory::FastCopy(&match_val, &req[req_off], sizeof(match_val));
        req_off = static_cast<uint16_t>(req_off + 4u);

        uint32_t rdata = 0u;
        uint32_t retry = dap_state_.transfer_cfg.match_retry;
        bool matched = false;

        while (true)
        {
          if (AP)
          {
            ec = swd_.ApReadTxn(ADDR2B, rdata, ack);
            if (ec == ErrorCode::OK && ack == LibXR::Debug::SwdProtocol::Ack::OK)
            {
              check_write = false;
            }
          }
          else
          {
            ec = swd_.DpReadTxn(static_cast<LibXR::Debug::SwdProtocol::DpReadReg>(ADDR2B),
                                rdata, ack);
          }

          response_value = ack_to_dap(ack);
          if (response_value != LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK)
          {
            break;
          }
          if (ec != ErrorCode::OK)
          {
            response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
            break;
          }

          if ((rdata & match_mask_) == (match_val & match_mask_))
          {
            matched = true;
            break;
          }

          if (retry == 0u)
          {
            break;
          }
          --retry;
        }

        if (response_value != LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK)
        {
          break;
        }

        if (!matched)
        {
          response_value =
              static_cast<uint8_t>(LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK |
                                   LibXR::USB::DapLinkV2Def::DAP_TRANSFER_MISMATCH);
          break;
        }

        response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK;
        response_count++;
        continue;
      }

      if (!AP)
      {
        if (!flush_pending_if_any())
        {
          break;
        }

        uint32_t rdata = 0u;
        ec = swd_.DpReadTxn(static_cast<LibXR::Debug::SwdProtocol::DpReadReg>(ADDR2B),
                            rdata, ack);

        response_value = ack_to_dap(ack);
        if (response_value != LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK)
        {
          break;
        }
        if (ec != ErrorCode::OK)
        {
          response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
          break;
        }

        if (!ensure_space(bytes_for_read(TS)))
        {
          response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
          break;
        }

        if (!emit_read_with_ts(TS, rdata))
        {
          response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
          break;
        }

        response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK;
        continue;
      }

      if (!pending.valid)
      {
        uint32_t dummy_posted = 0u;
        ec = swd_.ApReadPostedTxn(ADDR2B, dummy_posted, ack);

        response_value = ack_to_dap(ack);
        if (response_value != LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK)
        {
          break;
        }
        if (ec != ErrorCode::OK)
        {
          response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
          break;
        }

        pending.valid = true;
        pending.need_ts = TS;

        response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK;
      }
      else
      {
        if (!ensure_space(bytes_for_read(pending.need_ts)))
        {
          response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
          break;
        }

        uint32_t posted_prev = 0u;
        ec = swd_.ApReadPostedTxn(ADDR2B, posted_prev, ack);

        const uint8_t CUR_V = ack_to_dap(ack);
        if (CUR_V != LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK || ec != ErrorCode::OK)
        {
          const uint8_t PROOR_FAIL = (CUR_V != LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK)
                                         ? CUR_V
                                         : LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;

          if (!complete_pending_by_rdbuff())
          {
            break;
          }

          response_value = PROOR_FAIL;
          break;
        }

        if (!emit_read_with_ts(pending.need_ts, posted_prev))
        {
          response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
          break;
        }

        pending.valid = true;
        pending.need_ts = TS;

        response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK;
      }
    }

    if (dap_state_.transfer_abort)
    {
      dap_state_.transfer_abort = false;
      break;
    }
  }

  if (pending.valid)
  {
    const uint8_t PRIOR_FAIL = response_value;

    if (!complete_pending_by_rdbuff())
    {
      // keep pending fail
    }
    else
    {
      if (PRIOR_FAIL != 0u && PRIOR_FAIL != LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK)
      {
        response_value = PRIOR_FAIL;
      }
    }
  }

  if (response_value == LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK && check_write)
  {
    uint32_t dummy = 0u;
    LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
    const ErrorCode EC = swd_.DpReadRdbuffTxn(dummy, ack);
    const uint8_t V = ack_to_dap(ack);

    if (V != LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK)
    {
      response_value = V;
    }
    else if (EC != ErrorCode::OK)
    {
      response_value = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    }
  }

  resp[1] = response_count;
  resp[2] = response_value;
  out_len = resp_off;
  return ErrorCode::OK;
}

ErrorCode DapLinkV2Class::HandleTransferBlock(bool /*in_isr*/, const uint8_t* req,
                                              uint16_t req_len, uint8_t* resp,
                                              uint16_t resp_cap, uint16_t& out_len)
{
  if (!resp || resp_cap < 4u)
  {
    out_len = 0u;
    return ErrorCode::NOT_FOUND;
  }

  resp[0] = to_u8(LibXR::USB::DapLinkV2Def::CommandId::TRANSFER_BLOCK);
  resp[1] = 0u;
  resp[2] = 0u;
  resp[3] = 0u;
  out_len = 4u;

  if (!req || req_len < 5u)
  {
    resp[3] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    return ErrorCode::ARG_ERR;
  }

  if (dap_state_.transfer_abort)
  {
    dap_state_.transfer_abort = false;
    resp[3] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    return ErrorCode::OK;
  }

  uint16_t count = 0u;
  Memory::FastCopy(&count, &req[2], sizeof(count));

  const uint8_t DAP_RQ = req[4];

  if ((DAP_RQ & (LibXR::USB::DapLinkV2Def::DAP_TRANSFER_MATCH_VALUE |
                 LibXR::USB::DapLinkV2Def::DAP_TRANSFER_MATCH_MASK)) != 0u)
  {
    resp[3] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    return ErrorCode::NOT_SUPPORT;
  }
  if (LibXR::USB::DapLinkV2Def::req_need_timestamp(DAP_RQ))
  {
    resp[3] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
    return ErrorCode::NOT_SUPPORT;
  }

  if (count == 0u)
  {
    const uint16_t DONE0 = 0u;
    Memory::FastCopy(&resp[1], &DONE0, sizeof(DONE0));
    resp[3] = LibXR::USB::DapLinkV2Def::DAP_TRANSFER_OK;
    out_len = 4u;
    return ErrorCode::OK;
  }

  const bool AP = LibXR::USB::DapLinkV2Def::req_is_ap(DAP_RQ);
  const bool RNW = LibXR::USB::DapLinkV2Def::req_is_read(DAP_RQ);
  const uint8_t ADDR2B = LibXR::USB::DapLinkV2Def::req_addr2b(DAP_RQ);

  uint16_t done = 0u;
  uint8_t xresp = 0u;

  uint16_t req_off = 5u;
  uint16_t resp_off = 4u;

  if (!RNW)
  {
    for (uint32_t i = 0; i < count; ++i)
    {
      LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
      ErrorCode ec = ErrorCode::OK;

      if (req_off + 4u > req_len)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      uint32_t wdata = 0u;
      Memory::FastCopy(&wdata, &req[req_off], sizeof(wdata));
      req_off = static_cast<uint16_t>(req_off + 4u);

      if (AP)
      {
        ec = swd_.ApWriteTxn(ADDR2B, wdata, ack);
      }
      else
      {
        ec = swd_.DpWriteTxn(static_cast<LibXR::Debug::SwdProtocol::DpWriteReg>(ADDR2B),
                             wdata, ack);
      }

      xresp = MapAckToDapResp(ack);
      if (xresp == 0u)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (ack != LibXR::Debug::SwdProtocol::Ack::OK)
      {
        break;
      }

      if (ec != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      done = static_cast<uint16_t>(i + 1u);
    }

    Memory::FastCopy(&resp[1], &done, sizeof(done));
    resp[3] = xresp;
    out_len = resp_off;
    return ErrorCode::OK;
  }

  if (!AP)
  {
    for (uint32_t i = 0; i < count; ++i)
    {
      LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
      ErrorCode ec = ErrorCode::OK;
      uint32_t rdata = 0u;

      if (resp_off + 4u > resp_cap)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      ec = swd_.DpReadTxn(static_cast<LibXR::Debug::SwdProtocol::DpReadReg>(ADDR2B),
                          rdata, ack);

      xresp = MapAckToDapResp(ack);
      if (xresp == 0u)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      if (ack != LibXR::Debug::SwdProtocol::Ack::OK)
      {
        break;
      }

      if (ec != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        break;
      }

      Memory::FastCopy(&resp[resp_off], &rdata, sizeof(rdata));
      resp_off = static_cast<uint16_t>(resp_off + 4u);
      done = static_cast<uint16_t>(i + 1u);
    }

    Memory::FastCopy(&resp[1], &done, sizeof(done));
    resp[3] = xresp;
    out_len = resp_off;
    return ErrorCode::OK;
  }

  {
    LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
    ErrorCode ec = ErrorCode::OK;

    uint32_t dummy_posted = 0u;
    ec = swd_.ApReadPostedTxn(ADDR2B, dummy_posted, ack);
    xresp = MapAckToDapResp(ack);

    if (xresp == 0u)
    {
      xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
      goto out_ap_read;  // NOLINT
    }
    if (ack != LibXR::Debug::SwdProtocol::Ack::OK)
    {
      goto out_ap_read;  // NOLINT
    }
    if (ec != ErrorCode::OK)
    {
      xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
      goto out_ap_read;  // NOLINT
    }

    for (uint32_t i = 1; i < count; ++i)
    {
      if (resp_off + 4u > resp_cap)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        goto out_ap_read;  // NOLINT
      }

      uint32_t posted_prev = 0u;
      ec = swd_.ApReadPostedTxn(ADDR2B, posted_prev, ack);
      const uint8_t CUR = MapAckToDapResp(ack);

      if (CUR == 0u)
      {
        xresp = CUR | LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        goto out_ap_read;  // NOLINT
      }

      if (ack != LibXR::Debug::SwdProtocol::Ack::OK || ec != ErrorCode::OK)
      {
        if (resp_off + 4u <= resp_cap)
        {
          uint32_t last = 0u;
          LibXR::Debug::SwdProtocol::Ack ack2 = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
          const ErrorCode EC2 = swd_.DpReadRdbuffTxn(last, ack2);
          const uint8_t V2 = MapAckToDapResp(ack2);

          if (V2 == 1u && EC2 == ErrorCode::OK)
          {
            Memory::FastCopy(&resp[resp_off], &last, sizeof(last));
            resp_off = static_cast<uint16_t>(resp_off + 4u);
            done = static_cast<uint16_t>(i);
          }
          else
          {
            xresp = V2;
            if (EC2 != ErrorCode::OK)
            {
              xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
            }
            goto out_ap_read;  // NOLINT
          }
        }

        xresp = CUR;
        if (ec != ErrorCode::OK)
        {
          xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        }
        goto out_ap_read;  // NOLINT
      }

      Memory::FastCopy(&resp[resp_off], &posted_prev, sizeof(posted_prev));
      resp_off = static_cast<uint16_t>(resp_off + 4u);
      done = static_cast<uint16_t>(i);
      xresp = CUR;
    }

    if (resp_off + 4u > resp_cap)
    {
      xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
      goto out_ap_read;  // NOLINT
    }

    {
      uint32_t last = 0u;
      LibXR::Debug::SwdProtocol::Ack ack2 = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
      const ErrorCode EC2 = swd_.DpReadRdbuffTxn(last, ack2);
      const uint8_t V2 = MapAckToDapResp(ack2);

      xresp = V2;
      if (V2 != 1u)
      {
        goto out_ap_read;  // NOLINT
      }
      if (EC2 != ErrorCode::OK)
      {
        xresp |= LibXR::USB::DapLinkV2Def::DAP_TRANSFER_ERROR;
        goto out_ap_read;  // NOLINT
      }

      Memory::FastCopy(&resp[resp_off], &last, sizeof(last));
      resp_off = static_cast<uint16_t>(resp_off + 4u);
      done = count;
    }

  out_ap_read:
    Memory::FastCopy(&resp[1], &done, sizeof(done));
    resp[3] = xresp;
    out_len = resp_off;
    return ErrorCode::OK;
  }
}

// ============================================================================
// Reset helpers
// ============================================================================

void DapLinkV2Class::DriveReset(bool release)
{
  last_nreset_level_high_ = release;

  if (release)
  {
    swj_shadow_ |= LibXR::USB::DapLinkV2Def::DAP_SWJ_NRESET;
  }
  else
  {
    swj_shadow_ = static_cast<uint8_t>(
        swj_shadow_ & static_cast<uint8_t>(~LibXR::USB::DapLinkV2Def::DAP_SWJ_NRESET));
  }

  if (nreset_gpio_ != nullptr)
  {
    (void)nreset_gpio_->Write(release);
  }
}

void DapLinkV2Class::DelayUsIfAllowed(bool /*in_isr*/, uint32_t us)
{
  LibXR::Timebase::DelayMicroseconds(us);
}

}  // namespace LibXR::USB
