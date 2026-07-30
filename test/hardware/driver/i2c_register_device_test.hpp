#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "i2c.hpp"
#include "libxr_type.hpp"
#include "semaphore.hpp"

namespace LibXRTest
{

enum class I2cRegisterDeviceFailure : uint8_t
{
  NONE,
  INVALID_ARGUMENT,
  DEVICE_NOT_FOUND,
  IDENTITY_MISMATCH,
  SAVE_ORIGINAL,
  TEST_PAYLOAD_UNCHANGED,
  WRITE,
  READ_BACK,
  PAYLOAD_MISMATCH,
  RESTORE_WRITE,
  RESTORE_READ,
  RESTORE_MISMATCH,
  REPEATED_READ,
};

struct I2cRegisterDeviceTestCase
{
  uint16_t device_address = 0U;
  uint16_t identity_register = 0U;
  uint8_t expected_identity = 0U;
  uint16_t writable_register = 0U;
  std::span<const uint8_t> write_payload{};
  uint16_t continuous_read_register = 0U;
  size_t continuous_read_size = 0U;
  uint32_t repeated_read_count = 0U;
  uint32_t operation_timeout_ms = 0U;
  LibXR::I2C::MemAddrLength memory_address_length = LibXR::I2C::MemAddrLength::BYTE_8;
};

struct I2cRegisterDeviceTestWorkspace
{
  LibXR::RawData saved_writable_value{};
  LibXR::RawData io_buffer{};
};

struct I2cRegisterDeviceTestResult
{
  I2cRegisterDeviceFailure failure = I2cRegisterDeviceFailure::NONE;
  LibXR::ErrorCode error = LibXR::ErrorCode::OK;
  I2cRegisterDeviceFailure restore_failure = I2cRegisterDeviceFailure::NONE;
  LibXR::ErrorCode restore_error = LibXR::ErrorCode::OK;
  uint8_t observed_identity = 0U;
  uint32_t completed_repeated_reads = 0U;
  uint32_t failed_repeated_read = UINT32_MAX;
  uint64_t completed_multi_byte_reads = 0U;
  uint64_t completed_multi_byte_writes = 0U;
  uint64_t multi_byte_bytes_transferred = 0U;
  uint32_t repeated_read_checksum = 2166136261U;
  size_t payload_mismatch_offset = SIZE_MAX;
  size_t restore_mismatch_offset = SIZE_MAX;
  uint64_t elapsed_us = 0U;
  bool identity_read_completed = false;
  bool original_value_saved = false;
  bool restore_attempted = false;
  bool restore_verified = false;
  bool restore_skipped_due_unconfirmed_operation = false;
  bool operation_retirement_unconfirmed = false;
  bool device_state_uncertain = false;

  [[nodiscard]] bool Passed() const
  {
    return failure == I2cRegisterDeviceFailure::NONE && restore_verified;
  }
};

/**
 * @brief Validate one explicitly addressed I2C register device without scanning.
 *
 * The helper first reads exactly one identity byte. A failed identity transaction is
 * reported as DEVICE_NOT_FOUND, while a successful unexpected value is reported as
 * IDENTITY_MISMATCH. Neither case writes the device. The accepted device address is an
 * unshifted, non-reserved 7-bit address in the range 0x08..0x77.
 *
 * After identity validation, the helper saves the complete writable range, writes the
 * caller-selected payload, reads it back, and restores and verifies the saved bytes.
 * It then repeatedly reads the continuous register range and accumulates an FNV-1a
 * checksum without requiring live register values to remain constant. The write payload
 * and continuous read size must each be at least four bytes; with the STM32 I2C default
 * DMA threshold this exercises DMA while the identity access exercises polling.
 *
 * Every MemRead and MemWrite receives a dedicated BLOCK Operation with the finite
 * `operation_timeout_ms`. Calls are sequential and reuse `operation_semaphore` only
 * after the previous call returns. The I2C object must be dedicated to this test.
 *
 * The caller owns the semaphore, payload, and both workspace buffers. Their active
 * ranges must be pairwise disjoint and remain alive until the function returns. If
 * `operation_retirement_unconfirmed` is true, they and the I2C object must remain alive
 * and unused until the backend independently retires the timed-out transaction. The
 * helper deliberately performs no further I2C operation after such a timeout; when a
 * write may have started, restoration is marked skipped and device state is uncertain.
 * All other failures after a write attempt trigger one bounded restore and verification
 * attempt. The helper performs no dynamic allocation.
 *
 * Call only from task or main context after `PlatformInit()` and timebase setup.
 */
I2cRegisterDeviceTestResult RunI2cRegisterDeviceTest(
    LibXR::I2C& i2c, LibXR::Semaphore& operation_semaphore,
    const I2cRegisterDeviceTestCase& test_case, I2cRegisterDeviceTestWorkspace workspace);

const char* I2cRegisterDeviceFailureName(I2cRegisterDeviceFailure failure);

}  // namespace LibXRTest
