/**
 * @file test_linux_database_raw.cpp
 * @brief DatabaseRaw 存储与恢复测试 / DatabaseRaw storage and recovery tests.
 *
 * 借助文件模拟 Flash，检查多键更新、重新打开、大小不匹配、损坏恢复和致命 I/O 失败。
 * Use file-backed Flash to check key updates, reopening, size mismatches, recovery and
 * fatal I/O failures.
 */

#include "middleware/database/linux_database_test_common.hpp"
#include "test_assert.hpp"

namespace
{

using namespace LinuxDatabaseTestCommon;

void TestLinuxDatabaseRawSmoke()
{
  constexpr size_t FLASH_SIZE = XR_DB_FLASH_SIZE;

  std::array<uint32_t, 1> data_k1 = {1};
  std::array<uint32_t, 2> data_k2 = {11, 22};
  std::array<uint32_t, 3> data_k3 = {111, 222, 333};
  std::array<uint32_t, 4> data_k4 = {1111, 2222, 3333, 4444};

  LinuxBinaryFileFlash<FLASH_SIZE> flash_2("/tmp/flash_test_2.bin", 512, 16, false, true);
  DatabaseRaw<16> test_db_2(flash_2, 5);

  DatabaseRaw<16>::Key k1_2(test_db_2, "key1", data_k1);
  DatabaseRaw<16>::Key k2_2(test_db_2, "keasdasy2", data_k2);
  DatabaseRaw<16>::Key k3_2(test_db_2, "keaasdasdy3", data_k3);
  DatabaseRaw<16>::Key k4_2(test_db_2, "keyaskdhasjh4", data_k4);

  data_k4[1] = 1234567;

  k1_2 = data_k1;
  k2_2 = data_k2;
  k3_2 = data_k3;
  k4_2 = data_k4;

  k1_2.Load();
  k2_2.Load();
  k3_2.Load();
  k4_2.Load();

  TEST_ASSERT(std::memcmp(&data_k1[0], &k1_2.data_[0], sizeof(data_k1)) == 0);
  TEST_ASSERT(std::memcmp(&data_k2[0], &k2_2.data_[0], sizeof(data_k2)) == 0);
  TEST_ASSERT(std::memcmp(&data_k3[0], &k3_2.data_[0], sizeof(data_k3)) == 0);
  TEST_ASSERT(std::memcmp(&data_k4[0], &k4_2.data_[0], sizeof(data_k4)) == 0);

  for (size_t i = 0; i < 1000; i++)
  {
    for (uint32_t j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k1[0] = Thread::GetTime() + j;
      k1_2 = data_k1;
    }
    for (uint32_t j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k2[0] = Thread::GetTime() + j;
      k2_2 = data_k2;
    }
    for (uint32_t j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k3[0] = Thread::GetTime() + j;
      k3_2 = data_k3;
    }
    for (uint32_t j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k4[0] = Thread::GetTime() + j;
      k4_2 = data_k4;
    }

    k1_2.Load();
    k2_2.Load();
    k3_2.Load();
    k4_2.Load();
    TEST_ASSERT(std::memcmp(&data_k1[0], &k1_2.data_[0], sizeof(data_k1)) == 0);
    TEST_ASSERT(std::memcmp(&data_k2[0], &k2_2.data_[0], sizeof(data_k2)) == 0);
    TEST_ASSERT(std::memcmp(&data_k3[0], &k3_2.data_[0], sizeof(data_k3)) == 0);
    TEST_ASSERT(std::memcmp(&data_k4[0], &k4_2.data_[0], sizeof(data_k4)) == 0);
  }
}

void TestDatabasePartialBackupRecovery()
{
  RunPartialBackupCase("/tmp/flash_test_partial_valid_main.bin", MainChecksum::VALID, 0,
                       1234);
  RunPartialBackupCase("/tmp/flash_test_partial_broken_main.bin", MainChecksum::INVALID,
                       55, 55);
}

void TestDatabaseRawSaveCurrentValue()
{
  const char* path = "/tmp/flash_test_raw_save_current.bin";
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  db.Restore();

  DatabaseRaw<16>::Key<uint32_t> key(db, "raw", 1);
  key.data_ = 2;
  TEST_ASSERT(key.Save() == ErrorCode::OK);
  TEST_ASSERT(ReopenDatabaseValue(path, 0, "raw") == 2);
}

void TestDatabaseRawRequiresExactStoredSize()
{
  // 按一种大小保存，再用另一种大小打开同名键，检查不把不同大小的数据直接当成同一类型。
  // Store one size and reopen the same key with another; do not treat differently sized
  // data as the same type.
  const char* path = "/tmp/flash_test_raw_exact_size.bin";
  {
    LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                                 XR_DB_MIN_WRITE_SIZE, false, true);
    DatabaseRaw<16> db(flash, 5);
    db.Restore();
    DatabaseRaw<16>::Key<uint32_t> key(db, "shape", 0x11223344U);
    TEST_ASSERT(key.data_ == 0x11223344U);
  }

  {
    LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                                 XR_DB_MIN_WRITE_SIZE, false, true);
    DatabaseRaw<16> db(flash, 5);
    DatabaseRaw<16>::Key<uint64_t> wider_key(db, "shape", 0ULL);
    TEST_ASSERT(wider_key.data_ == 0ULL);
    TEST_ASSERT(wider_key.Load() == ErrorCode::FAILED);
  }

  TEST_ASSERT(ReopenDatabaseValue(path, 0, "shape") == 0x11223344U);
}

}  // namespace

void RunLinuxDatabaseRawSmokeTests()
{
  TestLinuxDatabaseRawSmoke();
  TestDatabasePartialBackupRecovery();
  TestDatabaseRawSaveCurrentValue();
  TestDatabaseRawRequiresExactStoredSize();
}

namespace
{

using namespace LinuxDatabaseTestCommon;

void TestDatabaseKeyAddFailureRequires()
{
  ExpectFatalExit(XR_DB_FATAL_KEY_ADD,
                  []
                  {
                    class MemoryDatabase : public Database
                    {
                     public:
                      ErrorCode get_result = ErrorCode::NOT_FOUND;
                      ErrorCode add_result = ErrorCode::FAILED;

                      ErrorCode Get(KeyBase&) override { return get_result; }
                      ErrorCode Set(KeyBase&, RawData) override { return ErrorCode::OK; }
                      ErrorCode Add(KeyBase&) override { return add_result; }
                    } db;

                    Database::Key<uint32_t> key(db, "mock", 123);
                    UNUSED(key);
                  });
}

void TestDatabaseRawReadFailureRequires()
{
  ExpectFatalExit(XR_DB_FATAL_RAW_READ,
                  []
                  {
                    FailingFlash flash(XR_DB_MIN_ERASE_SIZE, XR_DB_MIN_WRITE_SIZE);
                    flash.SetFailOp(FailingFlash::FailOp::READ);
                    DatabaseRaw<XR_DB_MIN_WRITE_SIZE> db(flash, 5);
                    UNUSED(db);
                  });
}

void TestDatabaseRawWriteFailureRequires()
{
  ExpectFatalExit(XR_DB_FATAL_RAW_WRITE,
                  []
                  {
                    FailingFlash flash(XR_DB_MIN_ERASE_SIZE, XR_DB_MIN_WRITE_SIZE);
                    flash.SetFailOp(FailingFlash::FailOp::WRITE);
                    DatabaseRaw<XR_DB_MIN_WRITE_SIZE> db(flash, 5);
                    UNUSED(db);
                  });
}

void TestDatabaseRawEraseFailureRequires()
{
  ExpectFatalExit(XR_DB_FATAL_RAW_ERASE,
                  []
                  {
                    FailingFlash flash(XR_DB_MIN_ERASE_SIZE, XR_DB_MIN_WRITE_SIZE);
                    flash.SetFailOp(FailingFlash::FailOp::ERASE);
                    DatabaseRaw<XR_DB_MIN_WRITE_SIZE> db(flash, 5);
                    UNUSED(db);
                  });
}

}  // namespace

void RunLinuxDatabaseRawFailureTests()
{
  TestDatabaseKeyAddFailureRequires();
  TestDatabaseRawReadFailureRequires();
  TestDatabaseRawWriteFailureRequires();
  TestDatabaseRawEraseFailureRequires();
}

namespace
{

using namespace LinuxDatabaseTestCommon;

void TestDatabaseRawInvalidMainKeyMetadataReinitializes()
{
  // 损坏主区键元数据后重新打开，检查数据库重新初始化而不是使用损坏的键。
  // Reopen after corrupting main key metadata; check reinitialization instead of using
  // the corrupt key.
  const char* path = "/tmp/flash_test_raw_invalid_main_key_metadata.bin";
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  MarkMainFirstKeyAsUninitialized(bytes);
  WriteAllBytes(path, bytes);

  TEST_ASSERT(ReopenDatabaseValue(path, 77) == 77);
  auto repaired = ReadAllBytes(path);
  TEST_ASSERT(ReadLe32(repaired, 0) == XR_DB_FLASH_HEADER);
  TEST_ASSERT(ReadLe32(repaired, XR_DB_CHECKSUM_OFFSET) == XR_DB_CHECKSUM);
}

void TestDatabaseRawInvalidBackupMetadataDoesNotRestore()
{
  // 备份元数据损坏时，即使主区需要恢复，也不能采用这份备份。
  // A backup with corrupt metadata must not be used even when the main block needs
  // recovery.
  const char* path = "/tmp/flash_test_raw_invalid_backup_metadata.bin";
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  MirrorMainBlockToBackup(bytes);
  CorruptBackupFirstKeyAvailableFlag(bytes);
  InvalidateMainChecksum(bytes);
  WriteAllBytes(path, bytes);

  TEST_ASSERT(ReopenDatabaseValue(path, 55) == 55);
  AssertMainValidBackupInvalid(path);
}

void TestDatabaseRawRestoresFromValidBackup()
{
  // 主区校验损坏但备份完整时，应恢复原来的值，而不是使用调用者的默认值。
  // When the main checksum is corrupt but the backup is valid, restore the stored value
  // rather than the default.
  const char* path = "/tmp/flash_test_raw_restore_from_valid_backup.bin";
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  MirrorMainBlockToBackup(bytes);
  InvalidateMainChecksum(bytes);
  WriteAllBytes(path, bytes);

  TEST_ASSERT(ReopenDatabaseValue(path, 55) == 1234);
  AssertMainValidBackupInvalid(path);
}

void TestDatabaseRawCorruptFirstKeySizeInMultiKeyDatabaseReinitializes()
{
  // 把首个键的长度改成越界值，检查重新初始化后的两个键都使用默认值。
  // Make the first key length exceed the block; after reinitialization both keys must use
  // their defaults.
  const char* path = "/tmp/flash_test_raw_corrupt_first_key_size.bin";
  CreateTwoKeyDatabase(path);

  auto bytes = ReadAllBytes(path);
  CorruptMainFirstKeyRawInfo(bytes, 0x7FFFFFFFU);
  WriteAllBytes(path, bytes);

  TEST_ASSERT(ReopenDatabaseValue(path, 77, "key1") == 77);
  TEST_ASSERT(ReopenDatabaseValue(path, 88, "key2") == 88);
  auto repaired = ReadAllBytes(path);
  TEST_ASSERT(ReadLe32(repaired, 0) == XR_DB_FLASH_HEADER);
  TEST_ASSERT(ReadLe32(repaired, XR_DB_CHECKSUM_OFFSET) == XR_DB_CHECKSUM);
}

}  // namespace

void RunLinuxDatabaseRawRecoveryTests()
{
  TestDatabaseRawInvalidMainKeyMetadataReinitializes();
  TestDatabaseRawInvalidBackupMetadataDoesNotRestore();
  TestDatabaseRawRestoresFromValidBackup();
  TestDatabaseRawCorruptFirstKeySizeInMultiKeyDatabaseReinitializes();
}

void test_linux_database_raw()
{
  RunLinuxDatabaseRawSmokeTests();
  RunLinuxDatabaseRawFailureTests();
  RunLinuxDatabaseRawRecoveryTests();
}
