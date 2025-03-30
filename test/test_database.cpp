#include <sys/types.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "database.hpp"
#include "libxr_def.hpp"
#include "linux_flash.hpp"
#include "test.hpp"

using namespace LibXR;

void test_database() {
  constexpr size_t FLASH_SIZE = 2048;
  LinuxBinaryFileFlash<FLASH_SIZE> flash("/tmp/flash_test.bin", 512, 8, true,
                                         true);
  DatabaseRawSequential db(flash);

  std::array<uint32_t, 1> key1_data = {1};
  std::array<uint32_t, 2> key2_data = {11, 22};
  std::array<uint32_t, 3> key3_data = {111, 222, 333};
  std::array<uint32_t, 4> key4_data = {1111, 2222, 3333, 4444};

  DatabaseRawSequential::Key key1(db, "key1", key1_data);
  DatabaseRawSequential::Key key2(db, "keasdasy2", key2_data);
  DatabaseRawSequential::Key key3(db, "keaasdasdy3", key3_data);
  DatabaseRawSequential::Key key4(db, "keyaskdhasjh4", key4_data);

  key4_data[1] = 123456;

  key4 = key4_data;

  db.Load();

  key1.Load();
  key2.Load();
  key3.Load();
  key4.Load();

  ASSERT(memcmp(&key1_data[0], &key1.data_[0], sizeof(key1_data)) == 0);
  ASSERT(memcmp(&key2_data[0], &key2.data_[0], sizeof(key2_data)) == 0);
  ASSERT(memcmp(&key3_data[0], &key3.data_[0], sizeof(key3_data)) == 0);
  ASSERT(memcmp(&key4_data[0], &key4.data_[0], sizeof(key4_data)) == 0);

  LinuxBinaryFileFlash<FLASH_SIZE> flash_2("/tmp/flash_test_2.bin", 512, 16,
                                           false, true);
  DatabaseRaw<16> db_2(flash_2);

  DatabaseRawSequential::Key key1_2(db_2, "key1", key1_data);
  DatabaseRawSequential::Key key2_2(db_2, "keasdasy2", key2_data);
  DatabaseRawSequential::Key key3_2(db_2, "keaasdasdy3", key3_data);
  DatabaseRawSequential::Key key4_2(db_2, "keyaskdhasjh4", key4_data);

  key4_data[1] = 1234567;

  key1_2 = key1_data;
  key2_2 = key2_data;
  key3_2 = key3_data;
  key4_2 = key4_data;

  key1_2.Load();
  key2_2.Load();
  key3_2.Load();
  key4_2.Load();

  ASSERT(memcmp(&key1_data[0], &key1_2.data_[0], sizeof(key1_data)) == 0);
  ASSERT(memcmp(&key2_data[0], &key2_2.data_[0], sizeof(key2_data)) == 0);
  ASSERT(memcmp(&key3_data[0], &key3_2.data_[0], sizeof(key3_data)) == 0);
  ASSERT(memcmp(&key4_data[0], &key4_2.data_[0], sizeof(key4_data)) == 0);

  for (size_t i = 0; i < FLASH_SIZE; i++) {
    for (int j = 0; j < Thread::GetTime() % 3; j++) {
      key1_data[0] = j;
      key1_2 = key1_data;
    }
    for (int j = 0; j < Thread::GetTime() % 3; j++) {
      key2_data[0] = j;
      key2_2 = key2_data;
    }
    for (int j = 0; j < Thread::GetTime() % 3; j++) {
      key3_data[0] = j;
      key3_2 = key3_data;
    }
    for (int j = 0; j < Thread::GetTime() % 3; j++) {
      key4_data[0] = j;
      key4_2 = key4_data;
    }

    key1_2.Load();
    key2_2.Load();
    key3_2.Load();
    key4_2.Load();
    ASSERT(memcmp(&key1_data[0], &key1_2.data_[0], sizeof(key1_data)) == 0);
    ASSERT(memcmp(&key2_data[0], &key2_2.data_[0], sizeof(key2_data)) == 0);
    ASSERT(memcmp(&key3_data[0], &key3_2.data_[0], sizeof(key3_data)) == 0);
    ASSERT(memcmp(&key4_data[0], &key4_2.data_[0], sizeof(key4_data)) == 0);
  }
}
