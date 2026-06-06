#include "database_binding_test_common.hpp"

namespace
{

using namespace DatabaseBindingTestCommon;

void TestDatabaseBindingRawSmoke()
{
  constexpr size_t FLASH_SIZE = XR_DB_FLASH_SIZE;

  std::array<uint32_t, 1> data_k1 = {1};
  std::array<uint32_t, 2> data_k2 = {11, 22};
  std::array<uint32_t, 3> data_k3 = {111, 222, 333};
  std::array<uint32_t, 4> data_k4 = {1111, 2222, 3333, 4444};

  LinuxBinaryFileFlash<FLASH_SIZE> flash_2("/tmp/flash_test_2.bin", 512, 16, false,
                                           true);
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

  ASSERT(std::memcmp(&data_k1[0], &k1_2.data_[0], sizeof(data_k1)) == 0);
  ASSERT(std::memcmp(&data_k2[0], &k2_2.data_[0], sizeof(data_k2)) == 0);
  ASSERT(std::memcmp(&data_k3[0], &k3_2.data_[0], sizeof(data_k3)) == 0);
  ASSERT(std::memcmp(&data_k4[0], &k4_2.data_[0], sizeof(data_k4)) == 0);

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
    ASSERT(std::memcmp(&data_k1[0], &k1_2.data_[0], sizeof(data_k1)) == 0);
    ASSERT(std::memcmp(&data_k2[0], &k2_2.data_[0], sizeof(data_k2)) == 0);
    ASSERT(std::memcmp(&data_k3[0], &k3_2.data_[0], sizeof(data_k3)) == 0);
    ASSERT(std::memcmp(&data_k4[0], &k4_2.data_[0], sizeof(data_k4)) == 0);
  }
}

void TestDatabasePartialBackupRecovery()
{
  RunPartialBackupCase("/tmp/flash_test_partial_valid_main.bin", MainChecksum::VALID, 0,
                       1234);
  RunPartialBackupCase("/tmp/flash_test_partial_broken_main.bin", MainChecksum::INVALID,
                       55, 55);
}

void TestDatabaseKeyAddFailureRequires()
{
#if defined(LIBXR_SYSTEM_Linux)
  ExpectFatalExit(
      XR_DB_FATAL_KEY_ADD,
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
#endif
}

void TestDatabaseRawReadFailureRequires()
{
#if defined(LIBXR_SYSTEM_Linux)
  ExpectFatalExit(
      XR_DB_FATAL_RAW_READ,
      []
      {
        FailingFlash flash(XR_DB_MIN_ERASE_SIZE, XR_DB_MIN_WRITE_SIZE);
        flash.SetFailOp(FailingFlash::FailOp::READ);
        DatabaseRaw<XR_DB_MIN_WRITE_SIZE> db(flash, 5);
        UNUSED(db);
      });
#endif
}

void TestDatabaseRawWriteFailureRequires()
{
#if defined(LIBXR_SYSTEM_Linux)
  ExpectFatalExit(
      XR_DB_FATAL_RAW_WRITE,
      []
      {
        FailingFlash flash(XR_DB_MIN_ERASE_SIZE, XR_DB_MIN_WRITE_SIZE);
        flash.SetFailOp(FailingFlash::FailOp::WRITE);
        DatabaseRaw<XR_DB_MIN_WRITE_SIZE> db(flash, 5);
        UNUSED(db);
      });
#endif
}

void TestDatabaseRawEraseFailureRequires()
{
#if defined(LIBXR_SYSTEM_Linux)
  ExpectFatalExit(
      XR_DB_FATAL_RAW_ERASE,
      []
      {
        FailingFlash flash(XR_DB_MIN_ERASE_SIZE, XR_DB_MIN_WRITE_SIZE);
        flash.SetFailOp(FailingFlash::FailOp::ERASE);
        DatabaseRaw<XR_DB_MIN_WRITE_SIZE> db(flash, 5);
        UNUSED(db);
      });
#endif
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
  ASSERT(key.Save() == ErrorCode::OK);
  ASSERT(ReopenDatabaseValue(path, 0, "raw") == 2);
}

void TestDatabaseRawRequiresExactStoredSize()
{
  const char* path = "/tmp/flash_test_raw_exact_size.bin";
  {
    LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                                 XR_DB_MIN_WRITE_SIZE, false, true);
    DatabaseRaw<16> db(flash, 5);
    db.Restore();
    DatabaseRaw<16>::Key<uint32_t> key(db, "shape", 0x11223344U);
    ASSERT(key.data_ == 0x11223344U);
  }

  {
    LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                                 XR_DB_MIN_WRITE_SIZE, false, true);
    DatabaseRaw<16> db(flash, 5);
    DatabaseRaw<16>::Key<uint64_t> wider_key(db, "shape", 0ULL);
    ASSERT(wider_key.data_ == 0ULL);
    ASSERT(wider_key.Load() == ErrorCode::FAILED);
  }

  ASSERT(ReopenDatabaseValue(path, 0, "shape") == 0x11223344U);
}

void TestDatabaseRawInvalidMainKeyMetadataReinitializes()
{
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

void TestDatabaseRawInvalidBackupMetadataDoesNotRestore()
{
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

void TestDatabaseRawRestoresFromValidBackup()
{
  const char* path = "/tmp/flash_test_raw_restore_from_valid_backup.bin";
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  MirrorMainBlockToBackup(bytes);
  InvalidateMainChecksum(bytes);
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, 55) == 1234);
  AssertMainValidBackupInvalid(path);
}

void TestDatabaseRawCorruptFirstKeySizeInMultiKeyDatabaseReinitializes()
{
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

void test_database_binding_raw()
{
  TestDatabaseBindingRawSmoke();
  TestDatabasePartialBackupRecovery();
  TestDatabaseKeyAddFailureRequires();
  TestDatabaseRawReadFailureRequires();
  TestDatabaseRawWriteFailureRequires();
  TestDatabaseRawEraseFailureRequires();
  TestDatabaseRawSaveCurrentValue();
  TestDatabaseRawRequiresExactStoredSize();
  TestDatabaseRawInvalidMainKeyMetadataReinitializes();
  TestDatabaseRawInvalidBackupMetadataDoesNotRestore();
  TestDatabaseRawRestoresFromValidBackup();
  TestDatabaseRawCorruptFirstKeySizeInMultiKeyDatabaseReinitializes();
}
