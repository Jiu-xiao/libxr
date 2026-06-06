/**
 * @file test_sequential.cpp
 * @brief Binding-plane tests for `DatabaseRawSequential`.
 *
 * Test items:
 * 1. Sequential smoke traffic: verify repeated multi-key save/load cycles preserve data through the sequential file-backed binding.
 * 2. Save-current-value behavior: verify `Save()` persists the key object's current value.
 * 3. Failure semantics: verify sequential read/write/erase backend failures take the expected fatal path.
 *
 * Test principle:
 * 1. Stress the real sequential database over the Linux flash binding so the test covers binding-plane persistence, not a mock backend.
 * 2. Combine long-running smoke traffic with fatal-path probes so both steady-state and hard-failure contracts are documented.
 */
#include "database_binding_test_common.hpp"

namespace
{

using namespace DatabaseBindingTestCommon;

void TestDatabaseBindingSequentialSmoke()
{
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
  const char* path = "/tmp/flash_test_seq_save_current.bin";
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, 512, 8, true, true);
  DatabaseRawSequential db(flash);
  db.Restore();

  DatabaseRawSequential::Key<uint32_t> key(db, "seq", 1);
  key.data_ = 2;
  ASSERT(key.Save() == ErrorCode::OK);
  ASSERT(ReopenSequentialDatabaseValue(path, 0, "seq") == 2);
}

void TestDatabaseSequentialReadFailureRequires()
{
#if defined(LIBXR_SYSTEM_Linux)
  ExpectFatalExit(
      XR_DB_FATAL_SEQ_READ,
      []
      {
        FailingFlash flash;
        DatabaseRawSequential db(flash);
        flash.SetFailOp(FailingFlash::FailOp::READ);
        db.Load();
      });
#endif
}

void TestDatabaseSequentialWriteFailureRequires()
{
#if defined(LIBXR_SYSTEM_Linux)
  ExpectFatalExit(
      XR_DB_FATAL_SEQ_WRITE,
      []
      {
        FailingFlash flash;
        DatabaseRawSequential db(flash);
        flash.SetFailOp(FailingFlash::FailOp::WRITE);
        db.Restore();
      });
#endif
}

void TestDatabaseSequentialEraseFailureRequires()
{
#if defined(LIBXR_SYSTEM_Linux)
  ExpectFatalExit(
      XR_DB_FATAL_SEQ_ERASE,
      []
      {
        FailingFlash flash;
        DatabaseRawSequential db(flash);
        flash.SetFailOp(FailingFlash::FailOp::ERASE);
        db.Save();
      });
#endif
}

}  // namespace

void test_database_binding_sequential()
{
  TestDatabaseBindingSequentialSmoke();
  TestDatabaseSequentialSaveCurrentValue();
  TestDatabaseSequentialReadFailureRequires();
  TestDatabaseSequentialWriteFailureRequires();
  TestDatabaseSequentialEraseFailureRequires();
}
