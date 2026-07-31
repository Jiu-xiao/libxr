#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include "driver/spi_loopback_test.hpp"
#include "libxr.hpp"

namespace
{

class MemoryLoopbackSpi : public LibXR::SPI
{
 public:
  MemoryLoopbackSpi(LibXR::RawData rx_buffer, LibXR::RawData tx_buffer)
      : LibXR::SPI(rx_buffer, tx_buffer)
  {
  }

  LibXR::ErrorCode ReadAndWrite(LibXR::RawData read_data, LibXR::ConstRawData write_data,
                                OperationRW& op, bool in_isr) override
  {
    transfer_calls_++;
    if (op.type == OperationRW::OperationType::BLOCK)
    {
      block_transfer_calls_++;
    }
    if (fail_next_transfer_)
    {
      fail_next_transfer_ = false;
      return LibXR::ErrorCode::FAILED;
    }
    if (timeout_next_transfer_)
    {
      timeout_next_transfer_ = false;
      return LibXR::ErrorCode::TIMEOUT;
    }
    if (read_data.addr_ == nullptr || write_data.addr_ == nullptr ||
        read_data.size_ != write_data.size_)
    {
      return LibXR::ErrorCode::ARG_ERR;
    }

    if (!skip_next_rx_write_)
    {
      std::memcpy(read_data.addr_, write_data.addr_, read_data.size_);
    }
    skip_next_rx_write_ = false;
    if (corrupt_next_transfer_ && read_data.size_ > 0U)
    {
      corrupt_next_transfer_ = false;
      static_cast<uint8_t*>(read_data.addr_)[0] ^= 0x80U;
    }
    if (op.type != OperationRW::OperationType::BLOCK)
    {
      op.UpdateStatus(in_isr, LibXR::ErrorCode::OK);
    }
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode SetConfig(Configuration config) override
  {
    config_calls_++;
    if (fail_next_config_)
    {
      fail_next_config_ = false;
      return LibXR::ErrorCode::FAILED;
    }
    GetConfig() = config;
    return LibXR::ErrorCode::OK;
  }

  uint32_t GetMaxBusSpeed() const override { return 48000000U; }

  Prescaler GetMaxPrescaler() const override { return Prescaler::DIV_256; }

  LibXR::ErrorCode Transfer(size_t, OperationRW&, bool) override
  {
    return LibXR::ErrorCode::NOT_SUPPORT;
  }

  LibXR::ErrorCode MemWrite(uint16_t, LibXR::ConstRawData, OperationRW&, bool) override
  {
    return LibXR::ErrorCode::NOT_SUPPORT;
  }

  LibXR::ErrorCode MemRead(uint16_t, LibXR::RawData, OperationRW&, bool) override
  {
    return LibXR::ErrorCode::NOT_SUPPORT;
  }

  uint32_t transfer_calls_ = 0U;
  uint32_t block_transfer_calls_ = 0U;
  uint32_t config_calls_ = 0U;
  bool fail_next_transfer_ = false;
  bool fail_next_config_ = false;
  bool corrupt_next_transfer_ = false;
  bool skip_next_rx_write_ = false;
  bool timeout_next_transfer_ = false;
};

bool Check(bool condition, const char* expression, int line)
{
  if (!condition)
  {
    std::fprintf(stderr, "selftest failure at line %d: %s\n", line, expression);
  }
  return condition;
}

#define SELF_CHECK(expression)                                 \
  do                                                           \
  {                                                            \
    if (!Check((expression), #expression, __LINE__)) return 1; \
  } while (false)

}  // namespace

int main()
{
  LibXR::PlatformInit();

  alignas(size_t) std::array<uint8_t, 256U> driver_rx{};
  alignas(size_t) std::array<uint8_t, 256U> driver_tx{};
  std::array<uint8_t, 128U> tx{};
  std::array<uint8_t, 128U> rx{};
  MemoryLoopbackSpi spi({driver_rx.data(), driver_rx.size()},
                        {driver_tx.data(), driver_tx.size()});

  constexpr std::array<size_t, 7U> kSizes = {1U, 3U, 4U, 31U, 32U, 33U, 64U};
  const LibXRTest::SpiLoopbackTestCase test_case = {
      .spi_config =
          {
              .clock_polarity = LibXR::SPI::ClockPolarity::LOW,
              .clock_phase = LibXR::SPI::ClockPhase::EDGE_1,
              .prescaler = LibXR::SPI::Prescaler::DIV_64,
              .double_buffer = false,
          },
      .transfer_sizes = std::span<const size_t>(kSizes),
      .rounds = 3U,
      .operation_timeout_ms = 100U,
      .pattern_seed = 0x12345678U,
  };

  auto result = LibXRTest::RunSpiLoopbackTest(spi, test_case, {tx.data(), tx.size()},
                                              {rx.data(), rx.size()});
  SELF_CHECK(result.Passed());
  SELF_CHECK(result.completed_transfers == 21U);
  SELF_CHECK(result.verified_bytes == 504U);
  SELF_CHECK(spi.transfer_calls_ == 21U);
  SELF_CHECK(spi.block_transfer_calls_ == 21U);
  SELF_CHECK(spi.config_calls_ == 1U);

  spi.corrupt_next_transfer_ = true;
  result = LibXRTest::RunSpiLoopbackTest(spi, test_case, {tx.data(), tx.size()},
                                         {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::DATA_MISMATCH);
  SELF_CHECK(result.error == LibXR::ErrorCode::CHECK_ERR);
  SELF_CHECK(result.failed_round == 0U);
  SELF_CHECK(result.failed_transfer_index == 0U);
  SELF_CHECK(result.mismatch_offset == 0U);

  spi.skip_next_rx_write_ = true;
  result = LibXRTest::RunSpiLoopbackTest(spi, test_case, {tx.data(), tx.size()},
                                         {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::DATA_MISMATCH);
  SELF_CHECK(result.error == LibXR::ErrorCode::CHECK_ERR);
  SELF_CHECK(result.mismatch_offset == 0U);

  spi.fail_next_transfer_ = true;
  result = LibXRTest::RunSpiLoopbackTest(spi, test_case, {tx.data(), tx.size()},
                                         {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::TRANSFER);
  SELF_CHECK(result.error == LibXR::ErrorCode::FAILED);
  SELF_CHECK(!result.transfer_retirement_unconfirmed);

  spi.timeout_next_transfer_ = true;
  result = LibXRTest::RunSpiLoopbackTest(spi, test_case, {tx.data(), tx.size()},
                                         {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::TRANSFER);
  SELF_CHECK(result.error == LibXR::ErrorCode::TIMEOUT);
  SELF_CHECK(result.transfer_retirement_unconfirmed);

  spi.fail_next_config_ = true;
  result = LibXRTest::RunSpiLoopbackTest(spi, test_case, {tx.data(), tx.size()},
                                         {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::SET_CONFIG);
  SELF_CHECK(result.error == LibXR::ErrorCode::FAILED);

  auto double_buffer_case = test_case;
  double_buffer_case.spi_config.double_buffer = true;
  const uint32_t config_calls_before_rejection = spi.config_calls_;
  result = LibXRTest::RunSpiLoopbackTest(spi, double_buffer_case, {tx.data(), tx.size()},
                                         {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::INVALID_ARGUMENT);
  SELF_CHECK(result.error == LibXR::ErrorCode::ARG_ERR);
  SELF_CHECK(spi.config_calls_ == config_calls_before_rejection);

  const uint32_t config_calls_before_overlap = spi.config_calls_;
  result = LibXRTest::RunSpiLoopbackTest(spi, test_case, {tx.data(), tx.size()},
                                         {tx.data(), tx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::INVALID_ARGUMENT);
  SELF_CHECK(result.error == LibXR::ErrorCode::ARG_ERR);
  SELF_CHECK(spi.config_calls_ == config_calls_before_overlap);

  result = LibXRTest::RunSpiLoopbackTest(spi, test_case, {tx.data(), 32U},
                                         {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::INVALID_ARGUMENT);
  SELF_CHECK(result.error == LibXR::ErrorCode::SIZE_ERR);

  alignas(size_t) std::array<uint8_t, 32U> small_driver_rx{};
  alignas(size_t) std::array<uint8_t, 32U> small_driver_tx{};
  MemoryLoopbackSpi small_spi({small_driver_rx.data(), small_driver_rx.size()},
                              {small_driver_tx.data(), small_driver_tx.size()});
  result = LibXRTest::RunSpiLoopbackTest(small_spi, test_case, {tx.data(), tx.size()},
                                         {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::INVALID_ARGUMENT);
  SELF_CHECK(result.error == LibXR::ErrorCode::SIZE_ERR);
  SELF_CHECK(small_spi.transfer_calls_ == 0U);

  MemoryLoopbackSpi overlapping_driver({driver_rx.data(), driver_rx.size()},
                                       {driver_rx.data(), driver_rx.size()});
  result = LibXRTest::RunSpiLoopbackTest(overlapping_driver, test_case,
                                         {tx.data(), tx.size()}, {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::INVALID_ARGUMENT);
  SELF_CHECK(result.error == LibXR::ErrorCode::ARG_ERR);
  SELF_CHECK(overlapping_driver.transfer_calls_ == 0U);

  auto empty_sizes_case = test_case;
  empty_sizes_case.transfer_sizes = {};
  result = LibXRTest::RunSpiLoopbackTest(spi, empty_sizes_case, {tx.data(), tx.size()},
                                         {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::INVALID_ARGUMENT);
  SELF_CHECK(result.error == LibXR::ErrorCode::ARG_ERR);

  constexpr std::array<size_t, 2U> kOverflowSizes = {SIZE_MAX, 1U};
  auto overflow_case = test_case;
  overflow_case.transfer_sizes = std::span<const size_t>(kOverflowSizes);
  result = LibXRTest::RunSpiLoopbackTest(spi, overflow_case, {tx.data(), tx.size()},
                                         {rx.data(), rx.size()});
  SELF_CHECK(result.failure == LibXRTest::SpiLoopbackFailure::INVALID_ARGUMENT);
  SELF_CHECK(result.error == LibXR::ErrorCode::ARG_ERR);

  SELF_CHECK(std::strcmp(LibXRTest::SpiLoopbackFailureName(
                             LibXRTest::SpiLoopbackFailure::DATA_MISMATCH),
                         "DATA_MISMATCH") == 0);

  std::puts("spi hardware-test support selftest: PASS");
  return 0;
}
