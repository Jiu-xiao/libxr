#include "hpm_spi.hpp"

#include <algorithm>

#if __has_include("board.h")
#include "board.h"
#define LIBXR_HPM_SPI_HAS_BOARD_HELPER 1
#else
#define LIBXR_HPM_SPI_HAS_BOARD_HELPER 0
#endif

using namespace LibXR;

HPMSPI::HPMSPI(SPI_Type* spi, clock_name_t clock, RawData rx_buffer, RawData tx_buffer,
               bool auto_board_init, SPI::Configuration config,
               ChipSelectControl chip_select)
    : SPI(rx_buffer, tx_buffer),
      spi_(spi),
      clock_(clock),
      rx_buffer_capacity_(rx_buffer.size_),
      tx_buffer_capacity_(tx_buffer.size_),
      auto_board_init_(auto_board_init),
      chip_select_(chip_select)
{
  ASSERT(spi_ != nullptr);
  ASSERT(rx_buffer.addr_ != nullptr);
  ASSERT(tx_buffer.addr_ != nullptr);
  ASSERT(rx_buffer.size_ > 0);
  ASSERT(tx_buffer.size_ > 0);

#if LIBXR_HPM_SPI_HAS_BOARD_HELPER
  if (auto_board_init_)
  {
    board_init_spi_pins(spi_);
    source_clock_hz_ = board_init_spi_clock(spi_);
  }
#else
  (void)auto_board_init_;
#endif

  if (source_clock_hz_ == 0)
  {
    clock_add_to_group(clock_, 0);
    source_clock_hz_ = clock_get_frequency(clock_);
  }

  ASSERT(source_clock_hz_ != 0);
  const ErrorCode ans = SetConfig(config);
  ASSERT(ans == ErrorCode::OK);
}

ErrorCode HPMSPI::ConvertStatus(hpm_stat_t status)
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

bool HPMSPI::ShouldRecover(hpm_stat_t status)
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
  control.common_config.cs_index = spi_cs_0;
#endif
  return control;
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

uint32_t HPMSPI::GetMaxBusSpeed() const { return source_clock_hz_; }

SPI::Prescaler HPMSPI::GetMaxPrescaler() const { return Prescaler::DIV_256; }

void HPMSPI::SetChipSelect(bool selected) const
{
  if (chip_select_ != nullptr)
  {
    chip_select_(selected);
  }
}

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
  if (size > SPI_SOC_TRANSFER_COUNT_MAX)
  {
    return ErrorCode::SIZE_ERR;
  }

  spi_control_config_t control = MakeControlConfig(spi_trans_write_read_together);
  SetChipSelect(true);
  const hpm_stat_t status = spi_transfer(spi_, &control, nullptr, nullptr,
                                         const_cast<uint8_t*>(tx), size, rx, size);
  SetChipSelect(false);
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
  if (size > SPI_SOC_TRANSFER_COUNT_MAX)
  {
    return ErrorCode::SIZE_ERR;
  }

  spi_control_config_t control = MakeControlConfig(spi_trans_write_only);
  SetChipSelect(true);
  const hpm_stat_t status = spi_transfer(spi_, &control, nullptr, nullptr,
                                         const_cast<uint8_t*>(tx), size, nullptr, 1);
  SetChipSelect(false);
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
  if (size > SPI_SOC_TRANSFER_COUNT_MAX)
  {
    return ErrorCode::SIZE_ERR;
  }

  spi_control_config_t control = MakeControlConfig(spi_trans_read_only);
  SetChipSelect(true);
  const hpm_stat_t status =
      spi_transfer(spi_, &control, nullptr, nullptr, nullptr, 1, rx, size);
  SetChipSelect(false);
  if (ShouldRecover(status))
  {
    RecoverController();
  }
  return ConvertStatus(status);
}

ErrorCode HPMSPI::ReadAndWrite(RawData read_data, ConstRawData write_data,
                               OperationRW& op, bool in_isr)
{
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
  if (need > SPI_SOC_TRANSFER_COUNT_MAX)
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

ErrorCode HPMSPI::Transfer(size_t size, OperationRW& op, bool in_isr)
{
  if (size == 0)
  {
    return FinishOperation(op, in_isr, ErrorCode::OK);
  }
  if (size > SPI_SOC_TRANSFER_COUNT_MAX)
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
  if (read_data.size_ == 0)
  {
    return FinishOperation(op, in_isr, ErrorCode::OK);
  }
  if (read_data.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }

  if (read_data.size_ >= SPI_SOC_TRANSFER_COUNT_MAX)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }
  const size_t total = read_data.size_ + 1;
  if (total > SPI_SOC_TRANSFER_COUNT_MAX)
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
  if (write_data.size_ >= SPI_SOC_TRANSFER_COUNT_MAX)
  {
    return FinishOperation(op, in_isr, ErrorCode::SIZE_ERR);
  }
  const size_t total = write_data.size_ + 1;
  if (write_data.size_ > 0 && write_data.addr_ == nullptr)
  {
    return FinishOperation(op, in_isr, ErrorCode::PTR_NULL);
  }
  if (total > SPI_SOC_TRANSFER_COUNT_MAX)
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
