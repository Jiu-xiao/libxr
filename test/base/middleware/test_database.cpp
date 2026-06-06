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
  MemoryDatabase db;
  Database::Key<uint32_t> key(db, "mock", 10);

  db.set_result = ErrorCode::FAILED;
  ASSERT(key.Set(40) == ErrorCode::FAILED);
  ASSERT(key.data_ == 40);
  ASSERT(db.stored == 10);
}

void TestDatabaseKeyUsesDefaultOnGetFailure()
{
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
  TestDatabaseKeySaveUsesCurrentData();
  TestDatabaseKeySetUpdatesCurrentValueBeforeSave();
  TestDatabaseKeyUsesDefaultOnGetFailure();
}
