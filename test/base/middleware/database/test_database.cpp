/**
 * @file test_database.cpp
 * @brief base 平面 `Database::Key` 契约测试。 Base-plane `Database::Key` contract tests.
 *
 * 测试项目 / Test items:
 * 1. `Save()` 使用当前内存值。 Save path: verify `Save()` writes the key's current in-memory value, not a stale constructor value.
 * 2. `Set()` 先更新本地缓存再落库。 Set path: verify `Set()` updates the local cached value before delegating persistence.
 * 3. `Get()` 失败时的默认值回退。 Get/default path: verify constructor fallback to explicit default or zero when `Get()` fails.
 *
 * 测试原理 / Test principles:
 * 1. 用极小的内存数据库 stub 隔离 key 对象语义，不把 flash 布局问题混进来。 Use a tiny in-memory database stub so the test isolates key-object semantics from flash layout or backend persistence behavior.
 * 2. 同时观察后端调用计数和本地缓存值，因为 key 契约涵盖两者。 Observe both backend call counters and local cached values, because the key contract covers caller-visible state as well as backend dispatch.
 */
#include <cstdint>

#include "database.hpp"
#include "test.hpp"

using namespace LibXR;

namespace
{

class MemoryDatabase : public Database
{
 public:
  ErrorCode get_result = ErrorCode::NOT_FOUND;
  ErrorCode set_result = ErrorCode::OK;
  ErrorCode add_result = ErrorCode::OK;
  uint32_t stored = 0;
  size_t get_calls = 0;
  size_t set_calls = 0;
  size_t add_calls = 0;

  ErrorCode Get(KeyBase& key) override
  {
    get_calls++;
    if (get_result == ErrorCode::OK)
    {
      if (key.raw_data_.size_ != sizeof(stored))
      {
        return ErrorCode::FAILED;
      }
      Memory::FastCopy(key.raw_data_.addr_, &stored, sizeof(stored));
    }
    return get_result;
  }

  ErrorCode Set(KeyBase&, RawData data) override
  {
    set_calls++;
    if (set_result == ErrorCode::OK)
    {
      ASSERT(data.size_ == sizeof(stored));
      Memory::FastCopy(&stored, data.addr_, sizeof(stored));
    }
    return set_result;
  }

  ErrorCode Add(KeyBase& key) override
  {
    add_calls++;
    if (add_result == ErrorCode::OK)
    {
      ASSERT(key.raw_data_.size_ == sizeof(stored));
      Memory::FastCopy(&stored, key.raw_data_.addr_, sizeof(stored));
    }
    return add_result;
  }
};

void TestDatabaseKeySaveUsesCurrentData()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  MemoryDatabase db;
  Database::Key<uint32_t> key(db, "mock", 10);
  ASSERT(key.data_ == 10);
  ASSERT(db.add_calls == 1);
  ASSERT(db.stored == 10);

  key.data_ = 20;
  ASSERT(key.Save() == ErrorCode::OK);
  ASSERT(db.stored == 20);

  db.set_result = ErrorCode::FAILED;
  key.data_ = 30;
  ASSERT(key.Save() == ErrorCode::FAILED);
  ASSERT(key.data_ == 30);
  ASSERT(db.stored == 20);
}

void TestDatabaseKeySetUpdatesCurrentValueBeforeSave()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  MemoryDatabase db;
  Database::Key<uint32_t> key(db, "mock", 10);

  db.set_result = ErrorCode::FAILED;
  ASSERT(key.Set(40) == ErrorCode::FAILED);
  ASSERT(key.data_ == 40);
  ASSERT(db.stored == 10);
}

void TestDatabaseKeyUsesDefaultOnGetFailure()
{
  // 测试内容：执行当前辅助测试项，对应文件头中的一个具体项目。
  // Test coverage: execute the current helper-scoped test item from this file.
  MemoryDatabase db;
  db.get_result = ErrorCode::FAILED;
  db.stored = 55;

  Database::Key<uint32_t> key(db, "mock", 123);
  ASSERT(key.data_ == 123);
  ASSERT(db.add_calls == 0);

  MemoryDatabase zero_db;
  zero_db.get_result = ErrorCode::FAILED;
  Database::Key<uint32_t> zero_key(zero_db, "mock");
  ASSERT(zero_key.data_ == 0);
  ASSERT(zero_db.add_calls == 0);
}

}  // namespace

void test_database()
{
  // 测试内容：按文件头列出的测试项目顺序执行当前测试入口。
  // Test coverage: execute the test items listed in this file header in sequence.
  TestDatabaseKeySaveUsesCurrentData();
  TestDatabaseKeySetUpdatesCurrentValueBeforeSave();
  TestDatabaseKeyUsesDefaultOnGetFailure();
}
