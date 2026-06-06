#pragma once

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "database.hpp"
#include "libxr_def.hpp"
#include "linux_flash.hpp"
#include "test.hpp"

namespace DatabaseBindingTestCommon
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

[[nodiscard]] inline uint32_t ReadLe32(const std::vector<uint8_t>& bytes, size_t offset)
{
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

inline void WriteLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
  bytes[offset] = static_cast<uint8_t>(value & 0xFF);
  bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

template <typename Func>
void ExpectFatalExit(int exit_code, Func&& func)
{
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

[[nodiscard]] inline std::vector<uint8_t> ReadAllBytes(const char* path)
{
  std::ifstream file(path, std::ios::binary);
  ASSERT(static_cast<bool>(file));

  std::vector<uint8_t> bytes(XR_DB_FLASH_SIZE, 0);
  file.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  ASSERT(file.gcount() == static_cast<std::streamsize>(bytes.size()));
  return bytes;
}

inline void WriteAllBytes(const char* path, const std::vector<uint8_t>& bytes)
{
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  ASSERT(static_cast<bool>(file));
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  ASSERT(static_cast<bool>(file));
}

inline void CraftPartialBackup(std::vector<uint8_t>& bytes, size_t partial_len)
{
  ASSERT(partial_len < XR_DB_BLOCK_SIZE);

  const size_t backup_offset = XR_DB_BLOCK_SIZE;
  for (size_t i = 0; i < partial_len; ++i)
  {
    bytes[backup_offset + i] = bytes[i];
  }

  WriteLe32(bytes, backup_offset + XR_DB_CHECKSUM_OFFSET, XR_DB_CHECKSUM);
}

inline void MirrorMainBlockToBackup(std::vector<uint8_t>& bytes)
{
  for (size_t i = 0; i < XR_DB_BLOCK_SIZE; ++i)
  {
    bytes[XR_DB_BLOCK_SIZE + i] = bytes[i];
  }
}

inline void InvalidateMainChecksum(std::vector<uint8_t>& bytes)
{
  WriteLe32(bytes, XR_DB_CHECKSUM_OFFSET, 0);
}

inline void MarkMainFirstKeyAsUninitialized(std::vector<uint8_t>& bytes)
{
  bytes[XR_DB_RAW_UNINIT_FLAG_LAST_BYTE_OFFSET] = 0xFF;
}

inline void CorruptBackupFirstKeyAvailableFlag(std::vector<uint8_t>& bytes)
{
  bytes[XR_DB_BLOCK_SIZE + XR_DB_RAW_AVAILABLE_FLAG_OFFSET] = 0x00;
}

inline void CorruptMainFirstKeyRawInfo(std::vector<uint8_t>& bytes, uint32_t raw_info)
{
  WriteLe32(bytes, XR_DB_RAW_FIRST_KEY_RAW_INFO_OFFSET, raw_info);
}

inline void CreateSeedDatabase(const char* path)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  db.Restore();
  DatabaseRaw<16>::Key<uint32_t> key(db, "key", 1234);
  ASSERT(key.data_ == 1234);
}

inline void CreateTwoKeyDatabase(const char* path)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  db.Restore();
  DatabaseRaw<16>::Key<uint32_t> key1(db, "key1", 1111);
  DatabaseRaw<16>::Key<uint32_t> key2(db, "key2", 2222);
  ASSERT(key1.data_ == 1111);
  ASSERT(key2.data_ == 2222);
}

[[nodiscard]] inline uint32_t ReopenDatabaseValue(const char* path, uint32_t default_value)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  DatabaseRaw<16>::Key<uint32_t> key(db, "key", default_value);
  return key.data_;
}

[[nodiscard]] inline uint32_t ReopenDatabaseValue(const char* path, uint32_t default_value,
                                                  const char* key_name)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  DatabaseRaw<16>::Key<uint32_t> key(db, key_name, default_value);
  return key.data_;
}

[[nodiscard]] inline uint32_t ReopenSequentialDatabaseValue(const char* path,
                                                            uint32_t default_value,
                                                            const char* key_name)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, 512, 8, true, true);
  DatabaseRawSequential db(flash);
  DatabaseRawSequential::Key<uint32_t> key(db, key_name, default_value);
  return key.data_;
}

inline void AssertMainValidBackupInvalid(const char* path)
{
  auto bytes = ReadAllBytes(path);
  ASSERT(ReadLe32(bytes, 0) == XR_DB_FLASH_HEADER);
  ASSERT(ReadLe32(bytes, XR_DB_CHECKSUM_OFFSET) == XR_DB_CHECKSUM);
  ASSERT(ReadLe32(bytes, XR_DB_BLOCK_SIZE + XR_DB_CHECKSUM_OFFSET) != XR_DB_CHECKSUM);
}

inline void RunPartialBackupCase(const char* path, MainChecksum main_checksum,
                                 uint32_t default_value, uint32_t expected_value)
{
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  CraftPartialBackup(bytes, 128);
  if (main_checksum == MainChecksum::INVALID)
  {
    InvalidateMainChecksum(bytes);
  }
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, default_value) == expected_value);
  AssertMainValidBackupInvalid(path);
}

}  // namespace DatabaseBindingTestCommon
