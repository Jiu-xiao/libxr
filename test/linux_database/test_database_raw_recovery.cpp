/**
 * @file test_raw_recovery.cpp
 * @brief linux file-backed `DatabaseRaw` recovery 场景子测试。 Split test unit for linux file-backed `DatabaseRaw` recovery scenarios.
 */
#include "linux_database_test_common.hpp"
#include "raw_database_test_groups.hpp"

namespace
{

using namespace LinuxDatabaseTestCommon;

/**
 * @brief 测试项函数 `TestDatabaseRawInvalidMainKeyMetadataReinitializes`。 Test-item function `TestDatabaseRawInvalidMainKeyMetadataReinitializes`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseRawInvalidMainKeyMetadataReinitializes()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  const char* path = "/tmp/flash_test_raw_invalid_main_key_metadata.bin";
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  MarkMainFirstKeyAsUninitialized(bytes);
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, 77) == 77);
  auto repaired = ReadAllBytes(path);
  ASSERT(ReadLe32(repaired, 0) == XR_DB_FLASH_HEADER);
  ASSERT(ReadLe32(repaired, XR_DB_CHECKSUM_OFFSET) == XR_DB_CHECKSUM);
}

/**
 * @brief 测试项函数 `TestDatabaseRawInvalidBackupMetadataDoesNotRestore`。 Test-item function `TestDatabaseRawInvalidBackupMetadataDoesNotRestore`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseRawInvalidBackupMetadataDoesNotRestore()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  const char* path = "/tmp/flash_test_raw_invalid_backup_metadata.bin";
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  MirrorMainBlockToBackup(bytes);
  CorruptBackupFirstKeyAvailableFlag(bytes);
  InvalidateMainChecksum(bytes);
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, 55) == 55);
  AssertMainValidBackupInvalid(path);
}

/**
 * @brief 测试项函数 `TestDatabaseRawRestoresFromValidBackup`。 Test-item function `TestDatabaseRawRestoresFromValidBackup`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseRawRestoresFromValidBackup()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  const char* path = "/tmp/flash_test_raw_restore_from_valid_backup.bin";
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  MirrorMainBlockToBackup(bytes);
  InvalidateMainChecksum(bytes);
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, 55) == 1234);
  AssertMainValidBackupInvalid(path);
}

/**
 * @brief 测试项函数 `TestDatabaseRawCorruptFirstKeySizeInMultiKeyDatabaseReinitializes`。 Test-item function `TestDatabaseRawCorruptFirstKeySizeInMultiKeyDatabaseReinitializes`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseRawCorruptFirstKeySizeInMultiKeyDatabaseReinitializes()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  const char* path = "/tmp/flash_test_raw_corrupt_first_key_size.bin";
  CreateTwoKeyDatabase(path);

  auto bytes = ReadAllBytes(path);
  CorruptMainFirstKeyRawInfo(bytes, 0x7FFFFFFFU);
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, 77, "key1") == 77);
  ASSERT(ReopenDatabaseValue(path, 88, "key2") == 88);
  auto repaired = ReadAllBytes(path);
  ASSERT(ReadLe32(repaired, 0) == XR_DB_FLASH_HEADER);
  ASSERT(ReadLe32(repaired, XR_DB_CHECKSUM_OFFSET) == XR_DB_CHECKSUM);
}

}  // namespace

/**
 * @brief 测试项函数 `RunLinuxDatabaseRawRecoveryTests`。 Test-item function `RunLinuxDatabaseRawRecoveryTests`.
 * @details 测试内容：执行当前分组里的 `DatabaseRaw` linux database 子场景。 Execute the grouped `DatabaseRaw` linux database sub-scenarios.
 *          测试原理：把 smoke / failure / recovery 三类路径拆开，避免一个原始大文件持续膨胀。 Split smoke, failure, and recovery paths so one raw monolithic file does not keep growing.
 */
void RunLinuxDatabaseRawRecoveryTests()
{
  TestDatabaseRawInvalidMainKeyMetadataReinitializes();
  TestDatabaseRawInvalidBackupMetadataDoesNotRestore();
  TestDatabaseRawRestoresFromValidBackup();
  TestDatabaseRawCorruptFirstKeySizeInMultiKeyDatabaseReinitializes();
}
