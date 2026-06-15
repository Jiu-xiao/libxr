/**
 * @file test_sequential_smoke.cpp
 * @brief linux file-backed `DatabaseRawSequential` smoke/save 场景子测试。 Split test unit for linux file-backed `DatabaseRawSequential` smoke/save scenarios.
 * @details 测试项目：
 *          1. 多 key sequential 数据库烟雾流量与重复 load/store。
 *          2. `Save()` 保存当前值语义。
 *          Test items:
 *          1. Multi-key sequential smoke traffic with repeated load/store.
 *          2. `Save()` persists the key object's current value.
 */
#include "linux_database_test_common.hpp"

namespace
{

using namespace LinuxDatabaseTestCommon;

void TestLinuxDatabaseSequentialSmoke()
{
  // 测试内容：验证 linux database sequential 在长时间多 key 读写流量下保持持久化一致性。
  // Test coverage: verify that the linux database sequential keeps persistence consistency under long-running multi-key traffic.
  constexpr size_t FLASH_SIZE = XR_DB_FLASH_SIZE;

  LinuxBinaryFileFlash<FLASH_SIZE> test_flash("/tmp/flash_test.bin", 512, 8, true, true);
  DatabaseRawSequential test_db(test_flash);

  std::array<uint32_t, 1> data_k1 = {1};
  std::array<uint32_t, 2> data_k2 = {11, 22};
  std::array<uint32_t, 3> data_k3 = {111, 222, 333};
  std::array<uint32_t, 4> data_k4 = {1111, 2222, 3333, 4444};

  DatabaseRawSequential::Key k1(test_db, "key1", data_k1);
  DatabaseRawSequential::Key k2(test_db, "key2", data_k2);
  DatabaseRawSequential::Key k3(test_db, "key3", data_k3);
  DatabaseRawSequential::Key k4(test_db, "key4", data_k4);

  for (int i = 0; i < 1000; i++)
  {
    k1 = data_k1;
    k2 = data_k2;
    k3 = data_k3;
    k4 = data_k4;

    k1.Load();
    k2.Load();
    k3.Load();
    k4.Load();

    ASSERT(std::memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
    ASSERT(std::memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
    ASSERT(std::memcmp(data_k3.data(), k3.data_.data(), sizeof(data_k3)) == 0);
    ASSERT(std::memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);

    for (int j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k4[1] = Thread::GetTime() + j;
      k4 = data_k4;
      k4.Load();
      ASSERT(std::memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);
    }

    for (int j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k1[0] = Thread::GetTime() + j;
      k1 = data_k1;
      k1.Load();
      ASSERT(std::memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
    }

    for (int j = 0; j < Thread::GetTime() % 100; ++j)
    {
      k1.Load();
      k2.Load();
      k3.Load();
      k4.Load();
      ASSERT(std::memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
      ASSERT(std::memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
      ASSERT(std::memcmp(data_k3.data(), k3.data_.data(), sizeof(data_k3)) == 0);
      ASSERT(std::memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);
    }

    for (int j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k2[0] = LibXR::Timebase::GetMicroseconds();
      data_k2[1] = LibXR::Timebase::GetMilliseconds();
      k2 = data_k2;
      k2.Load();
      ASSERT(std::memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
    }

    ASSERT(std::memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
    ASSERT(std::memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
    ASSERT(std::memcmp(data_k3.data(), k3.data_.data(), sizeof(data_k3)) == 0);
    ASSERT(std::memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);
  }
}

void TestDatabaseSequentialSaveCurrentValue()
{
  // 测试内容：验证 `Save()` 会把 key 对象当前缓冲区中的值持久化到 sequential backend。
  // Test coverage: verify that `Save()` persists the key object's current in-memory value to the sequential backend.
  const char* path = "/tmp/flash_test_seq_save_current.bin";
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, 512, 8, true, true);
  DatabaseRawSequential db(flash);
  db.Restore();

  DatabaseRawSequential::Key<uint32_t> key(db, "seq", 1);
  key.data_ = 2;
  ASSERT(key.Save() == ErrorCode::OK);
  ASSERT(ReopenSequentialDatabaseValue(path, 0, "seq") == 2);
}

}  // namespace

void RunLinuxDatabaseSequentialSmokeTests()
{
  TestLinuxDatabaseSequentialSmoke();
  TestDatabaseSequentialSaveCurrentValue();
}
