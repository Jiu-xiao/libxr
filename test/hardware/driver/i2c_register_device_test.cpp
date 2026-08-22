#include "driver/i2c_register_device_test.hpp"

#include <array>
#include <cstring>
#include <limits>

#include "libxr.hpp"

namespace LibXRTest
{
namespace
{

struct ActiveBuffer
{
  const void* address = nullptr;
  size_t size = 0U;
};

bool ActiveBuffersArePairwiseDisjoint(std::span<const ActiveBuffer> buffers)
{
  std::array<uintptr_t, 3U> begins{};
  std::array<uintptr_t, 3U> ends{};
  if (buffers.size() > begins.size())
  {
    return false;
  }

  for (size_t i = 0U; i < buffers.size(); ++i)
  {
    if (buffers[i].address == nullptr || buffers[i].size == 0U)
    {
      return false;
    }

    begins[i] = reinterpret_cast<uintptr_t>(buffers[i].address);
    if (begins[i] > std::numeric_limits<uintptr_t>::max() - buffers[i].size)
    {
      return false;
    }
    ends[i] = begins[i] + buffers[i].size;
  }

  for (size_t i = 0U; i < buffers.size(); ++i)
  {
    for (size_t j = i + 1U; j < buffers.size(); ++j)
    {
      if (begins[i] < ends[j] && begins[j] < ends[i])
      {
        return false;
      }
    }
  }
  return true;
}

bool RegisterRangeFits(uint16_t start_register, size_t size,
                       LibXR::I2C::MemAddrLength address_length)
{
  const uint32_t maximum_register =
      address_length == LibXR::I2C::MemAddrLength::BYTE_8 ? UINT8_MAX : UINT16_MAX;
  return size > 0U && start_register <= maximum_register &&
         size - 1U <= maximum_register - start_register;
}

bool WritableRangeContainsIdentity(const I2cRegisterDeviceTestCase& test_case)
{
  const uint32_t writable_end =
      static_cast<uint32_t>(test_case.writable_register) +
      static_cast<uint32_t>(test_case.write_payload.size() - 1U);
  return test_case.identity_register >= test_case.writable_register &&
         test_case.identity_register <= writable_end;
}

size_t FindMismatch(const uint8_t* expected, const uint8_t* actual, size_t size)
{
  for (size_t i = 0U; i < size; ++i)
  {
    if (expected[i] != actual[i])
    {
      return i;
    }
  }
  return SIZE_MAX;
}

LibXR::ErrorCode ReadRegisterBlock(LibXR::I2C& i2c, LibXR::Semaphore& semaphore,
                                   uint16_t device_address, uint16_t register_address,
                                   LibXR::RawData output,
                                   LibXR::I2C::MemAddrLength address_length,
                                   uint32_t timeout_ms)
{
  LibXR::ReadOperation operation(semaphore, timeout_ms);
  return i2c.MemRead(device_address, register_address, output, operation, address_length,
                     false);
}

LibXR::ErrorCode WriteRegisterBlock(LibXR::I2C& i2c, LibXR::Semaphore& semaphore,
                                    uint16_t device_address, uint16_t register_address,
                                    LibXR::ConstRawData input,
                                    LibXR::I2C::MemAddrLength address_length,
                                    uint32_t timeout_ms)
{
  LibXR::WriteOperation operation(semaphore, timeout_ms);
  return i2c.MemWrite(device_address, register_address, input, operation, address_length,
                      false);
}

void SetFailure(I2cRegisterDeviceTestResult& result, I2cRegisterDeviceFailure failure,
                LibXR::ErrorCode error)
{
  result.failure = failure;
  result.error = error;
  if (error == LibXR::ErrorCode::TIMEOUT)
  {
    result.operation_retirement_unconfirmed = true;
  }
}

void SetRestoreFailure(I2cRegisterDeviceTestResult& result,
                       I2cRegisterDeviceFailure failure, LibXR::ErrorCode error)
{
  result.restore_failure = failure;
  result.restore_error = error;
  result.device_state_uncertain = true;
  if (error == LibXR::ErrorCode::TIMEOUT)
  {
    result.operation_retirement_unconfirmed = true;
  }
  if (result.failure == I2cRegisterDeviceFailure::NONE)
  {
    result.failure = failure;
    result.error = error;
  }
}

uint32_t UpdateFnv1a(uint32_t checksum, const uint8_t* data, size_t size)
{
  for (size_t i = 0U; i < size; ++i)
  {
    checksum ^= data[i];
    checksum *= 16777619U;
  }
  return checksum;
}

LibXR::ErrorCode ValidateArguments(const I2cRegisterDeviceTestCase& test_case,
                                   const I2cRegisterDeviceTestWorkspace& workspace)
{
  const bool address_length_valid =
      test_case.memory_address_length == LibXR::I2C::MemAddrLength::BYTE_8 ||
      test_case.memory_address_length == LibXR::I2C::MemAddrLength::BYTE_16;
  if (!address_length_valid || test_case.device_address < 0x08U ||
      test_case.device_address > 0x77U || test_case.write_payload.empty() ||
      test_case.continuous_read_size == 0U || test_case.repeated_read_count == 0U ||
      test_case.operation_timeout_ms == 0U ||
      test_case.operation_timeout_ms == UINT32_MAX)
  {
    return LibXR::ErrorCode::ARG_ERR;
  }

  if (test_case.write_payload.size() < 4U || test_case.continuous_read_size < 4U ||
      test_case.write_payload.size() > UINT16_MAX ||
      test_case.continuous_read_size > UINT16_MAX)
  {
    return LibXR::ErrorCode::SIZE_ERR;
  }

  if (!RegisterRangeFits(test_case.identity_register, 1U,
                         test_case.memory_address_length) ||
      !RegisterRangeFits(test_case.writable_register, test_case.write_payload.size(),
                         test_case.memory_address_length) ||
      !RegisterRangeFits(test_case.continuous_read_register,
                         test_case.continuous_read_size,
                         test_case.memory_address_length) ||
      WritableRangeContainsIdentity(test_case))
  {
    return LibXR::ErrorCode::ARG_ERR;
  }

  const size_t io_size = test_case.write_payload.size() > test_case.continuous_read_size
                             ? test_case.write_payload.size()
                             : test_case.continuous_read_size;
  if (workspace.saved_writable_value.size_ < test_case.write_payload.size() ||
      workspace.io_buffer.size_ < io_size)
  {
    return LibXR::ErrorCode::SIZE_ERR;
  }

  const std::array<ActiveBuffer, 3U> buffers = {
      ActiveBuffer{workspace.saved_writable_value.addr_, test_case.write_payload.size()},
      ActiveBuffer{workspace.io_buffer.addr_, io_size},
      ActiveBuffer{test_case.write_payload.data(), test_case.write_payload.size()},
  };
  return ActiveBuffersArePairwiseDisjoint(buffers) ? LibXR::ErrorCode::OK
                                                   : LibXR::ErrorCode::ARG_ERR;
}

}  // namespace

I2cRegisterDeviceTestResult RunI2cRegisterDeviceTest(
    LibXR::I2C& i2c, LibXR::Semaphore& operation_semaphore,
    const I2cRegisterDeviceTestCase& test_case, I2cRegisterDeviceTestWorkspace workspace)
{
  I2cRegisterDeviceTestResult result;
  const uint64_t start_us = static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds());
  const auto Finish = [&result, start_us]()
  {
    result.elapsed_us =
        static_cast<uint64_t>(LibXR::Timebase::GetMicroseconds()) - start_us;
    return result;
  };

  const LibXR::ErrorCode validation_error = ValidateArguments(test_case, workspace);
  if (validation_error != LibXR::ErrorCode::OK)
  {
    SetFailure(result, I2cRegisterDeviceFailure::INVALID_ARGUMENT, validation_error);
    return Finish();
  }

  auto* const saved_value = static_cast<uint8_t*>(workspace.saved_writable_value.addr_);
  auto* const io_buffer = static_cast<uint8_t*>(workspace.io_buffer.addr_);
  const auto* const write_payload = test_case.write_payload.data();
  const size_t write_size = test_case.write_payload.size();

  LibXR::ErrorCode operation_error = ReadRegisterBlock(
      i2c, operation_semaphore, test_case.device_address, test_case.identity_register,
      {io_buffer, 1U}, test_case.memory_address_length, test_case.operation_timeout_ms);
  if (operation_error != LibXR::ErrorCode::OK)
  {
    SetFailure(result, I2cRegisterDeviceFailure::DEVICE_NOT_FOUND, operation_error);
    return Finish();
  }

  result.identity_read_completed = true;
  result.observed_identity = io_buffer[0];
  if (result.observed_identity != test_case.expected_identity)
  {
    SetFailure(result, I2cRegisterDeviceFailure::IDENTITY_MISMATCH,
               LibXR::ErrorCode::CHECK_ERR);
    return Finish();
  }

  operation_error =
      ReadRegisterBlock(i2c, operation_semaphore, test_case.device_address,
                        test_case.writable_register, {saved_value, write_size},
                        test_case.memory_address_length, test_case.operation_timeout_ms);
  if (operation_error != LibXR::ErrorCode::OK)
  {
    SetFailure(result, I2cRegisterDeviceFailure::SAVE_ORIGINAL, operation_error);
    return Finish();
  }
  result.original_value_saved = true;
  result.completed_multi_byte_reads++;
  result.multi_byte_bytes_transferred += write_size;

  if (std::memcmp(saved_value, write_payload, write_size) == 0)
  {
    SetFailure(result, I2cRegisterDeviceFailure::TEST_PAYLOAD_UNCHANGED,
               LibXR::ErrorCode::ARG_ERR);
    return Finish();
  }

  const auto RestoreOriginal = [&]()
  {
    result.restore_attempted = true;
    LibXR::ErrorCode restore_error = WriteRegisterBlock(
        i2c, operation_semaphore, test_case.device_address, test_case.writable_register,
        {saved_value, write_size}, test_case.memory_address_length,
        test_case.operation_timeout_ms);
    if (restore_error != LibXR::ErrorCode::OK)
    {
      SetRestoreFailure(result, I2cRegisterDeviceFailure::RESTORE_WRITE, restore_error);
      return;
    }
    result.completed_multi_byte_writes++;
    result.multi_byte_bytes_transferred += write_size;

    restore_error = ReadRegisterBlock(
        i2c, operation_semaphore, test_case.device_address, test_case.writable_register,
        {io_buffer, write_size}, test_case.memory_address_length,
        test_case.operation_timeout_ms);
    if (restore_error != LibXR::ErrorCode::OK)
    {
      SetRestoreFailure(result, I2cRegisterDeviceFailure::RESTORE_READ, restore_error);
      return;
    }
    result.completed_multi_byte_reads++;
    result.multi_byte_bytes_transferred += write_size;

    result.restore_mismatch_offset = FindMismatch(saved_value, io_buffer, write_size);
    if (result.restore_mismatch_offset != SIZE_MAX)
    {
      SetRestoreFailure(result, I2cRegisterDeviceFailure::RESTORE_MISMATCH,
                        LibXR::ErrorCode::CHECK_ERR);
      return;
    }

    result.restore_verified = true;
    result.device_state_uncertain = false;
  };

  result.device_state_uncertain = true;
  operation_error =
      WriteRegisterBlock(i2c, operation_semaphore, test_case.device_address,
                         test_case.writable_register, {write_payload, write_size},
                         test_case.memory_address_length, test_case.operation_timeout_ms);
  if (operation_error != LibXR::ErrorCode::OK)
  {
    SetFailure(result, I2cRegisterDeviceFailure::WRITE, operation_error);
    if (result.operation_retirement_unconfirmed)
    {
      result.restore_skipped_due_unconfirmed_operation = true;
    }
    else
    {
      RestoreOriginal();
    }
    return Finish();
  }
  result.completed_multi_byte_writes++;
  result.multi_byte_bytes_transferred += write_size;

  operation_error =
      ReadRegisterBlock(i2c, operation_semaphore, test_case.device_address,
                        test_case.writable_register, {io_buffer, write_size},
                        test_case.memory_address_length, test_case.operation_timeout_ms);
  if (operation_error != LibXR::ErrorCode::OK)
  {
    SetFailure(result, I2cRegisterDeviceFailure::READ_BACK, operation_error);
    if (result.operation_retirement_unconfirmed)
    {
      result.restore_skipped_due_unconfirmed_operation = true;
    }
    else
    {
      RestoreOriginal();
    }
    return Finish();
  }
  result.completed_multi_byte_reads++;
  result.multi_byte_bytes_transferred += write_size;

  result.payload_mismatch_offset = FindMismatch(write_payload, io_buffer, write_size);
  if (result.payload_mismatch_offset != SIZE_MAX)
  {
    SetFailure(result, I2cRegisterDeviceFailure::PAYLOAD_MISMATCH,
               LibXR::ErrorCode::CHECK_ERR);
    RestoreOriginal();
    return Finish();
  }

  RestoreOriginal();
  if (!result.restore_verified)
  {
    return Finish();
  }

  for (uint32_t repeat = 0U; repeat < test_case.repeated_read_count; ++repeat)
  {
    operation_error = ReadRegisterBlock(
        i2c, operation_semaphore, test_case.device_address,
        test_case.continuous_read_register, {io_buffer, test_case.continuous_read_size},
        test_case.memory_address_length, test_case.operation_timeout_ms);
    if (operation_error != LibXR::ErrorCode::OK)
    {
      result.failed_repeated_read = repeat;
      SetFailure(result, I2cRegisterDeviceFailure::REPEATED_READ, operation_error);
      return Finish();
    }

    result.completed_multi_byte_reads++;
    result.completed_repeated_reads++;
    result.multi_byte_bytes_transferred += test_case.continuous_read_size;
    result.repeated_read_checksum = UpdateFnv1a(result.repeated_read_checksum, io_buffer,
                                                test_case.continuous_read_size);
  }

  return Finish();
}

const char* I2cRegisterDeviceFailureName(I2cRegisterDeviceFailure failure)
{
  switch (failure)
  {
    case I2cRegisterDeviceFailure::NONE:
      return "NONE";
    case I2cRegisterDeviceFailure::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case I2cRegisterDeviceFailure::DEVICE_NOT_FOUND:
      return "DEVICE_NOT_FOUND";
    case I2cRegisterDeviceFailure::IDENTITY_MISMATCH:
      return "IDENTITY_MISMATCH";
    case I2cRegisterDeviceFailure::SAVE_ORIGINAL:
      return "SAVE_ORIGINAL";
    case I2cRegisterDeviceFailure::TEST_PAYLOAD_UNCHANGED:
      return "TEST_PAYLOAD_UNCHANGED";
    case I2cRegisterDeviceFailure::WRITE:
      return "WRITE";
    case I2cRegisterDeviceFailure::READ_BACK:
      return "READ_BACK";
    case I2cRegisterDeviceFailure::PAYLOAD_MISMATCH:
      return "PAYLOAD_MISMATCH";
    case I2cRegisterDeviceFailure::RESTORE_WRITE:
      return "RESTORE_WRITE";
    case I2cRegisterDeviceFailure::RESTORE_READ:
      return "RESTORE_READ";
    case I2cRegisterDeviceFailure::RESTORE_MISMATCH:
      return "RESTORE_MISMATCH";
    case I2cRegisterDeviceFailure::REPEATED_READ:
      return "REPEATED_READ";
  }
  return "UNKNOWN";
}

}  // namespace LibXRTest
