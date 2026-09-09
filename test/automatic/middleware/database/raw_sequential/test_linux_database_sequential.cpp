/**
 * @file test_linux_database_sequential.cpp
 * @brief DatabaseRawSequential 读写测试 / DatabaseRawSequential read and write tests.
 *
 * 检查多键反复更新和重新打开后的值，并确认 Flash 读、写、擦除失败会终止操作。
 * Check repeated key updates, persisted values and fatal handling of Flash read, write
 * and erase failures.
 */

#include "middleware/database/linux_database_test_common.hpp"
#include "test_assert.hpp"

namespace
{

using namespace LinuxDatabaseTestCommon;

void TestLinuxDatabaseSequentialSmoke()
{
  // 反复更新四个键并重新读取，检查写入一个键不会改变其他键的内容。
  // Repeatedly update and reload four keys; writing one must not change the others.
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

    TEST_ASSERT(std::memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
    TEST_ASSERT(std::memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
    TEST_ASSERT(std::memcmp(data_k3.data(), k3.data_.data(), sizeof(data_k3)) == 0);
    TEST_ASSERT(std::memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);

    for (int j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k4[1] = Thread::GetTime() + j;
      k4 = data_k4;
      k4.Load();
      TEST_ASSERT(std::memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);
    }

    for (int j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k1[0] = Thread::GetTime() + j;
      k1 = data_k1;
      k1.Load();
      TEST_ASSERT(std::memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
    }

    for (int j = 0; j < Thread::GetTime() % 100; ++j)
    {
      k1.Load();
      k2.Load();
      k3.Load();
      k4.Load();
      TEST_ASSERT(std::memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
      TEST_ASSERT(std::memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
      TEST_ASSERT(std::memcmp(data_k3.data(), k3.data_.data(), sizeof(data_k3)) == 0);
      TEST_ASSERT(std::memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);
    }

    for (int j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k2[0] = LibXR::Timebase::GetMicroseconds();
      data_k2[1] = LibXR::Timebase::GetMilliseconds();
      k2 = data_k2;
      k2.Load();
      TEST_ASSERT(std::memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
    }

    TEST_ASSERT(std::memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
    TEST_ASSERT(std::memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
    TEST_ASSERT(std::memcmp(data_k3.data(), k3.data_.data(), sizeof(data_k3)) == 0);
    TEST_ASSERT(std::memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);
  }
}

void TestDatabaseSequentialSaveCurrentValue()
{
  // 修改键的 data_ 后直接 Save，再重新打开文件，应该读到修改后的值。
  // Change data_, call Save, then reopen the file and expect the updated value.
  const char* path = "/tmp/flash_test_seq_save_current.bin";
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, 512, 8, true, true);
  DatabaseRawSequential db(flash);
  db.Restore();

  DatabaseRawSequential::Key<uint32_t> key(db, "seq", 1);
  key.data_ = 2;
  TEST_ASSERT(key.Save() == ErrorCode::OK);
  TEST_ASSERT(ReopenSequentialDatabaseValue(path, 0, "seq") == 2);
}

}  // namespace

void RunLinuxDatabaseSequentialSmokeTests()
{
  TestLinuxDatabaseSequentialSmoke();
  TestDatabaseSequentialSaveCurrentValue();
}

namespace
{

using namespace LinuxDatabaseTestCommon;

void TestDatabaseSequentialReadFailureRequires()
{
  // 让 Flash 读取失败，数据库应通过 fatal 回调退出。
  // Force a Flash read failure; the database must exit through the fatal callback.
  ExpectFatalExit(XR_DB_FATAL_SEQ_READ,
                  []
                  {
                    FailingFlash flash;
                    DatabaseRawSequential db(flash);
                    flash.SetFailOp(FailingFlash::FailOp::READ);
                    db.Load();
                  });
}

void TestDatabaseSequentialWriteFailureRequires()
{
  // 让 Flash 写入失败，检查数据库没有把失败当作成功继续运行。
  // Force a Flash write failure and check that the database does not continue as if it
  // succeeded.
  ExpectFatalExit(XR_DB_FATAL_SEQ_WRITE,
                  []
                  {
                    FailingFlash flash;
                    DatabaseRawSequential db(flash);
                    flash.SetFailOp(FailingFlash::FailOp::WRITE);
                    db.Restore();
                  });
}

void TestDatabaseSequentialEraseFailureRequires()
{
  // 让 Flash 擦除失败，检查进入规定的 fatal 退出路径。
  // Force a Flash erase failure and check the expected fatal exit.
  ExpectFatalExit(XR_DB_FATAL_SEQ_ERASE,
                  []
                  {
                    FailingFlash flash;
                    DatabaseRawSequential db(flash);
                    flash.SetFailOp(FailingFlash::FailOp::ERASE);
                    db.Save();
                  });
}

}  // namespace

void RunLinuxDatabaseSequentialFailureTests()
{
  TestDatabaseSequentialReadFailureRequires();
  TestDatabaseSequentialWriteFailureRequires();
  TestDatabaseSequentialEraseFailureRequires();
}

void test_linux_database_sequential()
{
  RunLinuxDatabaseSequentialSmokeTests();
  RunLinuxDatabaseSequentialFailureTests();
}
