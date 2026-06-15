/**
 * @file linux_database_flash_test_common.hpp
 * @brief linux database 测试共用 flash/fatal helper。 Shared flash and fatal-path helpers for linux database tests.
 * @details 共享职责：
 *          1. 定义 flash 布局常量和 main/backup 校验元数据常量。
 *          2. 提供可注入读写擦失败的 `FailingFlash`。
 *          3. 提供 Linux 下的 fatal-exit 断言 helper。
 *          Shared responsibilities:
 *          1. Define flash-layout and main/backup checksum metadata constants.
 *          2. Provide `FailingFlash` with injectable read/write/erase failures.
 *          3. Provide Linux fatal-exit assertion helpers.
 */
#pragma once

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "database.hpp"
#include "libxr_def.hpp"
#include "linux_flash.hpp"
#include "test.hpp"

namespace LinuxDatabaseTestCommon
{

using namespace LibXR;

constexpr size_t XR_DB_FLASH_SIZE = 4096;
constexpr size_t XR_DB_MIN_ERASE_SIZE = 512;
constexpr size_t XR_DB_MIN_WRITE_SIZE = 16;
constexpr size_t XR_DB_BLOCK_SIZE = XR_DB_FLASH_SIZE / 2;
constexpr size_t XR_DB_CHECKSUM_OFFSET = XR_DB_BLOCK_SIZE - XR_DB_MIN_WRITE_SIZE;
constexpr uint32_t XR_DB_FLASH_HEADER = 0x12345678 + LIBXR_DATABASE_VERSION;
constexpr uint32_t XR_DB_CHECKSUM = 0x9abcedf0;
constexpr uint8_t XR_DB_SEQ_CHECKSUM = 0x56;
constexpr size_t XR_DB_RAW_KEYINFO_ALIGNED_SIZE = XR_DB_MIN_WRITE_SIZE * 4;
constexpr size_t XR_DB_RAW_SENTINEL_KEY_OFFSET = XR_DB_MIN_WRITE_SIZE;
constexpr size_t XR_DB_RAW_FIRST_KEY_OFFSET =
    XR_DB_RAW_SENTINEL_KEY_OFFSET + XR_DB_RAW_KEYINFO_ALIGNED_SIZE;
constexpr size_t XR_DB_RAW_AVAILABLE_FLAG_OFFSET =
    XR_DB_RAW_FIRST_KEY_OFFSET + XR_DB_MIN_WRITE_SIZE;
constexpr size_t XR_DB_RAW_UNINIT_FLAG_LAST_BYTE_OFFSET =
    XR_DB_RAW_FIRST_KEY_OFFSET + XR_DB_MIN_WRITE_SIZE * 3 - 1;
constexpr size_t XR_DB_RAW_FIRST_KEY_RAW_INFO_OFFSET =
    XR_DB_RAW_FIRST_KEY_OFFSET + XR_DB_MIN_WRITE_SIZE * 3;
constexpr int XR_DB_FATAL_KEY_ADD = 97;
constexpr int XR_DB_FATAL_SEQ_READ = 91;
constexpr int XR_DB_FATAL_SEQ_WRITE = 92;
constexpr int XR_DB_FATAL_SEQ_ERASE = 93;
constexpr int XR_DB_FATAL_RAW_READ = 94;
constexpr int XR_DB_FATAL_RAW_WRITE = 95;
constexpr int XR_DB_FATAL_RAW_ERASE = 96;

enum class MainChecksum
{
  VALID,
  INVALID,
};

class FailingFlash : public Flash
{
 public:
  enum class FailOp
  {
    NONE,
    READ,
    WRITE,
    ERASE,
  };

  explicit FailingFlash(size_t min_erase_size = 512, size_t min_write_size = 8,
                        size_t sequential_buffer_size = 256)
      : Flash(min_erase_size, min_write_size,
              RawData(flash_area_.data(), flash_area_.size())),
        sequential_buffer_size_(sequential_buffer_size)
  {
    SeedValidSequentialBlocks();
  }

  void SetFailOp(FailOp op) { fail_op_ = op; }

  ErrorCode Erase(size_t offset, size_t size) override
  {
    if (fail_op_ == FailOp::ERASE)
    {
      return ErrorCode::FAILED;
    }
    ASSERT(offset + size <= flash_area_.size());
    std::memset(flash_area_.data() + offset, 0xFF, size);
    return ErrorCode::OK;
  }

  ErrorCode Write(size_t offset, ConstRawData data) override
  {
    if (fail_op_ == FailOp::WRITE)
    {
      return ErrorCode::FAILED;
    }
    ASSERT(offset + data.size_ <= flash_area_.size());
    std::memcpy(flash_area_.data() + offset, data.addr_, data.size_);
    return ErrorCode::OK;
  }

  ErrorCode Read(size_t offset, RawData data) override
  {
    if (fail_op_ == FailOp::READ)
    {
      return ErrorCode::FAILED;
    }
    return Flash::Read(offset, data);
  }

 private:
  /**
   * @brief 辅助函数 `SeedValidSequentialBlocks`。 Helper function `SeedValidSequentialBlocks`.
   * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
   *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
   */
  void SeedValidSequentialBlocks()
  {
    std::memset(flash_area_.data(), 0xFF, flash_area_.size());

    const size_t block_size = flash_area_.size() / 2;
    const uint32_t empty_key = 0;

    std::memcpy(flash_area_.data(), &XR_DB_FLASH_HEADER, sizeof(XR_DB_FLASH_HEADER));
    std::memcpy(flash_area_.data() + sizeof(XR_DB_FLASH_HEADER), &empty_key,
                sizeof(empty_key));
    flash_area_[sequential_buffer_size_ - 1] = XR_DB_SEQ_CHECKSUM;

    std::memcpy(flash_area_.data() + block_size, &XR_DB_FLASH_HEADER,
                sizeof(XR_DB_FLASH_HEADER));
    std::memcpy(flash_area_.data() + block_size + sizeof(XR_DB_FLASH_HEADER), &empty_key,
                sizeof(empty_key));
    flash_area_[block_size + sequential_buffer_size_ - 1] = XR_DB_SEQ_CHECKSUM;
  }

  std::array<uint8_t, XR_DB_FLASH_SIZE> flash_area_{};
  size_t sequential_buffer_size_ = 0;
  FailOp fail_op_ = FailOp::NONE;
};

template <typename Func>
void ExpectFatalExit(int exit_code, Func&& func)
{
  // 辅助内容：验证当前失败或退出预期。
  // Helper coverage: validate the current expected failure or exit condition.
  pid_t child = fork();
  ASSERT(child >= 0);

  if (child == 0)
  {
    auto cb = LibXR::Assert::FatalCallback::Create(
        [](bool in_isr, int code, const char*, uint32_t)
        {
          UNUSED(in_isr);
          _exit(code);
        },
        exit_code);
    LibXR::Assert::RegisterFatalErrorCallback(cb);
    func();
    _exit(0);
  }

  int status = 0;
  ASSERT(waitpid(child, &status, 0) == child);
  ASSERT(WIFEXITED(status));
  ASSERT(WEXITSTATUS(status) == exit_code);
}

}  // namespace LinuxDatabaseTestCommon
