/**
 * @file test_raw_smoke.cpp
 * @brief linux file-backed `DatabaseRaw` smoke/save 场景子测试。 Split test unit for linux file-backed `DatabaseRaw` smoke/save scenarios.
 */
#include "linux_database_test_common.hpp"
#include "raw_database_test_groups.hpp"

namespace
{

using namespace LinuxDatabaseTestCommon;

/**
 * @brief 测试项函数 `TestLinuxDatabaseRawSmoke`。 Test-item function `TestLinuxDatabaseRawSmoke`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestLinuxDatabaseRawSmoke()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试项函数 `TestDatabasePartialBackupRecovery`。 Test-item function `TestDatabasePartialBackupRecovery`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabasePartialBackupRecovery()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  RunPartialBackupCase("/tmp/flash_test_partial_valid_main.bin", MainChecksum::VALID, 0,
                       1234);
  RunPartialBackupCase("/tmp/flash_test_partial_broken_main.bin", MainChecksum::INVALID,
                       55, 55);
}

/**
 * @brief 测试项函数 `TestDatabaseRawSaveCurrentValue`。 Test-item function `TestDatabaseRawSaveCurrentValue`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseRawSaveCurrentValue()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试项函数 `TestDatabaseRawRequiresExactStoredSize`。 Test-item function `TestDatabaseRawRequiresExactStoredSize`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseRawRequiresExactStoredSize()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

}  // namespace

/**
 * @brief 测试项函数 `RunLinuxDatabaseRawSmokeTests`。 Test-item function `RunLinuxDatabaseRawSmokeTests`.
 * @details 测试内容：执行当前分组里的 `DatabaseRaw` linux database 子场景。 Execute the grouped `DatabaseRaw` linux database sub-scenarios.
 *          测试原理：把 smoke / failure / recovery 三类路径拆开，避免一个原始大文件持续膨胀。 Split smoke, failure, and recovery paths so one raw monolithic file does not keep growing.
 */
void RunLinuxDatabaseRawSmokeTests()
{
  TestLinuxDatabaseRawSmoke();
  TestDatabasePartialBackupRecovery();
  TestDatabaseRawSaveCurrentValue();
  TestDatabaseRawRequiresExactStoredSize();
}
