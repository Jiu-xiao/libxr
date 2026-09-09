/**
 * @file test_database.cpp
 * @brief Database::Key 读写测试 / Database::Key read and write tests.
 *
 * 检查 Save 和 Set 使用当前值，并在读取或保存失败时保留约定的数据和错误码。
 * Check current-value saves and the data and error codes retained after read or save
 * failures.
 */

#include <cstdint>

#include "database.hpp"
#include "test.hpp"
#include "test_assert.hpp"

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
      TEST_ASSERT(data.size_ == sizeof(stored));
      Memory::FastCopy(&stored, data.addr_, sizeof(stored));
    }
    return set_result;
  }

  ErrorCode Add(KeyBase& key) override
  {
    add_calls++;
    if (add_result == ErrorCode::OK)
    {
      TEST_ASSERT(key.raw_data_.size_ == sizeof(stored));
      Memory::FastCopy(&stored, key.raw_data_.addr_, sizeof(stored));
    }
    return add_result;
  }
};

void TestDatabaseKeySaveUsesCurrentData()
{
  // 保存失败时保留键对象的新值，后端已保存的旧值不应被覆盖。
  // A failed save keeps the key object's new value but must not overwrite the backend's
  // old value.
  MemoryDatabase db;
  Database::Key<uint32_t> key(db, "mock", 10);
  TEST_ASSERT(key.data_ == 10);
  TEST_ASSERT(db.add_calls == 1);
  TEST_ASSERT(db.stored == 10);

  key.data_ = 20;
  TEST_ASSERT(key.Save() == ErrorCode::OK);
  TEST_ASSERT(db.stored == 20);

  db.set_result = ErrorCode::FAILED;
  key.data_ = 30;
  TEST_ASSERT(key.Save() == ErrorCode::FAILED);
  TEST_ASSERT(key.data_ == 30);
  TEST_ASSERT(db.stored == 20);
}

void TestDatabaseKeySetUpdatesCurrentValueBeforeSave()
{
  MemoryDatabase db;
  Database::Key<uint32_t> key(db, "mock", 10);

  db.set_result = ErrorCode::FAILED;
  TEST_ASSERT(key.Set(40) == ErrorCode::FAILED);
  TEST_ASSERT(key.data_ == 40);
  TEST_ASSERT(db.stored == 10);
}

void TestDatabaseKeyUsesDefaultOnGetFailure()
{
  // 读取失败不同于键不存在：使用默认值，但不能新增键覆盖后端数据。
  // A failed read is not a missing key: use the default without adding a key over backend
  // data.
  MemoryDatabase db;
  db.get_result = ErrorCode::FAILED;
  db.stored = 55;

  Database::Key<uint32_t> key(db, "mock", 123);
  TEST_ASSERT(key.data_ == 123);
  TEST_ASSERT(db.add_calls == 0);

  MemoryDatabase zero_db;
  zero_db.get_result = ErrorCode::FAILED;
  Database::Key<uint32_t> zero_key(zero_db, "mock");
  TEST_ASSERT(zero_key.data_ == 0);
  TEST_ASSERT(zero_db.add_calls == 0);
}

}  // namespace

void test_database()
{
  TestDatabaseKeySaveUsesCurrentData();
  TestDatabaseKeySetUpdatesCurrentValueBeforeSave();
  TestDatabaseKeyUsesDefaultOnGetFailure();
}
