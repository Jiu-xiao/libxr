/**
 * @file database_binding_reopen_test_common.hpp
 * @brief database binding 测试共用 reopen/断言 helper。 Shared reopen/assertion helpers for database binding tests.
 * @details 共享职责：
 *          1. 通过公开 database API reopen 后读取持久化值。
 *          2. 校验 main 有效/backup 无效的持久化状态。
 *          3. 复用 partial-backup 场景执行 helper。
 *          Shared responsibilities:
 *          1. Reopen through public database APIs and read persisted values.
 *          2. Verify the persisted state where main is valid and backup is invalid.
 *          3. Reuse the partial-backup scenario execution helper.
 */
#pragma once

#include "database_binding_image_test_common.hpp"

namespace DatabaseBindingTestCommon
{

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
  // 辅助内容：为后续测试准备或校验共享状态。
  // Helper coverage: prepare or validate shared state for later tests.
  auto bytes = ReadAllBytes(path);
  ASSERT(ReadLe32(bytes, 0) == XR_DB_FLASH_HEADER);
  ASSERT(ReadLe32(bytes, XR_DB_CHECKSUM_OFFSET) == XR_DB_CHECKSUM);
  ASSERT(ReadLe32(bytes, XR_DB_BLOCK_SIZE + XR_DB_CHECKSUM_OFFSET) != XR_DB_CHECKSUM);
}

inline void RunPartialBackupCase(const char* path, MainChecksum main_checksum,
                                 uint32_t default_value, uint32_t expected_value)
{
  // 基准内容：执行当前子场景或 case。
  // Benchmark coverage: execute the current benchmark sub-case.
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
