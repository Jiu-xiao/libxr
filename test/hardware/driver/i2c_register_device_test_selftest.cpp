#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include "driver/i2c_register_device_test.hpp"
#include "libxr.hpp"

namespace
{

class FakeRegisterI2c : public LibXR::I2C
{
 public:
  FakeRegisterI2c(LibXR::Semaphore& expected_semaphore, uint32_t expected_timeout_ms)
      : expected_semaphore_(&expected_semaphore),
        expected_timeout_ms_(expected_timeout_ms)
  {
  }

  LibXR::ErrorCode Read(uint16_t, LibXR::RawData, LibXR::ReadOperation&, bool) override
  {
    unexpected_api_calls_++;
    return LibXR::ErrorCode::NOT_SUPPORT;
  }

  LibXR::ErrorCode Write(uint16_t, LibXR::ConstRawData, LibXR::WriteOperation&,
                         bool) override
  {
    unexpected_api_calls_++;
    return LibXR::ErrorCode::NOT_SUPPORT;
  }

  LibXR::ErrorCode SetConfig(Configuration) override
  {
    unexpected_api_calls_++;
    return LibXR::ErrorCode::NOT_SUPPORT;
  }

  LibXR::ErrorCode MemRead(uint16_t slave_addr, uint16_t mem_addr,
                           LibXR::RawData read_data, LibXR::ReadOperation& op,
                           MemAddrLength mem_addr_size, bool in_isr) override
  {
    read_calls_++;
    ObserveOperation(op, in_isr, read_data.size_);
    if (read_data.size_ == 1U)
    {
      polling_read_calls_++;
    }
    else if (read_data.size_ >= 4U)
    {
      multi_byte_read_calls_++;
    }
    else
    {
      expected_transfer_sizes_ = false;
    }

    if (slave_addr != expected_device_address_)
    {
      return LibXR::ErrorCode::NO_RESPONSE;
    }
    if (!ValidTransfer(mem_addr, read_data.addr_, read_data.size_, mem_addr_size))
    {
      return LibXR::ErrorCode::ARG_ERR;
    }
    if (read_calls_ == fail_read_call_)
    {
      return read_failure_error_;
    }

    std::memcpy(read_data.addr_, registers_.data() + mem_addr, read_data.size_);
    if (read_calls_ == corrupt_read_call_ && read_data.size_ > 0U)
    {
      const size_t offset = corrupt_read_offset_ < read_data.size_ ? corrupt_read_offset_
                                                                   : read_data.size_ - 1U;
      static_cast<uint8_t*>(read_data.addr_)[offset] ^= 0x80U;
    }
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode MemWrite(uint16_t slave_addr, uint16_t mem_addr,
                            LibXR::ConstRawData write_data, LibXR::WriteOperation& op,
                            MemAddrLength mem_addr_size, bool in_isr) override
  {
    write_calls_++;
    ObserveOperation(op, in_isr, write_data.size_);
    if (write_data.size_ >= 4U)
    {
      multi_byte_write_calls_++;
    }
    else
    {
      expected_transfer_sizes_ = false;
    }

    if (slave_addr != expected_device_address_)
    {
      return LibXR::ErrorCode::NO_RESPONSE;
    }
    if (!ValidTransfer(mem_addr, write_data.addr_, write_data.size_, mem_addr_size))
    {
      return LibXR::ErrorCode::ARG_ERR;
    }
    if (write_calls_ == fail_write_call_)
    {
      if (mutate_on_failed_write_ && write_data.size_ > 0U)
      {
        size_t partial_size = write_data.size_ / 2U;
        if (partial_size == 0U)
        {
          partial_size = 1U;
        }
        std::memcpy(registers_.data() + mem_addr, write_data.addr_, partial_size);
      }
      return write_failure_error_;
    }

    std::memcpy(registers_.data() + mem_addr, write_data.addr_, write_data.size_);
    return LibXR::ErrorCode::OK;
  }

  void SetBytes(uint16_t address, std::span<const uint8_t> bytes)
  {
    std::memcpy(registers_.data() + address, bytes.data(), bytes.size());
  }

  bool BytesEqual(uint16_t address, std::span<const uint8_t> bytes) const
  {
    return std::memcmp(registers_.data() + address, bytes.data(), bytes.size()) == 0;
  }

  uint16_t expected_device_address_ = 0x52U;
  uint32_t fail_read_call_ = UINT32_MAX;
  uint32_t fail_write_call_ = UINT32_MAX;
  uint32_t corrupt_read_call_ = UINT32_MAX;
  size_t corrupt_read_offset_ = 0U;
  LibXR::ErrorCode read_failure_error_ = LibXR::ErrorCode::FAILED;
  LibXR::ErrorCode write_failure_error_ = LibXR::ErrorCode::FAILED;
  bool mutate_on_failed_write_ = false;
  uint32_t read_calls_ = 0U;
  uint32_t write_calls_ = 0U;
  uint32_t polling_read_calls_ = 0U;
  uint32_t multi_byte_read_calls_ = 0U;
  uint32_t multi_byte_write_calls_ = 0U;
  uint32_t unexpected_api_calls_ = 0U;
  bool all_operations_block_ = true;
  bool all_operations_bounded_ = true;
  bool all_calls_task_context_ = true;
  bool expected_transfer_sizes_ = true;

 private:
  template <typename OperationType>
  void ObserveOperation(const OperationType& operation, bool in_isr, size_t size)
  {
    if (operation.type != OperationType::OperationType::BLOCK)
    {
      all_operations_block_ = false;
    }
    else
    {
      all_operations_bounded_ = all_operations_bounded_ &&
                                operation.data.sem_info.sem == expected_semaphore_ &&
                                operation.data.sem_info.timeout == expected_timeout_ms_ &&
                                operation.data.sem_info.timeout != 0U &&
                                operation.data.sem_info.timeout != UINT32_MAX;
    }
    all_calls_task_context_ = all_calls_task_context_ && !in_isr;
    expected_transfer_sizes_ = expected_transfer_sizes_ && size > 0U;
  }

  bool ValidTransfer(uint16_t mem_addr, const void* data, size_t size,
                     MemAddrLength mem_addr_size) const
  {
    return data != nullptr && size > 0U && mem_addr_size == MemAddrLength::BYTE_8 &&
           mem_addr < registers_.size() && size <= registers_.size() - mem_addr;
  }

  std::array<uint8_t, 256U> registers_{};
  LibXR::Semaphore* expected_semaphore_ = nullptr;
  uint32_t expected_timeout_ms_ = 0U;
};

struct Fixture
{
  static constexpr uint16_t kDeviceAddress = 0x52U;
  static constexpr uint16_t kIdentityRegister = 0x0FU;
  static constexpr uint16_t kWritableRegister = 0x20U;
  static constexpr uint16_t kContinuousRegister = 0x40U;
  static constexpr uint8_t kIdentity = 0xA5U;
  static constexpr uint32_t kTimeoutMs = 25U;

  Fixture()
      : semaphore(0U),
        i2c(semaphore, kTimeoutMs),
        test_case{
            .device_address = kDeviceAddress,
            .identity_register = kIdentityRegister,
            .expected_identity = kIdentity,
            .writable_register = kWritableRegister,
            .write_payload = std::span<const uint8_t>(payload),
            .continuous_read_register = kContinuousRegister,
            .continuous_read_size = continuous_value.size(),
            .repeated_read_count = 4U,
            .operation_timeout_ms = kTimeoutMs,
            .memory_address_length = LibXR::I2C::MemAddrLength::BYTE_8,
        },
        workspace{
            .saved_writable_value = {saved_value.data(), saved_value.size()},
            .io_buffer = {io_buffer.data(), io_buffer.size()},
        }
  {
    i2c.expected_device_address_ = kDeviceAddress;
    const std::array<uint8_t, 1U> identity = {kIdentity};
    i2c.SetBytes(kIdentityRegister, identity);
    i2c.SetBytes(kWritableRegister, original_value);
    i2c.SetBytes(kContinuousRegister, continuous_value);
  }

  LibXR::Semaphore semaphore;
  FakeRegisterI2c i2c;
  std::array<uint8_t, 6U> original_value = {0x91U, 0x82U, 0x73U, 0x64U, 0x55U, 0x46U};
  std::array<uint8_t, 6U> payload = {0x12U, 0x23U, 0x34U, 0x45U, 0x56U, 0x67U};
  std::array<uint8_t, 8U> continuous_value = {0x10U, 0x20U, 0x30U, 0x40U,
                                              0x50U, 0x60U, 0x70U, 0x80U};
  std::array<uint8_t, 6U> saved_value{};
  std::array<uint8_t, 8U> io_buffer{};
  LibXRTest::I2cRegisterDeviceTestCase test_case;
  LibXRTest::I2cRegisterDeviceTestWorkspace workspace;
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
  {
    Fixture fixture;
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.Passed());
    SELF_CHECK(result.identity_read_completed);
    SELF_CHECK(result.observed_identity == Fixture::kIdentity);
    SELF_CHECK(result.original_value_saved);
    SELF_CHECK(result.restore_attempted);
    SELF_CHECK(result.restore_verified);
    SELF_CHECK(!result.device_state_uncertain);
    SELF_CHECK(!result.operation_retirement_unconfirmed);
    SELF_CHECK(result.completed_multi_byte_reads == 7U);
    SELF_CHECK(result.completed_multi_byte_writes == 2U);
    SELF_CHECK(result.completed_repeated_reads == 4U);
    SELF_CHECK(result.multi_byte_bytes_transferred == 62U);
    SELF_CHECK(result.repeated_read_checksum != 2166136261U);
    SELF_CHECK(fixture.i2c.read_calls_ == 8U);
    SELF_CHECK(fixture.i2c.write_calls_ == 2U);
    SELF_CHECK(fixture.i2c.polling_read_calls_ == 1U);
    SELF_CHECK(fixture.i2c.multi_byte_read_calls_ == 7U);
    SELF_CHECK(fixture.i2c.multi_byte_write_calls_ == 2U);
    SELF_CHECK(fixture.i2c.all_operations_block_);
    SELF_CHECK(fixture.i2c.all_operations_bounded_);
    SELF_CHECK(fixture.i2c.all_calls_task_context_);
    SELF_CHECK(fixture.i2c.expected_transfer_sizes_);
    SELF_CHECK(fixture.i2c.unexpected_api_calls_ == 0U);
    SELF_CHECK(
        fixture.i2c.BytesEqual(Fixture::kWritableRegister, fixture.original_value));
  }

  {
    Fixture fixture;
    fixture.i2c.fail_read_call_ = 1U;
    fixture.i2c.read_failure_error_ = LibXR::ErrorCode::NO_RESPONSE;
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::DEVICE_NOT_FOUND);
    SELF_CHECK(result.error == LibXR::ErrorCode::NO_RESPONSE);
    SELF_CHECK(fixture.i2c.read_calls_ == 1U);
    SELF_CHECK(fixture.i2c.write_calls_ == 0U);
  }

  {
    Fixture fixture;
    const std::array<uint8_t, 1U> wrong_identity = {0x5AU};
    fixture.i2c.SetBytes(Fixture::kIdentityRegister, wrong_identity);
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::IDENTITY_MISMATCH);
    SELF_CHECK(result.error == LibXR::ErrorCode::CHECK_ERR);
    SELF_CHECK(result.observed_identity == wrong_identity[0]);
    SELF_CHECK(fixture.i2c.read_calls_ == 1U);
    SELF_CHECK(fixture.i2c.write_calls_ == 0U);
  }

  {
    Fixture fixture;
    fixture.i2c.fail_write_call_ = 1U;
    fixture.i2c.mutate_on_failed_write_ = true;
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::WRITE);
    SELF_CHECK(result.error == LibXR::ErrorCode::FAILED);
    SELF_CHECK(result.restore_attempted);
    SELF_CHECK(result.restore_verified);
    SELF_CHECK(!result.device_state_uncertain);
    SELF_CHECK(fixture.i2c.write_calls_ == 2U);
    SELF_CHECK(
        fixture.i2c.BytesEqual(Fixture::kWritableRegister, fixture.original_value));
  }

  {
    Fixture fixture;
    fixture.i2c.fail_read_call_ = 3U;
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::READ_BACK);
    SELF_CHECK(result.restore_attempted);
    SELF_CHECK(result.restore_verified);
    SELF_CHECK(fixture.i2c.write_calls_ == 2U);
    SELF_CHECK(
        fixture.i2c.BytesEqual(Fixture::kWritableRegister, fixture.original_value));
  }

  {
    Fixture fixture;
    fixture.i2c.fail_write_call_ = 2U;
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::RESTORE_WRITE);
    SELF_CHECK(result.restore_failure ==
               LibXRTest::I2cRegisterDeviceFailure::RESTORE_WRITE);
    SELF_CHECK(result.restore_error == LibXR::ErrorCode::FAILED);
    SELF_CHECK(result.restore_attempted);
    SELF_CHECK(!result.restore_verified);
    SELF_CHECK(result.device_state_uncertain);
    SELF_CHECK(fixture.i2c.BytesEqual(Fixture::kWritableRegister, fixture.payload));
  }

  {
    Fixture fixture;
    fixture.i2c.corrupt_read_call_ = 3U;
    fixture.i2c.corrupt_read_offset_ = 2U;
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::PAYLOAD_MISMATCH);
    SELF_CHECK(result.payload_mismatch_offset == 2U);
    SELF_CHECK(result.restore_verified);
    SELF_CHECK(
        fixture.i2c.BytesEqual(Fixture::kWritableRegister, fixture.original_value));
  }

  {
    Fixture fixture;
    fixture.i2c.fail_read_call_ = 6U;
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::REPEATED_READ);
    SELF_CHECK(result.failed_repeated_read == 1U);
    SELF_CHECK(result.completed_repeated_reads == 1U);
    SELF_CHECK(result.restore_verified);
    SELF_CHECK(!result.device_state_uncertain);
    SELF_CHECK(
        fixture.i2c.BytesEqual(Fixture::kWritableRegister, fixture.original_value));
  }

  {
    Fixture fixture;
    fixture.i2c.fail_read_call_ = 3U;
    fixture.i2c.read_failure_error_ = LibXR::ErrorCode::TIMEOUT;
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::READ_BACK);
    SELF_CHECK(result.error == LibXR::ErrorCode::TIMEOUT);
    SELF_CHECK(result.operation_retirement_unconfirmed);
    SELF_CHECK(result.restore_skipped_due_unconfirmed_operation);
    SELF_CHECK(!result.restore_attempted);
    SELF_CHECK(result.device_state_uncertain);
    SELF_CHECK(fixture.i2c.write_calls_ == 1U);
    SELF_CHECK(fixture.i2c.BytesEqual(Fixture::kWritableRegister, fixture.payload));
  }

  {
    Fixture fixture;
    fixture.i2c.SetBytes(Fixture::kWritableRegister, fixture.payload);
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure ==
               LibXRTest::I2cRegisterDeviceFailure::TEST_PAYLOAD_UNCHANGED);
    SELF_CHECK(fixture.i2c.write_calls_ == 0U);
  }

  {
    Fixture fixture;
    fixture.test_case.operation_timeout_ms = 0U;
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::INVALID_ARGUMENT);
    SELF_CHECK(result.error == LibXR::ErrorCode::ARG_ERR);
    SELF_CHECK(fixture.i2c.read_calls_ == 0U);
    SELF_CHECK(fixture.i2c.write_calls_ == 0U);
  }

  {
    Fixture fixture;
    fixture.test_case.write_payload =
        std::span<const uint8_t>(fixture.payload.data(), 3U);
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::INVALID_ARGUMENT);
    SELF_CHECK(result.error == LibXR::ErrorCode::SIZE_ERR);
    SELF_CHECK(fixture.i2c.read_calls_ == 0U);
  }

  {
    Fixture fixture;
    fixture.workspace.saved_writable_value = {fixture.payload.data(),
                                              fixture.payload.size()};
    const auto result = LibXRTest::RunI2cRegisterDeviceTest(
        fixture.i2c, fixture.semaphore, fixture.test_case, fixture.workspace);
    SELF_CHECK(result.failure == LibXRTest::I2cRegisterDeviceFailure::INVALID_ARGUMENT);
    SELF_CHECK(result.error == LibXR::ErrorCode::ARG_ERR);
    SELF_CHECK(fixture.i2c.read_calls_ == 0U);
  }

  SELF_CHECK(std::strcmp(LibXRTest::I2cRegisterDeviceFailureName(
                             LibXRTest::I2cRegisterDeviceFailure::DEVICE_NOT_FOUND),
                         "DEVICE_NOT_FOUND") == 0);
  SELF_CHECK(std::strcmp(LibXRTest::I2cRegisterDeviceFailureName(
                             LibXRTest::I2cRegisterDeviceFailure::RESTORE_MISMATCH),
                         "RESTORE_MISMATCH") == 0);

  std::puts("i2c register hardware-test support selftest: PASS");
  return 0;
}
