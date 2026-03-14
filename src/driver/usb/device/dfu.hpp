#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

#include "crc.hpp"
#include "dev_core.hpp"
#include "flash.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"
#include "timebase.hpp"
#include "usb/core/desc_cfg.hpp"
#include "usb/core/desc_str.hpp"

namespace LibXR::USB
{
enum class DFURequest : uint8_t
{
  DETACH = 0x00,
  DNLOAD = 0x01,
  UPLOAD = 0x02,
  GETSTATUS = 0x03,
  CLRSTATUS = 0x04,
  GETSTATE = 0x05,
  ABORT = 0x06,
};

enum class DFUState : uint8_t
{
  APP_IDLE = 0x00,
  APP_DETACH = 0x01,
  DFU_IDLE = 0x02,
  DFU_DNLOAD_SYNC = 0x03,
  DFU_DNBUSY = 0x04,
  DFU_DNLOAD_IDLE = 0x05,
  DFU_MANIFEST_SYNC = 0x06,
  DFU_MANIFEST = 0x07,
  DFU_MANIFEST_WAIT_RESET = 0x08,
  DFU_UPLOAD_IDLE = 0x09,
  DFU_ERROR = 0x0A,
};

enum class DFUStatusCode : uint8_t
{
  OK = 0x00,
  ERR_TARGET = 0x01,
  ERR_FILE = 0x02,
  ERR_WRITE = 0x03,
  ERR_ERASE = 0x04,
  ERR_CHECK_ERASED = 0x05,
  ERR_PROG = 0x06,
  ERR_VERIFY = 0x07,
  ERR_ADDRESS = 0x08,
  ERR_NOTDONE = 0x09,
  ERR_FIRMWARE = 0x0A,
  ERR_VENDOR = 0x0B,
  ERR_USBR = 0x0C,
  ERR_POR = 0x0D,
  ERR_UNKNOWN = 0x0E,
  ERR_STALLEDPKT = 0x0F,
};

struct DFUCapabilities
{
  bool can_download = true;
  bool can_upload = true;
  bool manifestation_tolerant = true;
  bool will_detach = false;
  uint16_t detach_timeout_ms = 1000u;
  uint16_t transfer_size = 1024u;
};

class DfuRuntimeClass : public DeviceClass
{
 public:
  using JumpCallback = void (*)(void*);
  static constexpr const char* kInterfaceStringDefault = "XRUSB DFU RT";

  explicit DfuRuntimeClass(JumpCallback jump_to_bootloader, void* jump_ctx = nullptr,
                           uint16_t detach_timeout_ms = 50u,
                           const char* interface_string = kInterfaceStringDefault)
      : jump_to_bootloader_(jump_to_bootloader),
        jump_ctx_(jump_ctx),
        interface_string_(interface_string),
        default_detach_timeout_ms_(detach_timeout_ms)
  {
  }

  const char* GetInterfaceString(size_t local_interface_index) const override
  {
    return (local_interface_index == 0u) ? interface_string_ : nullptr;
  }

  void Process()
  {
    if (!detach_pending_)
    {
      return;
    }

    const uint32_t now_ms = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
    if (static_cast<int32_t>(now_ms - detach_deadline_ms_) < 0)
    {
      return;
    }

    detach_pending_ = false;
    if (jump_to_bootloader_ != nullptr)
    {
      jump_to_bootloader_(jump_ctx_);
    }
  }

 protected:
#pragma pack(push, 1)
  struct FunctionalDescriptor
  {
    uint8_t bLength = 9;
    uint8_t bDescriptorType = 0x21;
    uint8_t bmAttributes = 0x08u;
    uint16_t wDetachTimeOut = 0;
    uint16_t wTransferSize = 0;
    uint16_t bcdDFUVersion = 0x0110u;
  };

  struct StatusResponse
  {
    uint8_t bStatus = static_cast<uint8_t>(DFUStatusCode::OK);
    uint8_t bwPollTimeout[3] = {0, 0, 0};
    uint8_t bState = static_cast<uint8_t>(DFUState::APP_IDLE);
    uint8_t iString = 0;

    void SetPollTimeout(uint32_t timeout_ms)
    {
      bwPollTimeout[0] = static_cast<uint8_t>(timeout_ms & 0xFFu);
      bwPollTimeout[1] = static_cast<uint8_t>((timeout_ms >> 8) & 0xFFu);
      bwPollTimeout[2] = static_cast<uint8_t>((timeout_ms >> 16) & 0xFFu);
    }
  };

  struct DescriptorBlock
  {
    ConfigDescriptorItem::InterfaceDescriptor interface_desc = {
        9, static_cast<uint8_t>(DescriptorType::INTERFACE), 0, 0, 0, 0xFEu, 0x01u, 0x01u,
        0};
    FunctionalDescriptor func_desc = {};
  };
#pragma pack(pop)

  void BindEndpoints(EndpointPool&, uint8_t start_itf_num, bool) override
  {
    interface_num_ = start_itf_num;
    current_alt_setting_ = 0u;
    detach_pending_ = false;
    detach_timeout_ms_ = default_detach_timeout_ms_;
    state_ = DFUState::APP_IDLE;
    desc_block_.interface_desc.bInterfaceNumber = interface_num_;
    desc_block_.interface_desc.iInterface = GetInterfaceStringIndex(0u);
    desc_block_.func_desc.wDetachTimeOut = detach_timeout_ms_;
    desc_block_.func_desc.wTransferSize = 0u;
    SetData(RawData{reinterpret_cast<uint8_t*>(&desc_block_), sizeof(desc_block_)});
    inited_ = true;
  }

  void UnbindEndpoints(EndpointPool&, bool) override
  {
    detach_pending_ = false;
    state_ = DFUState::APP_IDLE;
    inited_ = false;
  }

  size_t GetInterfaceCount() override { return 1u; }
  bool HasIAD() override { return false; }
  size_t GetMaxConfigSize() override { return sizeof(desc_block_); }

  ErrorCode SetAltSetting(uint8_t itf, uint8_t alt) override
  {
    if (itf != interface_num_)
    {
      return ErrorCode::NOT_FOUND;
    }
    if (alt != 0u)
    {
      return ErrorCode::NOT_SUPPORT;
    }
    current_alt_setting_ = alt;
    return ErrorCode::OK;
  }

  ErrorCode GetAltSetting(uint8_t itf, uint8_t& alt) override
  {
    if (itf != interface_num_)
    {
      return ErrorCode::NOT_FOUND;
    }
    alt = current_alt_setting_;
    return ErrorCode::OK;
  }

  ErrorCode OnGetDescriptor(bool, uint8_t, uint16_t wValue, uint16_t,
                            ConstRawData& out_data) override
  {
    const uint8_t desc_type = static_cast<uint8_t>((wValue >> 8) & 0xFFu);
    if (desc_type != desc_block_.func_desc.bDescriptorType)
    {
      return ErrorCode::NOT_SUPPORT;
    }

    out_data = {reinterpret_cast<const uint8_t*>(&desc_block_.func_desc),
                sizeof(desc_block_.func_desc)};
    return ErrorCode::OK;
  }

  ErrorCode OnClassRequest(bool, uint8_t bRequest, uint16_t wValue, uint16_t wLength,
                           uint16_t wIndex, ControlTransferResult& result) override
  {
    if (!inited_)
    {
      return ErrorCode::INIT_ERR;
    }
    if ((wIndex & 0xFFu) != interface_num_)
    {
      return ErrorCode::NOT_FOUND;
    }

    switch (static_cast<DFURequest>(bRequest))
    {
      case DFURequest::DETACH:
        if (wLength != 0u || state_ != DFUState::APP_IDLE)
        {
          return ErrorCode::ARG_ERR;
        }
        detach_timeout_ms_ = (wValue == 0u) ? default_detach_timeout_ms_ : wValue;
        detach_deadline_ms_ = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds()) +
                              detach_timeout_ms_;
        detach_pending_ = true;
        state_ = DFUState::APP_DETACH;
        desc_block_.func_desc.wDetachTimeOut = detach_timeout_ms_;
        result.SendStatusInZLP() = true;
        return ErrorCode::OK;

      case DFURequest::GETSTATUS:
        status_response_.bStatus = static_cast<uint8_t>(DFUStatusCode::OK);
        status_response_.bState = static_cast<uint8_t>(state_);
        if (detach_pending_)
        {
          const uint32_t now_ms =
              static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
          const uint32_t remain_ms =
              (static_cast<int32_t>(detach_deadline_ms_ - now_ms) > 0)
                  ? (detach_deadline_ms_ - now_ms)
                  : 0u;
          status_response_.SetPollTimeout(remain_ms);
        }
        else
        {
          status_response_.SetPollTimeout(0u);
        }
        result.InData() = {reinterpret_cast<const uint8_t*>(&status_response_),
                           sizeof(status_response_)};
        return ErrorCode::OK;

      case DFURequest::GETSTATE:
        state_response_ = static_cast<uint8_t>(state_);
        result.InData() = {&state_response_, sizeof(state_response_)};
        return ErrorCode::OK;

      default:
        return ErrorCode::ARG_ERR;
    }
  }

 private:
  DescriptorBlock desc_block_ = {};
  StatusResponse status_response_ = {};
  JumpCallback jump_to_bootloader_ = nullptr;
  void* jump_ctx_ = nullptr;
  const char* interface_string_ = nullptr;
  uint8_t state_response_ = 0u;
  uint8_t interface_num_ = 0u;
  uint8_t current_alt_setting_ = 0u;
  bool inited_ = false;
  bool detach_pending_ = false;
  uint16_t default_detach_timeout_ms_ = 50u;
  uint16_t detach_timeout_ms_ = 50u;
  uint32_t detach_deadline_ms_ = 0u;
  DFUState state_ = DFUState::APP_IDLE;
};

class DfuFirmwareBackendDetail
{
 public:
  using JumpCallback = void (*)(void*);
  static constexpr uint32_t kSealMagic = 0x4C414553u;  // "SEAL"

#pragma pack(push, 1)
  struct SealRecord
  {
    uint32_t magic = kSealMagic;
    uint32_t image_size = 0u;
    uint32_t crc32 = 0u;
    uint32_t crc32_inv = 0u;
  };
#pragma pack(pop)

  DfuFirmwareBackendDetail(Flash& flash, size_t image_offset, size_t image_size_limit,
                           size_t seal_offset, JumpCallback jump_to_app,
                           void* jump_app_ctx = nullptr, bool autorun = true)
      : flash_(flash),
        image_offset_(image_offset),
        image_size_limit_(image_size_limit),
        seal_offset_(seal_offset),
        jump_to_app_(jump_to_app),
        jump_app_ctx_(jump_app_ctx),
        autorun_(autorun)
  {
    erase_block_size_ = flash_.MinEraseSize();
    if (erase_block_size_ == 0u)
    {
      erase_block_size_ = 1u;
    }
    erase_block_count_ = (image_size_limit_ + erase_block_size_ - 1u) / erase_block_size_;
    if (erase_block_count_ > 0u)
    {
      erased_blocks_ = new (std::nothrow) uint8_t[erase_block_count_];
      if (erased_blocks_ != nullptr)
      {
        std::memset(erased_blocks_, 0, erase_block_count_);
      }
    }
    seal_storage_size_ = erase_block_size_;
    if (seal_storage_size_ < sizeof(SealRecord))
    {
      seal_storage_size_ = sizeof(SealRecord);
    }
    seal_storage_ = new (std::nothrow) uint8_t[seal_storage_size_];
    transfer_size_ = PayloadLimit();
    if (transfer_size_ == 0u)
    {
      transfer_size_ = 1024u;
    }
    if (transfer_size_ > 4096u)
    {
      transfer_size_ = 4096u;
    }
    write_buffer_ = new (std::nothrow) uint8_t[transfer_size_];
    ResetTransferState();
    launch_requested_ = false;
    image_ready_ = false;
    stored_image_size_ = 0u;
  }

  ~DfuFirmwareBackendDetail()
  {
    delete[] erased_blocks_;
    delete[] seal_storage_;
    delete[] write_buffer_;
  }

  DfuFirmwareBackendDetail(const DfuFirmwareBackendDetail&) = delete;
  DfuFirmwareBackendDetail& operator=(const DfuFirmwareBackendDetail&) = delete;

  DFUCapabilities GetDfuCapabilities() const
  {
    DFUCapabilities caps = {};
    caps.can_download = true;
    caps.can_upload = true;
    caps.manifestation_tolerant = false;
    caps.will_detach = false;
    caps.detach_timeout_ms = 0u;

    if (transfer_size_ > 0xFFFFu)
    {
      caps.transfer_size = 0xFFFFu;
    }
    else
    {
      caps.transfer_size = static_cast<uint16_t>(transfer_size_);
    }
    return caps;
  }

  ErrorCode DfuSetAlternate(uint8_t alt)
  {
    return (alt == 0u) ? ErrorCode::OK : ErrorCode::NOT_SUPPORT;
  }

  void DfuAbort(uint8_t)
  {
    ResetTransferState();
    launch_requested_ = false;
    image_ready_ = false;
    stored_image_size_ = 0u;
  }

  void DfuClearStatus(uint8_t)
  {
    ResetTransferState();
    launch_requested_ = false;
  }

  DFUStatusCode DfuDownload(uint8_t alt, uint16_t block_num, ConstRawData data,
                            uint32_t& poll_timeout_ms)
  {
    poll_timeout_ms = 0u;

    if (alt != 0u)
    {
      return DFUStatusCode::ERR_TARGET;
    }
    if (data.addr_ == nullptr || data.size_ == 0u || data.size_ > transfer_size_)
    {
      return DFUStatusCode::ERR_USBR;
    }
    if ((erased_blocks_ == nullptr && erase_block_count_ > 0u) ||
        write_buffer_ == nullptr)
    {
      return DFUStatusCode::ERR_VENDOR;
    }
    if (HasPendingWrite() || HasPendingManifest())
    {
      return DFUStatusCode::ERR_NOTDONE;
    }

    if ((block_num == 0u) && (download_session_started_ &&
                              (received_bytes_ != 0u || expected_block_num_ != 0u)))
    {
      ResetTransferState();
    }

    if (!download_session_started_)
    {
      if (block_num != 0u)
      {
        return DFUStatusCode::ERR_ADDRESS;
      }
      StartDownloadSession();
    }
    else if (block_num != expected_block_num_)
    {
      return DFUStatusCode::ERR_ADDRESS;
    }

    const size_t offset = received_bytes_;
    const size_t payload_limit = PayloadLimit();
    if (offset + data.size_ < offset || (offset + data.size_) > payload_limit)
    {
      return DFUStatusCode::ERR_ADDRESS;
    }

    std::memcpy(write_buffer_, data.addr_, data.size_);
    pending_write_offset_ = offset;
    pending_write_len_ = data.size_;
    pending_write_block_num_ = block_num;
    write_pending_ = true;
    last_download_status_ = DFUStatusCode::OK;
    next_download_poll_timeout_ms_ = ComputeWritePollTimeout(offset, data.size_);
    poll_timeout_ms = next_download_poll_timeout_ms_;
    return DFUStatusCode::OK;
  }

  DFUStatusCode DfuGetDownloadStatus(uint8_t alt, bool& busy, uint32_t& poll_timeout_ms)
  {
    if (alt != 0u)
    {
      busy = false;
      poll_timeout_ms = 0u;
      return DFUStatusCode::ERR_TARGET;
    }
    busy = HasPendingWrite();
    poll_timeout_ms = busy ? next_download_poll_timeout_ms_ : 0u;
    return last_download_status_;
  }

  size_t DfuUpload(uint8_t alt, uint16_t block_num, RawData data, DFUStatusCode& status,
                   uint32_t& poll_timeout_ms)
  {
    poll_timeout_ms = 0u;
    status = DFUStatusCode::OK;

    if (alt != 0u)
    {
      status = DFUStatusCode::ERR_TARGET;
      return 0u;
    }
    if (data.addr_ == nullptr || data.size_ == 0u)
    {
      status = DFUStatusCode::ERR_USBR;
      return 0u;
    }

    if (block_num == 0u)
    {
      upload_session_started_ = true;
      upload_offset_ = 0u;
      upload_expected_block_num_ = 1u;
      size_t image_size = 0u;
      if (image_ready_)
      {
        image_size = stored_image_size_;
      }
      else if (!ProbeStoredImage(&image_size))
      {
        image_size = 0u;
      }
      upload_image_size_ = image_size;
      if (upload_image_size_ == 0u)
      {
        status = DFUStatusCode::ERR_FIRMWARE;
        return 0u;
      }
    }
    else if (!upload_session_started_ || block_num != upload_expected_block_num_)
    {
      status = DFUStatusCode::ERR_ADDRESS;
      return 0u;
    }

    if (upload_offset_ >= upload_image_size_)
    {
      return 0u;
    }

    size_t read_size = upload_image_size_ - upload_offset_;
    if (read_size > data.size_)
    {
      read_size = data.size_;
    }
    if (flash_.Read(image_offset_ + upload_offset_,
                    {reinterpret_cast<uint8_t*>(data.addr_), read_size}) != ErrorCode::OK)
    {
      status = DFUStatusCode::ERR_FIRMWARE;
      return 0u;
    }

    upload_offset_ += read_size;
    upload_expected_block_num_ = static_cast<uint16_t>(block_num + 1u);
    return read_size;
  }

  DFUStatusCode DfuManifest(uint8_t alt, uint32_t& poll_timeout_ms)
  {
    poll_timeout_ms = 0u;

    if (alt != 0u)
    {
      return DFUStatusCode::ERR_TARGET;
    }
    if (!download_session_started_ || received_bytes_ == 0u || HasPendingWrite())
    {
      return DFUStatusCode::ERR_NOTDONE;
    }
    manifest_pending_ = true;
    last_manifest_status_ = DFUStatusCode::OK;
    poll_timeout_ms = manifest_poll_timeout_ms_;
    return DFUStatusCode::OK;
  }

  DFUStatusCode DfuGetManifestStatus(uint8_t alt, bool& busy, uint32_t& poll_timeout_ms)
  {
    if (alt != 0u)
    {
      busy = false;
      poll_timeout_ms = 0u;
      return DFUStatusCode::ERR_TARGET;
    }
    busy = HasPendingManifest();
    poll_timeout_ms = busy ? manifest_poll_timeout_ms_ : 0u;
    return last_manifest_status_;
  }

  void Process()
  {
    if (write_pending_)
    {
      ProcessPendingWrite();
      return;
    }
    if (manifest_pending_)
    {
      ProcessPendingManifest();
    }
  }

  void RequestRunApp()
  {
    if (!image_ready_)
    {
      size_t image_size = 0u;
      image_ready_ = ProbeStoredImage(&image_size);
      stored_image_size_ = image_ready_ ? image_size : 0u;
    }
    if (image_ready_)
    {
      launch_requested_ = true;
    }
  }

  void DfuCommitManifestWaitReset(uint8_t alt)
  {
    if (alt != 0u)
    {
      return;
    }
    if (autorun_ && image_ready_)
    {
      launch_requested_ = true;
    }
  }

  bool TryConsumeAppLaunch(uint32_t)
  {
    if (!launch_requested_ || !image_ready_)
    {
      return false;
    }
    launch_requested_ = false;
    if (jump_to_app_ != nullptr)
    {
      jump_to_app_(jump_app_ctx_);
    }
    return true;
  }

  bool HasPendingWork() const
  {
    return HasPendingWrite() || HasPendingManifest() || launch_requested_;
  }

  bool HasValidImage() const { return image_ready_; }
  size_t ImageSize() const { return stored_image_size_; }

 private:
  size_t PayloadLimit() const
  {
    if (seal_offset_ > image_size_limit_)
    {
      return 0u;
    }
    return seal_offset_;
  }

  void StartDownloadSession()
  {
    ResetTransferState();
    if (erased_blocks_ != nullptr)
    {
      std::memset(erased_blocks_, 0, erase_block_count_);
    }
    download_session_started_ = true;
    image_ready_ = false;
    stored_image_size_ = 0u;
    launch_requested_ = false;
  }

  void ResetTransferState()
  {
    download_session_started_ = false;
    received_bytes_ = 0u;
    expected_block_num_ = 0u;
    pending_write_offset_ = 0u;
    pending_write_len_ = 0u;
    pending_write_block_num_ = 0u;
    write_pending_ = false;
    write_in_progress_ = false;
    next_download_poll_timeout_ms_ = 0u;
    last_download_status_ = DFUStatusCode::OK;
    manifest_pending_ = false;
    manifest_in_progress_ = false;
    last_manifest_status_ = DFUStatusCode::OK;
    upload_session_started_ = false;
    upload_offset_ = 0u;
    upload_expected_block_num_ = 0u;
    upload_image_size_ = 0u;
  }

  bool HasPendingWrite() const { return write_pending_ || write_in_progress_; }

  bool HasPendingManifest() const { return manifest_pending_ || manifest_in_progress_; }

  uint32_t ComputeWritePollTimeout(size_t offset, size_t len) const
  {
    const size_t first_block = offset / erase_block_size_;
    const size_t last_block = (offset + len - 1u) / erase_block_size_;
    for (size_t block = first_block; block <= last_block && block < erase_block_count_;
         ++block)
    {
      if (erased_blocks_[block] == 0u)
      {
        return 25u;
      }
    }
    return 10u;
  }

  void ProcessPendingWrite()
  {
    write_in_progress_ = true;
    write_pending_ = false;

    if (!EnsureBlocksErased(pending_write_offset_, pending_write_len_))
    {
      last_download_status_ = DFUStatusCode::ERR_ERASE;
      write_in_progress_ = false;
      return;
    }
    if (flash_.Write(image_offset_ + pending_write_offset_,
                     {write_buffer_, pending_write_len_}) != ErrorCode::OK)
    {
      last_download_status_ = DFUStatusCode::ERR_PROG;
      write_in_progress_ = false;
      return;
    }

    received_bytes_ = pending_write_offset_ + pending_write_len_;
    expected_block_num_ = static_cast<uint16_t>(pending_write_block_num_ + 1u);
    last_download_status_ = DFUStatusCode::OK;
    write_in_progress_ = false;
  }

  void ProcessPendingManifest()
  {
    manifest_in_progress_ = true;
    manifest_pending_ = false;

    const size_t payload_limit = PayloadLimit();
    if (received_bytes_ == 0u || received_bytes_ > payload_limit)
    {
      last_manifest_status_ = DFUStatusCode::ERR_ADDRESS;
      manifest_in_progress_ = false;
      return;
    }

    const uint32_t crc32 = ComputeImageCrc32(received_bytes_);
    if (!WriteSeal(received_bytes_, crc32))
    {
      last_manifest_status_ = DFUStatusCode::ERR_VERIFY;
      manifest_in_progress_ = false;
      return;
    }

    stored_image_size_ = received_bytes_;
    image_ready_ = true;
    launch_requested_ = false;
    last_manifest_status_ = DFUStatusCode::OK;
    manifest_in_progress_ = false;
    download_session_started_ = false;
    received_bytes_ = 0u;
    expected_block_num_ = 0u;
    upload_session_started_ = false;
    upload_offset_ = 0u;
    upload_expected_block_num_ = 0u;
    upload_image_size_ = 0u;
  }

  uint32_t ComputeImageCrc32(size_t image_size)
  {
    if (!LibXR::CRC32::inited_)
    {
      LibXR::CRC32::GenerateTable();
    }
    uint32_t crc = 0xFFFFFFFFu;
    size_t offset = 0u;
    while (offset < image_size)
    {
      size_t chunk = image_size - offset;
      if (chunk > sizeof(crc_buffer_))
      {
        chunk = sizeof(crc_buffer_);
      }
      if (flash_.Read(image_offset_ + offset, {crc_buffer_, chunk}) != ErrorCode::OK)
      {
        return 0u;
      }
      const uint8_t* buf = crc_buffer_;
      size_t remain = chunk;
      while (remain-- > 0u)
      {
        crc = LibXR::CRC32::tab_[(crc ^ *buf++) & 0xFFu] ^ (crc >> 8);
      }
      offset += chunk;
    }
    return crc;
  }

  bool ReadSeal(SealRecord& seal)
  {
    return flash_.Read(image_offset_ + seal_offset_, {reinterpret_cast<uint8_t*>(&seal),
                                                      sizeof(seal)}) == ErrorCode::OK;
  }

  bool WriteSeal(size_t image_size, uint32_t crc32)
  {
    if (seal_storage_ == nullptr)
    {
      return false;
    }
    std::memset(seal_storage_, 0xFF, seal_storage_size_);
    auto* seal = reinterpret_cast<SealRecord*>(seal_storage_);
    seal->magic = kSealMagic;
    seal->image_size = static_cast<uint32_t>(image_size);
    seal->crc32 = crc32;
    seal->crc32_inv = ~crc32;

    if (flash_.Erase(image_offset_ + seal_offset_, erase_block_size_) != ErrorCode::OK)
    {
      return false;
    }
    return flash_.Write(image_offset_ + seal_offset_,
                        {seal_storage_, seal_storage_size_}) == ErrorCode::OK;
  }

  bool ProbeStoredImage(size_t* out_image_size)
  {
    SealRecord seal = {};
    if (!ReadSeal(seal))
    {
      return false;
    }
    if (seal.magic != kSealMagic)
    {
      return false;
    }
    if ((seal.crc32 ^ seal.crc32_inv) != 0xFFFFFFFFu)
    {
      return false;
    }
    const size_t image_size = static_cast<size_t>(seal.image_size);
    if (image_size == 0u || image_size > PayloadLimit())
    {
      return false;
    }
    const uint32_t actual_crc = ComputeImageCrc32(image_size);
    if (actual_crc != seal.crc32)
    {
      return false;
    }
    if (out_image_size != nullptr)
    {
      *out_image_size = image_size;
    }
    return true;
  }

  bool EnsureBlocksErased(size_t offset, size_t len)
  {
    if (len == 0u)
    {
      return true;
    }

    const size_t first_block = offset / erase_block_size_;
    const size_t last_block = (offset + len - 1u) / erase_block_size_;
    if (last_block >= erase_block_count_)
    {
      return false;
    }

    for (size_t block = first_block; block <= last_block; ++block)
    {
      if (erased_blocks_[block] != 0u)
      {
        continue;
      }
      const size_t block_offset = block * erase_block_size_;
      if (flash_.Erase(image_offset_ + block_offset, erase_block_size_) != ErrorCode::OK)
      {
        return false;
      }
      erased_blocks_[block] = 1u;
    }
    return true;
  }

  Flash& flash_;
  size_t image_offset_ = 0u;
  size_t image_size_limit_ = 0u;
  size_t seal_offset_ = 0u;
  JumpCallback jump_to_app_ = nullptr;
  void* jump_app_ctx_ = nullptr;
  bool autorun_ = true;
  bool launch_requested_ = false;
  bool image_ready_ = false;
  size_t stored_image_size_ = 0u;
  size_t erase_block_size_ = 1u;
  size_t erase_block_count_ = 0u;
  uint8_t* erased_blocks_ = nullptr;
  size_t seal_storage_size_ = 0u;
  uint8_t* seal_storage_ = nullptr;
  size_t transfer_size_ = 0u;
  uint8_t* write_buffer_ = nullptr;
  uint8_t crc_buffer_[256] = {};

  bool download_session_started_ = false;
  size_t received_bytes_ = 0u;
  uint16_t expected_block_num_ = 0u;
  size_t pending_write_offset_ = 0u;
  size_t pending_write_len_ = 0u;
  uint16_t pending_write_block_num_ = 0u;
  bool write_pending_ = false;
  bool write_in_progress_ = false;
  uint32_t next_download_poll_timeout_ms_ = 0u;
  DFUStatusCode last_download_status_ = DFUStatusCode::OK;
  bool manifest_pending_ = false;
  bool manifest_in_progress_ = false;
  uint32_t manifest_poll_timeout_ms_ = 50u;
  DFUStatusCode last_manifest_status_ = DFUStatusCode::OK;

  bool upload_session_started_ = false;
  size_t upload_offset_ = 0u;
  uint16_t upload_expected_block_num_ = 0u;
  size_t upload_image_size_ = 0u;
};

template <typename Backend, size_t MAX_TRANSFER_SIZE = 4096u>
class DFUClass : public DeviceClass
{
  static_assert(MAX_TRANSFER_SIZE > 0u, "DFU transfer size must be non-zero.");
  static_assert(MAX_TRANSFER_SIZE <= 0xFFFFu,
                "DFU transfer size must fit in wTransferSize.");

  static constexpr uint8_t kInterfaceClass = 0xFEu;
  static constexpr uint8_t kInterfaceSubClass = 0x01u;
  static constexpr uint8_t kInterfaceProtocol = 0x02u;
  static constexpr uint16_t kDfuVersion = 0x0110u;

  static constexpr uint8_t kAttrCanDownload = 0x01u;
  static constexpr uint8_t kAttrCanUpload = 0x02u;
  static constexpr uint8_t kAttrManifestationTolerant = 0x04u;
  static constexpr uint8_t kAttrWillDetach = 0x08u;

 public:
#pragma pack(push, 1)
  struct FunctionalDescriptor
  {
    uint8_t bLength = 9;
    uint8_t bDescriptorType = 0x21;
    uint8_t bmAttributes = 0;
    uint16_t wDetachTimeOut = 0;
    uint16_t wTransferSize = 0;
    uint16_t bcdDFUVersion = kDfuVersion;
  };

  struct StatusResponse
  {
    uint8_t bStatus = 0;
    uint8_t bwPollTimeout[3] = {0, 0, 0};
    uint8_t bState = 0;
    uint8_t iString = 0;

    void SetPollTimeout(uint32_t timeout_ms)
    {
      bwPollTimeout[0] = static_cast<uint8_t>(timeout_ms & 0xFFu);
      bwPollTimeout[1] = static_cast<uint8_t>((timeout_ms >> 8) & 0xFFu);
      bwPollTimeout[2] = static_cast<uint8_t>((timeout_ms >> 16) & 0xFFu);
    }
  };

  struct DescriptorBlock
  {
    ConfigDescriptorItem::InterfaceDescriptor interface_desc = {
        9,
        static_cast<uint8_t>(DescriptorType::INTERFACE),
        0,
        0,
        0,
        kInterfaceClass,
        kInterfaceSubClass,
        kInterfaceProtocol,
        0};
    FunctionalDescriptor func_desc = {};
  };
#pragma pack(pop)

  static_assert(sizeof(FunctionalDescriptor) == 9,
                "DFU functional descriptor size mismatch.");
  static_assert(sizeof(StatusResponse) == 6, "DFU status response size mismatch.");

 public:
  static constexpr const char* kInterfaceStringDefault = "XRUSB DFU";

  /**
   * Backend contract:
   * - `DFUCapabilities GetDfuCapabilities() const`
   * - `ErrorCode DfuSetAlternate(uint8_t alt)`
   * - `void DfuAbort(uint8_t alt)`
   * - `void DfuClearStatus(uint8_t alt)`
   * - `DFUStatusCode DfuDownload(uint8_t alt, uint16_t block_num, ConstRawData data,
   *                              uint32_t& poll_timeout_ms)`
   * - `DFUStatusCode DfuGetDownloadStatus(uint8_t alt, bool& busy,
   *                                       uint32_t& poll_timeout_ms)`
   * - `size_t DfuUpload(uint8_t alt, uint16_t block_num, RawData data,
   *                     DFUStatusCode& status, uint32_t& poll_timeout_ms)`
   * - `DFUStatusCode DfuManifest(uint8_t alt, uint32_t& poll_timeout_ms)`
   * - `DFUStatusCode DfuGetManifestStatus(uint8_t alt, bool& busy,
   *                                       uint32_t& poll_timeout_ms)`
   */
  explicit DFUClass(Backend& backend,
                    const char* interface_string = kInterfaceStringDefault)
      : backend_(backend), interface_string_(interface_string)
  {
  }

  const char* GetInterfaceString(size_t local_interface_index) const override
  {
    return (local_interface_index == 0u) ? interface_string_ : nullptr;
  }

 protected:
  void BindEndpoints(EndpointPool&, uint8_t start_itf_num, bool) override
  {
    interface_num_ = start_itf_num;
    current_alt_setting_ = 0u;

    caps_ = backend_.GetDfuCapabilities();
    if (caps_.transfer_size == 0u || caps_.transfer_size > MAX_TRANSFER_SIZE)
    {
      caps_.transfer_size = static_cast<uint16_t>(MAX_TRANSFER_SIZE);
    }

    desc_block_.interface_desc.bInterfaceNumber = interface_num_;
    desc_block_.interface_desc.bAlternateSetting = 0u;
    desc_block_.interface_desc.iInterface = GetInterfaceStringIndex(0u);
    desc_block_.func_desc.bmAttributes = BuildAttributeBitmap(caps_);
    desc_block_.func_desc.wDetachTimeOut = caps_.detach_timeout_ms;
    desc_block_.func_desc.wTransferSize = caps_.transfer_size;
    desc_block_.func_desc.bcdDFUVersion = kDfuVersion;

    SetData(RawData{reinterpret_cast<uint8_t*>(&desc_block_), sizeof(desc_block_)});

    ResetProtocolState();
    inited_ = true;
    (void)backend_.DfuSetAlternate(current_alt_setting_);
  }

  void UnbindEndpoints(EndpointPool&, bool) override
  {
    if (inited_)
    {
      backend_.DfuAbort(current_alt_setting_);
    }
    inited_ = false;
    ResetProtocolState();
  }

  size_t GetInterfaceCount() override { return 1u; }
  bool HasIAD() override { return false; }
  size_t GetMaxConfigSize() override { return sizeof(desc_block_); }
  ErrorCode WriteDeviceDescriptor(DeviceDescriptor& header) override
  {
    header.data_.bDeviceClass = DeviceDescriptor::ClassID::APPLICATION_SPECIFIC;
    header.data_.bDeviceSubClass = kInterfaceSubClass;
    header.data_.bDeviceProtocol = kInterfaceProtocol;
    return ErrorCode::OK;
  }

  ErrorCode SetAltSetting(uint8_t itf, uint8_t alt) override
  {
    if (itf != interface_num_)
    {
      return ErrorCode::NOT_FOUND;
    }
    if (alt != 0u)
    {
      return ErrorCode::NOT_SUPPORT;
    }
    if (state_ == DFUState::DFU_DNBUSY || state_ == DFUState::DFU_MANIFEST ||
        state_ == DFUState::DFU_MANIFEST_WAIT_RESET)
    {
      return ErrorCode::BUSY;
    }

    auto ec = backend_.DfuSetAlternate(alt);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }

    backend_.DfuAbort(current_alt_setting_);
    current_alt_setting_ = alt;
    ClearErrorState();
    return ErrorCode::OK;
  }

  ErrorCode GetAltSetting(uint8_t itf, uint8_t& alt) override
  {
    if (itf != interface_num_)
    {
      return ErrorCode::NOT_FOUND;
    }
    alt = current_alt_setting_;
    return ErrorCode::OK;
  }

  ErrorCode OnGetDescriptor(bool, uint8_t, uint16_t wValue, uint16_t,
                            ConstRawData& out_data) override
  {
    const uint8_t desc_type = static_cast<uint8_t>((wValue >> 8) & 0xFFu);
    if (desc_type != desc_block_.func_desc.bDescriptorType)
    {
      return ErrorCode::NOT_SUPPORT;
    }

    out_data = {reinterpret_cast<const uint8_t*>(&desc_block_.func_desc),
                sizeof(desc_block_.func_desc)};
    return ErrorCode::OK;
  }

  ErrorCode OnClassRequest(bool, uint8_t bRequest, uint16_t wValue, uint16_t wLength,
                           uint16_t wIndex, ControlTransferResult& result) override
  {
    if (!inited_)
    {
      return ErrorCode::INIT_ERR;
    }
    if ((wIndex & 0xFFu) != interface_num_)
    {
      return ErrorCode::NOT_FOUND;
    }

    switch (static_cast<DFURequest>(bRequest))
    {
      case DFURequest::DETACH:
        return HandleDetach(result);

      case DFURequest::DNLOAD:
        return HandleDnload(wValue, wLength, result);

      case DFURequest::UPLOAD:
        return HandleUpload(wValue, wLength, result);

      case DFURequest::GETSTATUS:
        return HandleGetStatus(result);

      case DFURequest::CLRSTATUS:
        return HandleClearStatus(result);

      case DFURequest::GETSTATE:
        return HandleGetState(result);

      case DFURequest::ABORT:
        return HandleAbort(result);

      default:
        return ProtocolStall(DFUStatusCode::ERR_STALLEDPKT);
    }
  }

  ErrorCode OnClassData(bool, uint8_t bRequest, LibXR::ConstRawData& data) override
  {
    const auto request = static_cast<DFURequest>(bRequest);
    if (request == DFURequest::GETSTATUS)
    {
      UNUSED(data);
      if (state_ == DFUState::DFU_MANIFEST_WAIT_RESET)
      {
        backend_.DfuCommitManifestWaitReset(current_alt_setting_);
      }
      return ErrorCode::OK;
    }

    if (request != DFURequest::DNLOAD)
    {
      return ErrorCode::NOT_SUPPORT;
    }

    if (pending_dnload_length_ == 0u)
    {
      return ErrorCode::OK;
    }

    uint32_t poll_timeout_ms = 0u;
    auto status = backend_.DfuDownload(current_alt_setting_, pending_block_num_, data,
                                       poll_timeout_ms);
    pending_dnload_length_ = 0u;
    poll_timeout_ms_ = poll_timeout_ms;

    if (status == DFUStatusCode::OK)
    {
      state_ = DFUState::DFU_DNLOAD_SYNC;
      status_ = DFUStatusCode::OK;
      download_started_ = true;
    }
    else
    {
      EnterErrorState(status);
    }
    return ErrorCode::OK;
  }

 private:
  static constexpr uint8_t BuildAttributeBitmap(const DFUCapabilities& caps)
  {
    return static_cast<uint8_t>(
        (caps.can_download ? kAttrCanDownload : 0u) |
        (caps.can_upload ? kAttrCanUpload : 0u) |
        (caps.manifestation_tolerant ? kAttrManifestationTolerant : 0u) |
        (caps.will_detach ? kAttrWillDetach : 0u));
  }

  ErrorCode HandleDetach(ControlTransferResult&)
  {
    return ProtocolStall(DFUStatusCode::ERR_STALLEDPKT);
  }

  ErrorCode HandleDnload(uint16_t block_num, uint16_t wLength,
                         ControlTransferResult& result)
  {
    if (!caps_.can_download)
    {
      return ProtocolStall(DFUStatusCode::ERR_STALLEDPKT);
    }
    if (!(state_ == DFUState::DFU_IDLE || state_ == DFUState::DFU_DNLOAD_IDLE))
    {
      return ProtocolStall(DFUStatusCode::ERR_STALLEDPKT);
    }
    if (wLength > caps_.transfer_size)
    {
      return ProtocolStall(DFUStatusCode::ERR_USBR);
    }

    if (wLength == 0u)
    {
      pending_dnload_length_ = 0u;
      pending_block_num_ = block_num;
      if (!download_started_)
      {
        EnterErrorState(DFUStatusCode::ERR_NOTDONE);
      }
      else
      {
        state_ = DFUState::DFU_MANIFEST_SYNC;
        status_ = DFUStatusCode::OK;
        poll_timeout_ms_ = 0u;
      }
      result.SendStatusInZLP() = true;
      return ErrorCode::OK;
    }

    pending_block_num_ = block_num;
    pending_dnload_length_ = wLength;
    result.OutData() = {transfer_buffer_, wLength};
    return ErrorCode::OK;
  }

  ErrorCode HandleUpload(uint16_t block_num, uint16_t wLength,
                         ControlTransferResult& result)
  {
    if (!caps_.can_upload)
    {
      return ProtocolStall(DFUStatusCode::ERR_STALLEDPKT);
    }
    if (!(state_ == DFUState::DFU_IDLE || state_ == DFUState::DFU_UPLOAD_IDLE))
    {
      return ProtocolStall(DFUStatusCode::ERR_STALLEDPKT);
    }
    if (wLength == 0u)
    {
      return ProtocolStall(DFUStatusCode::ERR_USBR);
    }

    size_t req_size = wLength;
    if (req_size > caps_.transfer_size)
    {
      req_size = caps_.transfer_size;
    }

    DFUStatusCode op_status = DFUStatusCode::OK;
    uint32_t poll_timeout_ms = 0u;
    const size_t read_size =
        backend_.DfuUpload(current_alt_setting_, block_num, {transfer_buffer_, req_size},
                           op_status, poll_timeout_ms);

    poll_timeout_ms_ = poll_timeout_ms;
    if (op_status != DFUStatusCode::OK)
    {
      return ProtocolStall(op_status);
    }
    if (read_size > req_size)
    {
      return ProtocolStall(DFUStatusCode::ERR_USBR);
    }

    status_ = DFUStatusCode::OK;
    state_ = (read_size < req_size) ? DFUState::DFU_IDLE : DFUState::DFU_UPLOAD_IDLE;
    result.InData() = {transfer_buffer_, read_size};
    return ErrorCode::OK;
  }

  ErrorCode HandleGetStatus(ControlTransferResult& result)
  {
    AdvanceStateForStatusRead();
    status_response_.bStatus = static_cast<uint8_t>(status_);
    status_response_.SetPollTimeout(poll_timeout_ms_);
    status_response_.bState = static_cast<uint8_t>(state_);
    status_response_.iString = 0u;
    result.InData() = {reinterpret_cast<const uint8_t*>(&status_response_),
                       sizeof(status_response_)};
    return ErrorCode::OK;
  }

  ErrorCode HandleClearStatus(ControlTransferResult& result)
  {
    if (state_ != DFUState::DFU_ERROR)
    {
      return ProtocolStall(DFUStatusCode::ERR_STALLEDPKT);
    }
    backend_.DfuClearStatus(current_alt_setting_);
    ClearErrorState();
    result.SendStatusInZLP() = true;
    return ErrorCode::OK;
  }

  ErrorCode HandleGetState(ControlTransferResult& result)
  {
    state_response_ = static_cast<uint8_t>(state_);
    result.InData() = {&state_response_, sizeof(state_response_)};
    return ErrorCode::OK;
  }

  ErrorCode HandleAbort(ControlTransferResult& result)
  {
    switch (state_)
    {
      case DFUState::DFU_IDLE:
      case DFUState::DFU_DNLOAD_SYNC:
      case DFUState::DFU_DNLOAD_IDLE:
      case DFUState::DFU_UPLOAD_IDLE:
      case DFUState::DFU_MANIFEST_SYNC:
        backend_.DfuAbort(current_alt_setting_);
        ClearErrorState();
        result.SendStatusInZLP() = true;
        return ErrorCode::OK;

      default:
        return ProtocolStall(DFUStatusCode::ERR_STALLEDPKT);
    }
  }

  void AdvanceStateForStatusRead()
  {
    switch (state_)
    {
      case DFUState::DFU_DNLOAD_SYNC:
        RefreshDownloadStatus();
        break;

      case DFUState::DFU_MANIFEST_SYNC:
      {
        if (status_ != DFUStatusCode::OK)
        {
          state_ = DFUState::DFU_ERROR;
          break;
        }

        uint32_t poll_timeout_ms = 0u;
        auto manifest_status =
            backend_.DfuManifest(current_alt_setting_, poll_timeout_ms);
        poll_timeout_ms_ = poll_timeout_ms;
        if (manifest_status == DFUStatusCode::OK)
        {
          status_ = DFUStatusCode::OK;
          state_ = DFUState::DFU_MANIFEST;
        }
        else
        {
          EnterErrorState(manifest_status);
        }
        break;
      }

      case DFUState::DFU_DNBUSY:
        RefreshDownloadStatus();
        break;

      case DFUState::DFU_MANIFEST:
        RefreshManifestStatus();
        break;

      default:
        break;
    }
  }

  void RefreshDownloadStatus()
  {
    if (status_ != DFUStatusCode::OK)
    {
      state_ = DFUState::DFU_ERROR;
      return;
    }

    bool busy = false;
    uint32_t poll_timeout_ms = 0u;
    const auto download_status =
        backend_.DfuGetDownloadStatus(current_alt_setting_, busy, poll_timeout_ms);
    poll_timeout_ms_ = poll_timeout_ms;
    if (download_status != DFUStatusCode::OK)
    {
      EnterErrorState(download_status);
      return;
    }

    state_ = busy ? DFUState::DFU_DNBUSY : DFUState::DFU_DNLOAD_IDLE;
  }

  void RefreshManifestStatus()
  {
    if (status_ != DFUStatusCode::OK)
    {
      state_ = DFUState::DFU_ERROR;
      return;
    }

    bool busy = false;
    uint32_t poll_timeout_ms = 0u;
    const auto manifest_status =
        backend_.DfuGetManifestStatus(current_alt_setting_, busy, poll_timeout_ms);
    poll_timeout_ms_ = poll_timeout_ms;
    if (manifest_status != DFUStatusCode::OK)
    {
      EnterErrorState(manifest_status);
      return;
    }

    if (busy)
    {
      state_ = DFUState::DFU_MANIFEST;
      return;
    }

    download_started_ = false;
    state_ = caps_.manifestation_tolerant ? DFUState::DFU_IDLE
                                          : DFUState::DFU_MANIFEST_WAIT_RESET;
  }

  void ResetProtocolState()
  {
    pending_block_num_ = 0u;
    pending_dnload_length_ = 0u;
    poll_timeout_ms_ = 0u;
    current_alt_setting_ = 0u;
    download_started_ = false;
    ClearErrorState();
  }

  void ClearErrorState()
  {
    status_ = DFUStatusCode::OK;
    state_ = DFUState::DFU_IDLE;
    poll_timeout_ms_ = 0u;
  }

  void EnterErrorState(DFUStatusCode status)
  {
    status_ = status;
    state_ = DFUState::DFU_ERROR;
    poll_timeout_ms_ = 0u;
  }

  ErrorCode ProtocolStall(DFUStatusCode status)
  {
    EnterErrorState(status);
    return ErrorCode::ARG_ERR;
  }

 private:
  Backend& backend_;
  const char* interface_string_ = nullptr;
  DFUCapabilities caps_ = {};
  DescriptorBlock desc_block_ = {};
  StatusResponse status_response_ = {};
  uint8_t state_response_ = 0u;
  uint8_t transfer_buffer_[MAX_TRANSFER_SIZE] = {};
  uint8_t interface_num_ = 0u;
  uint8_t current_alt_setting_ = 0u;
  bool inited_ = false;
  bool download_started_ = false;
  uint16_t pending_block_num_ = 0u;
  uint16_t pending_dnload_length_ = 0u;
  uint32_t poll_timeout_ms_ = 0u;
  DFUState state_ = DFUState::DFU_IDLE;
  DFUStatusCode status_ = DFUStatusCode::OK;
};

class DfuFirmwareClassStorage
{
 protected:
  using JumpCallback = DfuFirmwareBackendDetail::JumpCallback;

  DfuFirmwareClassStorage(Flash& flash, size_t image_base, size_t image_limit,
                          size_t seal_offset, JumpCallback jump_to_app,
                          void* jump_app_ctx = nullptr, bool autorun = true)
      : backend_(flash, image_base, image_limit, seal_offset, jump_to_app, jump_app_ctx,
                 autorun),
        image_base_(image_base),
        image_limit_(image_limit),
        seal_offset_(seal_offset)
  {
  }

  DfuFirmwareBackendDetail backend_;
  size_t image_base_ = 0u;
  size_t image_limit_ = 0u;
  size_t seal_offset_ = 0u;
};

class DfuFirmwareClass : private DfuFirmwareClassStorage,
                         public DFUClass<DfuFirmwareBackendDetail, 4096u>
{
  using Storage = DfuFirmwareClassStorage;
  using Base = DFUClass<DfuFirmwareBackendDetail, 4096u>;

 public:
  using JumpCallback = DfuFirmwareBackendDetail::JumpCallback;

  DfuFirmwareClass(Flash& flash, size_t image_base, size_t image_limit,
                   size_t seal_offset, JumpCallback jump_to_app,
                   void* jump_app_ctx = nullptr, bool autorun = true,
                   const char* interface_string = Base::kInterfaceStringDefault)
      : Storage(flash, image_base, image_limit, seal_offset, jump_to_app, jump_app_ctx,
                autorun),
        Base(Storage::backend_, interface_string)
  {
  }

  void Process() { Storage::backend_.Process(); }
  void RequestRunApp() { Storage::backend_.RequestRunApp(); }

  bool TryConsumeAppLaunch(uint32_t now_ms)
  {
    return Storage::backend_.TryConsumeAppLaunch(now_ms);
  }

  bool HasPendingWork() const { return Storage::backend_.HasPendingWork(); }
  bool HasValidImage() const { return Storage::backend_.HasValidImage(); }
  size_t ImageSize() const { return Storage::backend_.ImageSize(); }
  size_t ImageBase() const { return Storage::image_base_; }
  size_t ImageLimit() const { return Storage::image_limit_; }
  size_t SealOffset() const { return Storage::seal_offset_; }
};

}  // namespace LibXR::USB
