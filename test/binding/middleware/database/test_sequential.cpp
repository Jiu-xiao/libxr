/**
 * @file test_sequential.cpp
 * @brief binding 平面 `DatabaseRawSequential` 测试。 Binding-plane tests for `DatabaseRawSequential`.
 *
 * 测试项目 / Test items:
 * 1. sequential 数据库烟雾流量。 Sequential smoke traffic: verify repeated multi-key save/load cycles preserve data through the sequential file-backed binding.
 * 2. `Save()` 保存当前值语义。 Save-current-value behavior: verify `Save()` persists the key object's current value.
 * 3. 读写擦失败的 fatal 路径。 Failure semantics: verify sequential read/write/erase backend failures take the expected fatal path.
 *
 * 测试原理 / Test principles:
 * 1. 在 Linux flash 绑定上跑真实 sequential 数据库，验证的是 binding 平面的持久化契约。 Stress the real sequential database over the Linux flash binding so the test covers binding-plane persistence, not a mock backend.
 * 2. 把长时间烟雾流量和 fatal-path probe 放在一起，兼顾稳态和硬失败语义。 Combine long-running smoke traffic with fatal-path probes so both steady-state and hard-failure contracts are documented.
 */
#include "database_binding_test_common.hpp"

namespace
{

using namespace DatabaseBindingTestCommon;

/**
 * @brief 测试项函数 `TestDatabaseBindingSequentialSmoke`。 Test-item function `TestDatabaseBindingSequentialSmoke`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseBindingSequentialSmoke()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试项函数 `TestDatabaseSequentialSaveCurrentValue`。 Test-item function `TestDatabaseSequentialSaveCurrentValue`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseSequentialSaveCurrentValue()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  const char* path = "/tmp/flash_test_seq_save_current.bin";
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, 512, 8, true, true);
  DatabaseRawSequential db(flash);
  db.Restore();

  DatabaseRawSequential::Key<uint32_t> key(db, "seq", 1);
  key.data_ = 2;
  ASSERT(key.Save() == ErrorCode::OK);
  ASSERT(ReopenSequentialDatabaseValue(path, 0, "seq") == 2);
}

/**
 * @brief 测试项函数 `TestDatabaseSequentialReadFailureRequires`。 Test-item function `TestDatabaseSequentialReadFailureRequires`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseSequentialReadFailureRequires()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试项函数 `TestDatabaseSequentialWriteFailureRequires`。 Test-item function `TestDatabaseSequentialWriteFailureRequires`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseSequentialWriteFailureRequires()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试项函数 `TestDatabaseSequentialEraseFailureRequires`。 Test-item function `TestDatabaseSequentialEraseFailureRequires`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseSequentialEraseFailureRequires()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试入口函数 `test_database_binding_sequential`。 Test entry function `test_database_binding_sequential`.
 * @details 测试内容：按本文件声明的测试项目顺序执行验证。 Execute the test items declared in this file in order.
 *          测试原理：通过当前文件组织的测试场景组合，对外验证该模块契约。 Validate the module contract through the scenarios assembled in this file.
 */
void test_database_binding_sequential()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestDatabaseBindingSequentialSmoke();
  TestDatabaseSequentialSaveCurrentValue();
  TestDatabaseSequentialReadFailureRequires();
  TestDatabaseSequentialWriteFailureRequires();
  TestDatabaseSequentialEraseFailureRequires();
}
