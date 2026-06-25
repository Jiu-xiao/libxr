/**
 * @file linux_database_image_test_common.hpp
 * @brief linux database 测试共用 flash 映像注入 helper。 Shared flash-image mutation helpers for linux database tests.
 * @details 共享职责：
 *          1. 读取/写回原始 flash 映像并注入 main/backup 损坏。
 *          2. 生成 seed 数据库与双 key 数据库。
 *          3. 为 reopen helper 提供原始 bytes 读写和损坏注入基础。
 *          Shared responsibilities:
 *          1. Read/write raw flash images and inject main/backup corruption.
 *          2. Generate seed databases and two-key databases.
 *          3. Provide the raw bytes and mutation foundation for reopen helpers.
 */
#pragma once

#include <fstream>
#include <vector>

#include "linux_database_flash_test_common.hpp"

namespace LinuxDatabaseTestCommon
{

[[nodiscard]] inline uint32_t ReadLe32(const std::vector<uint8_t>& bytes, size_t offset)
{
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

/**
 * @brief 辅助函数 `WriteLe32`。 Helper function `WriteLe32`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline void WriteLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  bytes[offset] = static_cast<uint8_t>(value & 0xFF);
  bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
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

/**
 * @brief 辅助函数 `WriteAllBytes`。 Helper function `WriteAllBytes`.
 * @details 测试内容：为后续测试准备、转换、统计或校验共享状态。 Prepare, transform, measure, or validate shared state for later test steps.
 *          测试原理：把重复辅助逻辑局部封装，保持测试主体聚焦在测试项本身。 Encapsulate repeated helper logic locally so the main test body stays focused on the test item itself.
 */
inline void WriteAllBytes(const char* path, const std::vector<uint8_t>& bytes)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  ASSERT(static_cast<bool>(file));
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  ASSERT(static_cast<bool>(file));
}

inline void CraftPartialBackup(std::vector<uint8_t>& bytes, size_t partial_len)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
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
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  for (size_t i = 0; i < XR_DB_BLOCK_SIZE; ++i)
  {
    bytes[XR_DB_BLOCK_SIZE + i] = bytes[i];
  }
}

inline void InvalidateMainChecksum(std::vector<uint8_t>& bytes)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  WriteLe32(bytes, XR_DB_CHECKSUM_OFFSET, 0);
}

inline void MarkMainFirstKeyAsUninitialized(std::vector<uint8_t>& bytes)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  bytes[XR_DB_RAW_UNINIT_FLAG_LAST_BYTE_OFFSET] = 0xFF;
}

inline void CorruptBackupFirstKeyAvailableFlag(std::vector<uint8_t>& bytes)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  bytes[XR_DB_BLOCK_SIZE + XR_DB_RAW_AVAILABLE_FLAG_OFFSET] = 0x00;
}

inline void CorruptMainFirstKeyRawInfo(std::vector<uint8_t>& bytes, uint32_t raw_info)
{
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
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

}  // namespace LinuxDatabaseTestCommon
