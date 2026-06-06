/**
 * @file test_raw_failures.cpp
 * @brief binding `DatabaseRaw` failure 场景子测试。 Split test unit for binding `DatabaseRaw` failure scenarios.
 */
#include "database_binding_test_common.hpp"
#include "raw_binding_test_groups.hpp"

namespace
{

using namespace DatabaseBindingTestCommon;

/**
 * @brief 测试项函数 `TestDatabaseKeyAddFailureRequires`。 Test-item function `TestDatabaseKeyAddFailureRequires`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseKeyAddFailureRequires()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试项函数 `TestDatabaseRawReadFailureRequires`。 Test-item function `TestDatabaseRawReadFailureRequires`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseRawReadFailureRequires()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试项函数 `TestDatabaseRawWriteFailureRequires`。 Test-item function `TestDatabaseRawWriteFailureRequires`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseRawWriteFailureRequires()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

/**
 * @brief 测试项函数 `TestDatabaseRawEraseFailureRequires`。 Test-item function `TestDatabaseRawEraseFailureRequires`.
 * @details 测试内容：执行当前辅助测试项对应的具体场景与断言。 Execute the concrete scenario and assertions for the current helper-scoped test item.
 *          测试原理：把一个可单独说明的测试项目拆成独立函数，便于定位失败点并复用场景。 Split one explainable test item into an independent function so failures and reused scenarios stay easy to locate.
 */
void TestDatabaseRawEraseFailureRequires()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
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

}  // namespace

/**
 * @brief 测试项函数 `RunDatabaseBindingRawFailureTests`。 Test-item function `RunDatabaseBindingRawFailureTests`.
 * @details 测试内容：执行当前分组里的 `DatabaseRaw` binding 子场景。 Execute the grouped `DatabaseRaw` binding sub-scenarios.
 *          测试原理：把 smoke / failure / recovery 三类路径拆开，避免一个原始大文件持续膨胀。 Split smoke, failure, and recovery paths so one raw monolithic file does not keep growing.
 */
void RunDatabaseBindingRawFailureTests()
{
  TestDatabaseKeyAddFailureRequires();
  TestDatabaseRawReadFailureRequires();
  TestDatabaseRawWriteFailureRequires();
  TestDatabaseRawEraseFailureRequires();
}
