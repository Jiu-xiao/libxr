/**
 * @file test_sequential_failures.cpp
 * @brief linux file-backed `DatabaseRawSequential` fatal 失败路径子测试。 Split test unit for linux file-backed `DatabaseRawSequential` fatal failure paths.
 * @details 测试项目：
 *          1. backend 读失败触发预期 fatal exit。
 *          2. backend 写失败触发预期 fatal exit。
 *          3. backend 擦除失败触发预期 fatal exit。
 *          Test items:
 *          1. Backend read failures trigger the expected fatal exit.
 *          2. Backend write failures trigger the expected fatal exit.
 *          3. Backend erase failures trigger the expected fatal exit.
 */
#include "linux_database_test_common.hpp"

namespace
{

using namespace LinuxDatabaseTestCommon;

void TestDatabaseSequentialReadFailureRequires()
{
  // 测试内容：验证 sequential backend 读失败时走到规定的 fatal 退出路径。
  // Test coverage: verify that sequential backend read failures take the specified fatal-exit path.
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
  // 测试内容：验证 sequential backend 写失败时走到规定的 fatal 退出路径。
  // Test coverage: verify that sequential backend write failures take the specified fatal-exit path.
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
  // 测试内容：验证 sequential backend 擦除失败时走到规定的 fatal 退出路径。
  // Test coverage: verify that sequential backend erase failures take the specified fatal-exit path.
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

void RunLinuxDatabaseSequentialFailureTests()
{
  TestDatabaseSequentialReadFailureRequires();
  TestDatabaseSequentialWriteFailureRequires();
  TestDatabaseSequentialEraseFailureRequires();
}
