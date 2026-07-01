#include "hpm_spi.hpp"

#if LIBXR_HPM_SPI_SUPPORTED

#include <algorithm>
#include <cstdint>

#if __has_include("board.h")
#include "board.h"
#endif

#if LIBXR_HPM_SPI_HAS_DMA_MGR && __has_include("hpm_l1c_drv.h")
#include "hpm_l1c_drv.h"
#define LIBXR_HPM_SPI_HAS_L1C 1
#else
#define LIBXR_HPM_SPI_HAS_L1C 0
#endif

extern "C"
{
  ATTR_WEAK void board_init_spi_pins(SPI_Type* ptr);
  ATTR_WEAK uint32_t board_init_spi_clock(SPI_Type* ptr);
}

using namespace LibXR;

namespace
{
#if defined(SPI_SOC_TRANSFER_COUNT_MAX)
constexpr uint32_t kHpmSpiTransferCountMax = SPI_SOC_TRANSFER_COUNT_MAX;
#else
constexpr uint32_t kHpmSpiTransferCountMax = UINT32_MAX;
#endif
constexpr uint16_t kSpiRegisterAddressMask = 0x007FU;

#if LIBXR_HPM_SPI_HAS_DMA_MGR
static_assert(sizeof(uintptr_t) <= sizeof(uint32_t),
              "HPM SPI DMA helper assumes a 32-bit address space.");

#if LIBXR_HPM_SPI_HAS_L1C
bool ResolveDCacheRange(const void* addr, uint32_t size, uint32_t& start,
                        uint32_t& aligned_size)
{
  if (addr == nullptr || size == 0U)
  {
    start = 0U;
    aligned_size = 0U;
    return false;
  }

  const uint64_t line_size = HPM_L1C_CACHELINE_SIZE;
  const uint64_t address = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(addr));
  const uint64_t end = address + static_cast<uint64_t>(size);
  constexpr uint64_t kAddressSpaceSize = static_cast<uint64_t>(UINT32_MAX) + 1ULL;
  if (line_size == 0U || end > kAddressSpaceSize)
  {
    start = 0U;
    aligned_size = 0U;
    return false;
  }

  const uint64_t aligned_start = address - (address % line_size);
  const uint64_t aligned_end = ((end + line_size - 1U) / line_size) * line_size;
  const uint64_t range_size = aligned_end - aligned_start;
  if (aligned_end > kAddressSpaceSize || range_size > UINT32_MAX)
  {
    start = 0U;
    aligned_size = 0U;
    return false;
  }

  start = static_cast<uint32_t>(aligned_start);
  aligned_size = static_cast<uint32_t>(range_size);
  return aligned_size > 0U;
}
#endif

void FlushDCacheIfNeeded(const void* addr, uint32_t size)
{
#if LIBXR_HPM_SPI_HAS_L1C
  if (addr != nullptr && size > 0U && l1c_dc_is_enabled())
  {
    uint32_t start = 0U;
    uint32_t aligned_size = 0U;
    if (ResolveDCacheRange(addr, size, start, aligned_size))
    {
      l1c_dc_flush(start, aligned_size);
    }
  }
#else
  UNUSED(addr);
  UNUSED(size);
#endif
}

void InvalidateDCacheIfNeeded(const void* addr, uint32_t size)
{
#if LIBXR_HPM_SPI_HAS_L1C
  if (addr != nullptr && size > 0U && l1c_dc_is_enabled())
  {
    uint32_t start = 0U;
    uint32_t aligned_size = 0U;
    if (ResolveDCacheRange(addr, size, start, aligned_size))
    {
      l1c_dc_invalidate(start, aligned_size);
    }
  }
#else
  UNUSED(addr);
  UNUSED(size);
#endif
}
#endif
}  // namespace

HPMSPI::HPMSPI(SPI_Type* spi, clock_name_t clock, RawData rx_buffer, RawData tx_buffer,
               bool auto_board_init, SPI::Configuration config, ChipSelect cs)
    : SPI(rx_buffer, tx_buffer),
      spi_(spi),
      clock_(clock),
      rx_buffer_capacity_(rx_buffer.size_),
      tx_buffer_capacity_(tx_buffer.size_),
      auto_board_init_(auto_board_init),
      cs_(cs)
{
  ASSERT(spi_ != nullptr);
  ASSERT(rx_buffer.addr_ != nullptr);
  ASSERT(tx_buffer.addr_ != nullptr);
  ASSERT(rx_buffer.size_ > 0);
  ASSERT(tx_buffer.size_ > 0);
#if defined(HPM_IP_FEATURE_SPI_CS_SELECT) && (HPM_IP_FEATURE_SPI_CS_SELECT == 1)
  ASSERT(ConvertChipSelect(cs_) != 0U);
#endif

  if (auto_board_init_)
  {
    if (board_init_spi_pins != nullptr)
    {
      board_init_spi_pins(spi_);
    }
    if (board_init_spi_clock != nullptr)
    {
      source_clock_hz_ = board_init_spi_clock(spi_);
    }
  }

  if (source_clock_hz_ == 0)
  {
    clock_add_to_group(clock_, 0);
    source_clock_hz_ = clock_get_frequency(clock_);
  }

  ASSERT(source_clock_hz_ != 0);
  const ErrorCode ans = SetConfig(config);
  ASSERT(ans == ErrorCode::OK);
}

ErrorCode HPMSPI::ConvertStatus(LibXRHpmSpiStatusType status)
{
  switch (status)
  {
    case status_success:
      return ErrorCode::OK;
    case status_timeout:
      return ErrorCode::TIMEOUT;
    case status_invalid_argument:
      return ErrorCode::ARG_ERR;
    case status_spi_master_busy:
      return ErrorCode::BUSY;
    default:
      return ErrorCode::FAILED;
  }
}

uint8_t HPMSPI::ConvertChipSelect(ChipSelect cs)
{
  switch (cs)
  {
#if defined(HPM_IP_FEATURE_SPI_CS_SELECT) && (HPM_IP_FEATURE_SPI_CS_SELECT == 1)
    case ChipSelect::CS0:
      return spi_cs_0;
    case ChipSelect::CS1:
      return spi_cs_1;
    case ChipSelect::CS2:
      return spi_cs_2;
    case ChipSelect::CS3:
      return spi_cs_3;
#else
    case ChipSelect::CS0:
    case ChipSelect::CS1:
    case ChipSelect::CS2:
    case ChipSelect::CS3:
      return 0U;
#endif
    default:
      return 0U;
  }
}

bool HPMSPI::ShouldRecover(LibXRHpmSpiStatusType status)
{
  switch (status)
  {
    case status_timeout:
      return true;
    default:
      return false;
  }
}

spi_sclk_idle_state_t HPMSPI::ConvertPolarity(ClockPolarity polarity)
{
  return polarity == ClockPolarity::HIGH ? spi_sclk_high_idle : spi_sclk_low_idle;
}

spi_sclk_sampling_clk_edges_t HPMSPI::ConvertPhase(ClockPhase phase)
{
  return phase == ClockPhase::EDGE_2 ? spi_sclk_sampling_even_clk_edges
                                     : spi_sclk_sampling_odd_clk_edges;
}

ErrorCode HPMSPI::ValidateConfiguration(const Configuration& config) const
{
  const uint32_t div = SPI::PrescalerToDiv(config.prescaler);
  if (div == 0U)
  {
    return ErrorCode::ARG_ERR;
  }

  if (div > SPI::PrescalerToDiv(Prescaler::DIV_256))
  {
    return ErrorCode::NOT_SUPPORT;
  }

  // HPM master timing divider requires:
  // - div == 1: encoded as max speed path
  // - div in [2, 510] and even
  if (div != 1U && ((div & 0x1U) != 0U || div > 510U))
  {
    return ErrorCode::NOT_SUPPORT;
  }

  if (config.double_buffer &&
      ((rx_buffer_capacity_ < 2U) || (tx_buffer_capacity_ < 2U) ||
       ((rx_buffer_capacity_ / 2U) == 0U) || ((tx_buffer_capacity_ / 2U) == 0U)))
  {
    return ErrorCode::SIZE_ERR;
  }

  return ErrorCode::OK;
}

ErrorCode HPMSPI::EnsureClockReady()
{
  if (source_clock_hz_ != 0U)
  {
    return ErrorCode::OK;
  }

  source_clock_hz_ = clock_get_frequency(clock_);
  return (source_clock_hz_ != 0U) ? ErrorCode::OK : ErrorCode::INIT_ERR;
}

void HPMSPI::ApplyFormat(const Configuration& config)
{
  spi_format_config_t format{};
  spi_master_get_default_format_config(&format);
  format.common_config.data_len_in_bits = 8;
  format.common_config.data_merge = false;
  format.common_config.mosi_bidir = false;
  format.common_config.lsb = false;
  format.common_config.mode = spi_master_mode;
  format.common_config.cpol = ConvertPolarity(config.clock_polarity);
  format.common_config.cpha = ConvertPhase(config.clock_phase);
  spi_format_init(spi_, &format);
}

spi_control_config_t HPMSPI::MakeControlConfig(spi_trans_mode_t mode) const
{
  spi_control_config_t control{};
  spi_master_get_default_control_config(&control);
  control.common_config.trans_mode = mode;
  control.common_config.data_phase_fmt = spi_single_io_mode;
#if defined(HPM_IP_FEATURE_SPI_CS_SELECT) && (HPM_IP_FEATURE_SPI_CS_SELECT == 1)
  control.common_config.cs_index = ConvertChipSelect(cs_);
#endif
  return control;
}

void HPMSPI::ApplyChipSelect() const
{
#if defined(HPM_IP_FEATURE_SPI_CS_SELECT) && (HPM_IP_FEATURE_SPI_CS_SELECT == 1)
  if (spi_ != nullptr)
  {
    spi_master_enable_cs_select(spi_,
                                static_cast<spi_cs_index_t>(ConvertChipSelect(cs_)));
  }
#endif
}

void HPMSPI::RecoverController()
{
  if (spi_ == nullptr)
  {
    return;
  }

  spi_reset(spi_);
  if (spi_poll_reset_complete(spi_, spi_reset_spi, 5000U) != status_success)
  {
    configured_ = false;
    return;
  }

  if (configured_)
  {
    const Configuration config = GetConfig();
    const ErrorCode ready_ans = EnsureClockReady();
    if (ready_ans != ErrorCode::OK)
    {
      configured_ = false;
      return;
    }

    if (ApplyTiming(config.prescaler) != ErrorCode::OK)
    {
      configured_ = false;
      return;
    }

    ApplyFormat(config);
  }
}

ErrorCode HPMSPI::ApplyTiming(Prescaler prescaler)
{
  const uint32_t div = SPI::PrescalerToDiv(prescaler);
  if (div == 0U)
  {
    return ErrorCode::ARG_ERR;
  }

  if (div != 1U && ((div & 0x1U) != 0U || div > 510U))
  {
    return ErrorCode::NOT_SUPPORT;
  }

  const ErrorCode clock_ans = EnsureClockReady();
  if (clock_ans != ErrorCode::OK)
  {
    return clock_ans;
  }

  if (div > SPI::PrescalerToDiv(Prescaler::DIV_256))
  {
    return ErrorCode::NOT_SUPPORT;
  }

  spi_timing_config_t timing{};
  spi_master_get_default_timing_config(&timing);
  timing.master_config.clk_src_freq_in_hz = source_clock_hz_;
  timing.master_config.sclk_freq_in_hz =
      (div == 1U) ? source_clock_hz_ : (source_clock_hz_ / div);
  if (timing.master_config.sclk_freq_in_hz == 0U)
  {
    return ErrorCode::ARG_ERR;
  }

  return ConvertStatus(spi_master_timing_init(spi_, &timing));
}

ErrorCode HPMSPI::SetConfig(SPI::Configuration config)
{
  if (spi_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
#if LIBXR_HPM_SPI_HAS_DMA_MGR
  if (DmaTransferActive())
  {
    return ErrorCode::BUSY;
  }
#endif

  const ErrorCode valid_ans = ValidateConfiguration(config);
  if (valid_ans != ErrorCode::OK)
  {
    return valid_ans;
  }

  ErrorCode ans = EnsureClockReady();
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  ans = ApplyTiming(config.prescaler);
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  ApplyFormat(config);

  GetConfig() = config;
  configured_ = true;
  return ErrorCode::OK;
}

ErrorCode HPMSPI::SetChipSelect(ChipSelect cs)
{
#if LIBXR_HPM_SPI_HAS_DMA_MGR
  if (DmaTransferActive())
  {
    return ErrorCode::BUSY;
  }
#endif
#if defined(HPM_IP_FEATURE_SPI_CS_SELECT) && (HPM_IP_FEATURE_SPI_CS_SELECT == 1)
  if (ConvertChipSelect(cs) == 0U)
  {
    return ErrorCode::ARG_ERR;
  }

  cs_ = cs;
  return ErrorCode::OK;
#else
  UNUSED(cs);
  return ErrorCode::NOT_SUPPORT;
#endif
}

uint32_t HPMSPI::GetMaxBusSpeed() const { return source_clock_hz_; }

SPI::Prescaler HPMSPI::GetMaxPrescaler() const { return Prescaler::DIV_256; }

ErrorCode HPMSPI::SetDmaEnabled(bool enabled)
{
#if LIBXR_HPM_SPI_HAS_DMA_MGR
  if (DmaTransferActive())
  {
    return ErrorCode::BUSY;
  }

  if (!enabled)
  {
    dma_enabled_ = false;
    return ErrorCode::OK;
  }

  const ErrorCode ans = EnsureDmaReady();
  if (ans != ErrorCode::OK)
  {
    return ans;
  }

  dma_enabled_ = true;
  return ErrorCode::OK;
#else
  dma_enabled_ = false;
  return enabled ? ErrorCode::NOT_SUPPORT : ErrorCode::OK;
#endif
}

#if LIBXR_HPM_SPI_HAS_DMA_MGR
ErrorCode HPMSPI::ConvertDmaStatus(hpm_stat_t status)
{
  switch (status)
  {
    case status_success:
      return ErrorCode::OK;
    case status_timeout:
      return ErrorCode::TIMEOUT;
    case status_invalid_argument:
      return ErrorCode::ARG_ERR;
    case status_dma_mgr_no_resource:
      return ErrorCode::FULL;
    default:
      return ErrorCode::FAILED;
  }
}

ErrorCode HPMSPI::EnsureDmaReady()
{
  if (dma_ready_)
  {
    return ErrorCode::OK;
  }
  if (spi_ == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }

  dma_mgr_init();
  hpm_stat_t status =
      hpm_spi_rx_dma_mgr_install_custom_callback(spi_, &HPMSPI::OnRxDmaTcCallback, this);
  if (status != status_success)
  {
    return (status == status_invalid_argument) ? ErrorCode::NOT_SUPPORT
                                               : ConvertDmaStatus(status);
  }

  status =
      hpm_spi_tx_dma_mgr_install_custom_callback(spi_, &HPMSPI::OnTxDmaTcCallback, this);
  if (status != status_success)
  {
    dma_resource_t* rx_resource = hpm_spi_get_rx_dma_resource(spi_);
    if (rx_resource != nullptr && rx_resource->base != nullptr)
    {
      (void)dma_mgr_release_resource(rx_resource);
    }
    return (status == status_invalid_argument) ? ErrorCode::NOT_SUPPORT
                                               : ConvertDmaStatus(status);
  }

  dma_ready_ = true;
  return ErrorCode::OK;
}

void HPMSPI::ClearDmaContext()
{
  dma_ctx_.kind = DmaTransferKind::NONE;
  dma_ctx_.op = OperationRW();
  dma_ctx_.rx = nullptr;
  dma_ctx_.tx = nullptr;
  dma_ctx_.user_read = {nullptr, 0};
  dma_ctx_.size = 0U;
  dma_ctx_.copy_rx_to_user = false;
  dma_ctx_.switch_buffer_on_success = false;
  dma_ctx_.rx_done.store(0U, std::memory_order_release);
  dma_ctx_.tx_done.store(0U, std::memory_order_release);
}

void HPMSPI::StopDmaTransfer()
{
  if (spi_ == nullptr)
  {
    return;
  }

  spi_disable_rx_dma(spi_);
  spi_disable_tx_dma(spi_);

  dma_resource_t* rx_resource = hpm_spi_get_rx_dma_resource(spi_);
  if (rx_resource != nullptr && rx_resource->base != nullptr)
  {
    (void)dma_mgr_disable_channel(rx_resource);
  }
  dma_resource_t* tx_resource = hpm_spi_get_tx_dma_resource(spi_);
  if (tx_resource != nullptr && tx_resource->base != nullptr)
  {
    (void)dma_mgr_disable_channel(tx_resource);
  }
}

void HPMSPI::AbortDmaStart()
{
  StopDmaTransfer();
  ClearDmaContext();
  dma_completion_claim_.store(0U, std::memory_order_release);
  dma_busy_.store(0U, std::memory_order_release);
}

bool HPMSPI::TryClaimDmaCompletion()
{
  uint32_t expected = 0U;
  return dma_completion_claim_.compare_exchange_strong(
      expected, 1U, std::memory_order_acq_rel, std::memory_order_acquire);
}

ErrorCode HPMSPI::StartDmaTransfer(uint8_t* rx, uint8_t* tx, uint32_t size,
                                   DmaTransferKind kind, RawData user_read,
                                   bool copy_rx_to_user, bool switch_buffer_on_success,
                                   OperationRW& op, bool in_isr)
{
  if (size == 0U)
  {
    return FinishOperation(op, in_isr, ErrorCode::OK);
  }
  if (DmaTransferActive())
  {
    return FinishOperation(op, in_isr, ErrorCode::BUSY);
  }
  if (kind == DmaTransferKind::READ_ONLY && rx == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (kind == DmaTransferKind::WRITE_ONLY && tx == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (kind == DmaTransferKind::WRITE_READ && (rx == nullptr || tx == nullptr))
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }

  ErrorCode ans = EnsureDmaReady();
  if (ans != ErrorCode::OK)
  {
    return FinishOperation(op, in_isr, ans);
  }

  dma_busy_.store(1U, std::memory_order_release);
  ClearDmaContext();
  dma_ctx_.kind = kind;
  dma_ctx_.op = op;
  dma_ctx_.rx = rx;
  dma_ctx_.tx = tx;
  dma_ctx_.user_read = user_read;
  dma_ctx_.size = size;
  dma_ctx_.copy_rx_to_user = copy_rx_to_user;
  dma_ctx_.switch_buffer_on_success = switch_buffer_on_success;
  dma_completion_claim_.store(0U, std::memory_order_release);

  if (op.type == OperationRW::OperationType::BLOCK)
  {
    dma_block_wait_.Start(*op.data.sem_info.sem);
  }
  op.MarkAsRunning();

  if (tx != nullptr)
  {
    FlushDCacheIfNeeded(tx, size);
  }
  if (rx != nullptr)
  {
    FlushDCacheIfNeeded(rx, size);
  }

  ApplyChipSelect();

  hpm_stat_t status = status_invalid_argument;
  switch (kind)
  {
    case DmaTransferKind::READ_ONLY:
      status = hpm_spi_receive_nonblocking(spi_, rx, size);
      break;
    case DmaTransferKind::WRITE_ONLY:
      status = hpm_spi_transmit_nonblocking(spi_, tx, size);
      break;
    case DmaTransferKind::WRITE_READ:
      status = hpm_spi_transmit_receive_nonblocking(spi_, tx, rx, size);
      break;
    case DmaTransferKind::NONE:
    default:
      status = status_invalid_argument;
      break;
  }

  ans = ConvertStatus(status);
  if (ans != ErrorCode::OK)
  {
    if (op.type == OperationRW::OperationType::BLOCK)
    {
      dma_block_wait_.Cancel();
    }
    AbortDmaStart();
    return FinishOperation(op, in_isr, ans);
  }

  if (op.type == OperationRW::OperationType::BLOCK)
  {
    return WaitForDmaBlockResult(op.data.sem_info.timeout);
  }
  return ErrorCode::OK;
}

ErrorCode HPMSPI::WaitForDmaBlockResult(uint32_t timeout)
{
  const ErrorCode ans = dma_block_wait_.Wait(timeout);
  if (ans == ErrorCode::TIMEOUT && DmaTransferActive())
  {
    CompleteDmaTransfer(false, ErrorCode::TIMEOUT);
  }
  return ans;
}

void HPMSPI::MaybeCompleteDmaTransfer(bool in_isr)
{
  if (!DmaTransferActive())
  {
    return;
  }

  bool ready = false;
  switch (dma_ctx_.kind)
  {
    case DmaTransferKind::READ_ONLY:
      ready = dma_ctx_.rx_done.load(std::memory_order_acquire) != 0U;
      break;
    case DmaTransferKind::WRITE_ONLY:
      ready = dma_ctx_.tx_done.load(std::memory_order_acquire) != 0U;
      break;
    case DmaTransferKind::WRITE_READ:
      ready = (dma_ctx_.rx_done.load(std::memory_order_acquire) != 0U) &&
              (dma_ctx_.tx_done.load(std::memory_order_acquire) != 0U);
      break;
    case DmaTransferKind::NONE:
    default:
      break;
  }

  if (ready)
  {
    CompleteDmaTransfer(in_isr, ErrorCode::OK);
  }
}

void HPMSPI::CompleteDmaTransfer(bool in_isr, ErrorCode ans)
{
  if (!TryClaimDmaCompletion())
  {
    return;
  }

  DmaTransferKind kind = dma_ctx_.kind;
  OperationRW op = dma_ctx_.op;
  uint8_t* rx = dma_ctx_.rx;
  const uint32_t size = dma_ctx_.size;
  const RawData user_read = dma_ctx_.user_read;
  const bool copy_rx_to_user = dma_ctx_.copy_rx_to_user;
  const bool switch_buffer_on_success = dma_ctx_.switch_buffer_on_success;
  bool dma_stopped = false;

  if (ans != ErrorCode::OK)
  {
    StopDmaTransfer();
    dma_stopped = true;
  }

  if (ans == ErrorCode::OK && !in_isr)
  {
    const hpm_stat_t idle_status = spi_wait_for_idle_status(spi_);
    ans = ConvertStatus(idle_status);
  }

  if (!dma_stopped)
  {
    StopDmaTransfer();
  }

  if (rx != nullptr &&
      (kind == DmaTransferKind::READ_ONLY || kind == DmaTransferKind::WRITE_READ))
  {
    InvalidateDCacheIfNeeded(rx, size);
  }

  if (ans == ErrorCode::OK && copy_rx_to_user && user_read.addr_ != nullptr &&
      user_read.size_ > 0U)
  {
    Memory::FastCopy(user_read.addr_, rx, user_read.size_);
  }
  if (ans == ErrorCode::OK && switch_buffer_on_success)
  {
    SwitchBuffer();
  }

  if (ans != ErrorCode::OK && !in_isr)
  {
    RecoverController();
  }

  ClearDmaContext();
  dma_busy_.store(0U, std::memory_order_release);

  if (op.type == OperationRW::OperationType::BLOCK)
  {
    (void)dma_block_wait_.TryPost(in_isr, ans);
  }
  else
  {
    op.UpdateStatus(in_isr, ans);
  }
}

void HPMSPI::OnRxDmaTcCallback(DMA_Type* base, uint32_t channel, void* cb_data_ptr)
{
  UNUSED(base);
  UNUSED(channel);
  auto* self = static_cast<HPMSPI*>(cb_data_ptr);
  if (self == nullptr || !self->DmaTransferActive())
  {
    return;
  }

  self->dma_ctx_.rx_done.store(1U, std::memory_order_release);
  self->MaybeCompleteDmaTransfer(true);
}

void HPMSPI::OnTxDmaTcCallback(DMA_Type* base, uint32_t channel, void* cb_data_ptr)
{
  UNUSED(base);
  UNUSED(channel);
  auto* self = static_cast<HPMSPI*>(cb_data_ptr);
  if (self == nullptr || !self->DmaTransferActive())
  {
    return;
  }

  self->dma_ctx_.tx_done.store(1U, std::memory_order_release);
  self->MaybeCompleteDmaTransfer(true);
}
#endif

ErrorCode HPMSPI::DoTransfer(uint8_t* rx, const uint8_t* tx, uint32_t size)
{
  if (size == 0)
  {
    return ErrorCode::OK;
  }
  if (rx == nullptr || tx == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
  if (size > kHpmSpiTransferCountMax)
  {
    return ErrorCode::SIZE_ERR;
  }

  spi_control_config_t control = MakeControlConfig(spi_trans_write_read_together);
  const hpm_stat_t status = spi_transfer(spi_, &control, nullptr, nullptr,
                                         const_cast<uint8_t*>(tx), size, rx, size);
  if (ShouldRecover(status))
  {
    RecoverController();
  }
  return ConvertStatus(status);
}

ErrorCode HPMSPI::DoWriteOnly(const uint8_t* tx, uint32_t size)
{
  if (size == 0)
  {
    return ErrorCode::OK;
  }
  if (tx == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
  if (size > kHpmSpiTransferCountMax)
  {
    return ErrorCode::SIZE_ERR;
  }

  spi_control_config_t control = MakeControlConfig(spi_trans_write_only);
  const hpm_stat_t status = spi_transfer(spi_, &control, nullptr, nullptr,
                                         const_cast<uint8_t*>(tx), size, nullptr, 1);
  if (ShouldRecover(status))
  {
    RecoverController();
  }
  return ConvertStatus(status);
}

ErrorCode HPMSPI::DoReadOnly(uint8_t* rx, uint32_t size)
{
  if (size == 0)
  {
    return ErrorCode::OK;
  }
  if (rx == nullptr)
  {
    return ErrorCode::PTR_NULL;
  }
  if (size > kHpmSpiTransferCountMax)
  {
    return ErrorCode::SIZE_ERR;
  }

  spi_control_config_t control = MakeControlConfig(spi_trans_read_only);
  const hpm_stat_t status =
      spi_transfer(spi_, &control, nullptr, nullptr, nullptr, 1, rx, size);
  if (ShouldRecover(status))
  {
    RecoverController();
  }
  return ConvertStatus(status);
}

ErrorCode HPMSPI::DoCommandRead(uint8_t command, uint8_t* rx, uint32_t size)
{
  return DoCommandWriteRead(command, nullptr, 0U, rx, size);
}

ErrorCode HPMSPI::DoCommandWriteRead(uint8_t command, const uint8_t* tx, uint32_t tx_size,
                                     uint8_t* rx, uint32_t rx_size)
{
  if ((tx_size > 0U && tx == nullptr) || (rx_size > 0U && rx == nullptr))
  {
    return ErrorCode::PTR_NULL;
  }
  if (tx_size > kHpmSpiTransferCountMax || rx_size > kHpmSpiTransferCountMax)
  {
    return ErrorCode::SIZE_ERR;
  }

  spi_trans_mode_t mode = spi_trans_no_data;
  if (tx_size > 0U && rx_size > 0U)
  {
    mode = spi_trans_write_read;
  }
  else if (tx_size > 0U)
  {
    mode = spi_trans_write_only;
  }
  else if (rx_size > 0U)
  {
    mode = spi_trans_read_only;
  }

  spi_control_config_t control = MakeControlConfig(mode);
  control.master_config.cmd_enable = true;
  const uint32_t wcount = (tx_size > 0U) ? tx_size : 1U;
  const uint32_t rcount = (rx_size > 0U) ? rx_size : 1U;
  const hpm_stat_t status = spi_transfer(spi_, &control, &command, nullptr,
                                         const_cast<uint8_t*>(tx), wcount, rx, rcount);
  if (ShouldRecover(status))
  {
    RecoverController();
  }
  return ConvertStatus(status);
}

ErrorCode HPMSPI::ReadAndWrite(RawData read_data, ConstRawData write_data,
                               OperationRW& op, bool in_isr)
{
#if LIBXR_HPM_SPI_HAS_DMA_MGR
  if (DmaTransferActive())
  {
    return FinishOperation(op, in_isr, ErrorCode::BUSY);
  }
#endif

  const size_t need = std::max(read_data.size_, write_data.size_);
  if (need == 0)
  {
    return FinishOperation(op, in_isr, ErrorCode::OK);
  }
  if (read_data.size_ > 0 && read_data.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (write_data.size_ > 0 && write_data.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (need > kHpmSpiTransferCountMax)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }

  RawData rx = GetRxBuffer();
  RawData tx = GetTxBuffer();
  if ((read_data.size_ > 0 && rx.addr_ == nullptr) || tx.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if ((read_data.size_ > 0 && rx.size_ < need) || tx.size_ < need)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }

  auto* rx_bytes = static_cast<uint8_t*>(rx.addr_);
  auto* tx_bytes = static_cast<uint8_t*>(tx.addr_);
  Memory::FastSet(tx_bytes, 0, need);
  if (write_data.size_ > 0)
  {
    Memory::FastCopy(tx_bytes, write_data.addr_, write_data.size_);
  }

#if LIBXR_HPM_SPI_HAS_DMA_MGR
  if (dma_enabled_)
  {
    DmaTransferKind kind = DmaTransferKind::WRITE_ONLY;
    if (read_data.size_ > 0 && write_data.size_ > 0)
    {
      kind = DmaTransferKind::WRITE_READ;
    }
    else if (read_data.size_ > 0)
    {
      kind = DmaTransferKind::READ_ONLY;
    }

    return StartDmaTransfer((read_data.size_ > 0) ? rx_bytes : nullptr,
                            (write_data.size_ > 0) ? tx_bytes : nullptr,
                            static_cast<uint32_t>(need), kind, read_data,
                            read_data.size_ > 0, true, op, in_isr);
  }
#endif

  ErrorCode ans;
  if (read_data.size_ > 0 && write_data.size_ > 0)
  {
    ans = DoTransfer(rx_bytes, tx_bytes, static_cast<uint32_t>(need));
  }
  else if (read_data.size_ > 0)
  {
    ans = DoReadOnly(rx_bytes, static_cast<uint32_t>(need));
  }
  else
  {
    ans = DoWriteOnly(tx_bytes, static_cast<uint32_t>(need));
  }

  if (ans == ErrorCode::OK && read_data.size_ > 0)
  {
    Memory::FastCopy(read_data.addr_, rx_bytes, read_data.size_);
  }

  if (ans == ErrorCode::OK)
  {
    SwitchBuffer();
  }
  return FinishOperation(op, in_isr, ans);
}

ErrorCode HPMSPI::CommandRead(uint8_t command, RawData read_data, OperationRW& op,
                              bool in_isr)
{
  if (read_data.size_ == 0)
  {
    UNUSED(command);
    return FinishOperation(op, in_isr, ErrorCode::OK);
  }

  return CommandWriteRead(command, ConstRawData(nullptr, 0), read_data, op, in_isr);
}

ErrorCode HPMSPI::CommandWriteRead(uint8_t command, ConstRawData write_data,
                                   RawData read_data, OperationRW& op, bool in_isr)
{
#if LIBXR_HPM_SPI_HAS_DMA_MGR
  if (DmaTransferActive())
  {
    return FinishOperation(op, in_isr, ErrorCode::BUSY);
  }
#endif

  if (write_data.size_ > 0 && write_data.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (read_data.size_ > 0 && read_data.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (write_data.size_ > kHpmSpiTransferCountMax ||
      read_data.size_ > kHpmSpiTransferCountMax)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }

  uint8_t* rx_bytes = nullptr;
  const uint8_t* tx_bytes = nullptr;

  if (write_data.size_ > 0)
  {
    RawData tx = GetTxBuffer();
    if (tx.addr_ == nullptr)
    {
      return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
    }
    if (tx.size_ < write_data.size_)
    {
      return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
    }

    auto* tx_buffer = static_cast<uint8_t*>(tx.addr_);
    Memory::FastCopy(tx_buffer, write_data.addr_, write_data.size_);
    tx_bytes = tx_buffer;
  }

  if (read_data.size_ == 0)
  {
    ErrorCode ans = DoCommandWriteRead(
        command, tx_bytes, static_cast<uint32_t>(write_data.size_), nullptr, 0U);
    if (ans == ErrorCode::OK)
    {
      SwitchBuffer();
    }
    return FinishOperation(op, in_isr, ans);
  }

  RawData rx = GetRxBuffer();
  if (rx.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (rx.size_ < read_data.size_)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }

  rx_bytes = static_cast<uint8_t*>(rx.addr_);
  ErrorCode ans =
      DoCommandWriteRead(command, tx_bytes, static_cast<uint32_t>(write_data.size_),
                         rx_bytes, static_cast<uint32_t>(read_data.size_));
  if (ans == ErrorCode::OK)
  {
    Memory::FastCopy(read_data.addr_, rx_bytes, read_data.size_);
    SwitchBuffer();
  }
  return FinishOperation(op, in_isr, ans);
}

ErrorCode HPMSPI::Transfer(size_t size, OperationRW& op, bool in_isr)
{
#if LIBXR_HPM_SPI_HAS_DMA_MGR
  if (DmaTransferActive())
  {
    return FinishOperation(op, in_isr, ErrorCode::BUSY);
  }
#endif

  if (size == 0)
  {
    return FinishOperation(op, in_isr, ErrorCode::OK);
  }
  if (size > kHpmSpiTransferCountMax)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }

  RawData rx = GetRxBuffer();
  RawData tx = GetTxBuffer();
  if (rx.addr_ == nullptr || tx.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (rx.size_ < size || tx.size_ < size)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }

#if LIBXR_HPM_SPI_HAS_DMA_MGR
  if (dma_enabled_)
  {
    return StartDmaTransfer(static_cast<uint8_t*>(rx.addr_),
                            static_cast<uint8_t*>(tx.addr_), static_cast<uint32_t>(size),
                            DmaTransferKind::WRITE_READ, RawData(nullptr, 0), false, true,
                            op, in_isr);
  }
#endif

  ErrorCode ans =
      DoTransfer(static_cast<uint8_t*>(rx.addr_), static_cast<const uint8_t*>(tx.addr_),
                 static_cast<uint32_t>(size));
  if (ans == ErrorCode::OK)
  {
    SwitchBuffer();
  }
  return FinishOperation(op, in_isr, ans);
}

ErrorCode HPMSPI::MemRead(uint16_t reg, RawData read_data, OperationRW& op, bool in_isr)
{
#if LIBXR_HPM_SPI_HAS_DMA_MGR
  if (DmaTransferActive())
  {
    return FinishOperation(op, in_isr, ErrorCode::BUSY);
  }
#endif

  if (read_data.size_ == 0)
  {
    return FinishOperation(op, in_isr, ErrorCode::OK);
  }
  if (read_data.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if ((reg & static_cast<uint16_t>(~kSpiRegisterAddressMask)) != 0U)
  {
    return FinishOperation(op, in_isr, ErrorCode::OUT_OF_RANGE);
  }

  const size_t total = read_data.size_ + 1;
  if (total > kHpmSpiTransferCountMax)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }

  RawData rx = GetRxBuffer();
  RawData tx = GetTxBuffer();
  if (rx.addr_ == nullptr || tx.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (rx.size_ < total || tx.size_ < total)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }

  auto* rx_bytes = static_cast<uint8_t*>(rx.addr_);
  auto* tx_bytes = static_cast<uint8_t*>(tx.addr_);
  tx_bytes[0] = static_cast<uint8_t>(reg | 0x80u);
  Memory::FastSet(tx_bytes + 1, 0, read_data.size_);

  ErrorCode ans = DoTransfer(rx_bytes, tx_bytes, static_cast<uint32_t>(total));
  if (ans == ErrorCode::OK)
  {
    Memory::FastCopy(read_data.addr_, rx_bytes + 1, read_data.size_);
  }

  if (ans == ErrorCode::OK)
  {
    SwitchBuffer();
  }
  return FinishOperation(op, in_isr, ans);
}

ErrorCode HPMSPI::MemWrite(uint16_t reg, ConstRawData write_data, OperationRW& op,
                           bool in_isr)
{
#if LIBXR_HPM_SPI_HAS_DMA_MGR
  if (DmaTransferActive())
  {
    return FinishOperation(op, in_isr, ErrorCode::BUSY);
  }
#endif

  const size_t total = write_data.size_ + 1;
  if ((reg & static_cast<uint16_t>(~kSpiRegisterAddressMask)) != 0U)
  {
    return FinishOperation(op, in_isr, ErrorCode::OUT_OF_RANGE);
  }
  if (write_data.size_ > 0 && write_data.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (total > kHpmSpiTransferCountMax)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }

  RawData tx = GetTxBuffer();
  if (tx.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (tx.size_ < total)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }

  auto* tx_bytes = static_cast<uint8_t*>(tx.addr_);
  tx_bytes[0] = static_cast<uint8_t>(reg & 0x7Fu);
  if (write_data.size_ == 0 && tx.size_ > 1)
  {
    tx_bytes[1] = 0;
  }
  if (write_data.size_ > 0)
  {
    Memory::FastCopy(tx_bytes + 1, write_data.addr_, write_data.size_);
  }

  ErrorCode ans = DoWriteOnly(tx_bytes, static_cast<uint32_t>(total));
  if (ans == ErrorCode::OK)
  {
    SwitchBuffer();
  }
  return FinishOperation(op, in_isr, ans);
}

#else

using namespace LibXR;

HPMSPI::HPMSPI(LibXRHpmSpiType* spi, clock_name_t clock, RawData rx_buffer,
               RawData tx_buffer, bool auto_board_init, SPI::Configuration config,
               ChipSelect cs)
    : SPI(rx_buffer, tx_buffer),
      spi_(spi),
      clock_(clock),
      rx_buffer_capacity_(rx_buffer.size_),
      tx_buffer_capacity_(tx_buffer.size_),
      auto_board_init_(auto_board_init),
      cs_(cs)
{
  (void)spi_;
  (void)clock_;
  (void)auto_board_init_;
  (void)cs_;
  GetConfig() = config;
}

ErrorCode HPMSPI::ConvertStatus(LibXRHpmSpiStatusType status)
{
  UNUSED(status);
  return ErrorCode::NOT_SUPPORT;
}

bool HPMSPI::ShouldRecover(LibXRHpmSpiStatusType status)
{
  UNUSED(status);
  return false;
}

ErrorCode HPMSPI::ValidateConfiguration(const Configuration& config) const
{
  UNUSED(config);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMSPI::EnsureClockReady() { return ErrorCode::NOT_SUPPORT; }

void HPMSPI::ApplyFormat(const Configuration& config) { UNUSED(config); }

void HPMSPI::RecoverController() {}

ErrorCode HPMSPI::ApplyTiming(Prescaler prescaler)
{
  UNUSED(prescaler);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMSPI::SetConfig(SPI::Configuration config)
{
  GetConfig() = config;
  configured_ = false;
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMSPI::SetChipSelect(ChipSelect cs)
{
  UNUSED(cs);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMSPI::SetDmaEnabled(bool enabled)
{
  dma_enabled_ = false;
  return enabled ? ErrorCode::NOT_SUPPORT : ErrorCode::OK;
}

uint32_t HPMSPI::GetMaxBusSpeed() const { return 0U; }

SPI::Prescaler HPMSPI::GetMaxPrescaler() const { return Prescaler::UNKNOWN; }

ErrorCode HPMSPI::DoTransfer(uint8_t* rx, const uint8_t* tx, uint32_t size)
{
  UNUSED(rx);
  UNUSED(tx);
  UNUSED(size);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMSPI::DoWriteOnly(const uint8_t* tx, uint32_t size)
{
  UNUSED(tx);
  UNUSED(size);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMSPI::DoReadOnly(uint8_t* rx, uint32_t size)
{
  UNUSED(rx);
  UNUSED(size);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMSPI::DoCommandRead(uint8_t command, uint8_t* rx, uint32_t size)
{
  UNUSED(command);
  UNUSED(rx);
  UNUSED(size);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMSPI::DoCommandWriteRead(uint8_t command, const uint8_t* tx, uint32_t tx_size,
                                     uint8_t* rx, uint32_t rx_size)
{
  UNUSED(command);
  UNUSED(tx);
  UNUSED(tx_size);
  UNUSED(rx);
  UNUSED(rx_size);
  return ErrorCode::NOT_SUPPORT;
}

ErrorCode HPMSPI::ReadAndWrite(RawData read_data, ConstRawData write_data,
                               OperationRW& op, bool in_isr)
{
  UNUSED(read_data);
  UNUSED(write_data);
  return FinishOperation(op, in_isr, ErrorCode::NOT_SUPPORT);
}

ErrorCode HPMSPI::CommandRead(uint8_t command, RawData read_data, OperationRW& op,
                              bool in_isr)
{
  UNUSED(command);
  UNUSED(read_data);
  return FinishOperation(op, in_isr, ErrorCode::NOT_SUPPORT);
}

ErrorCode HPMSPI::CommandWriteRead(uint8_t command, ConstRawData write_data,
                                   RawData read_data, OperationRW& op, bool in_isr)
{
  UNUSED(command);
  UNUSED(write_data);
  UNUSED(read_data);
  return FinishOperation(op, in_isr, ErrorCode::NOT_SUPPORT);
}

ErrorCode HPMSPI::Transfer(size_t size, OperationRW& op, bool in_isr)
{
  UNUSED(size);
  return FinishOperation(op, in_isr, ErrorCode::NOT_SUPPORT);
}

ErrorCode HPMSPI::MemRead(uint16_t reg, RawData read_data, OperationRW& op, bool in_isr)
{
  UNUSED(reg);
  UNUSED(read_data);
  return FinishOperation(op, in_isr, ErrorCode::NOT_SUPPORT);
}

ErrorCode HPMSPI::MemWrite(uint16_t reg, ConstRawData write_data, OperationRW& op,
                           bool in_isr)
{
  UNUSED(reg);
  UNUSED(write_data);
  return FinishOperation(op, in_isr, ErrorCode::NOT_SUPPORT);
}

#endif
