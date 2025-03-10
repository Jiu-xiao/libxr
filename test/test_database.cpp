#include <sys/types.h>

#include <cstdint>
#include <cstring>

#include "database.hpp"
#include "linux_flash.hpp"
#include "test.hpp"

using namespace LibXR;

void test_database() {
  constexpr size_t FLASH_SIZE = 1024;
  LinuxBinaryFileFlash<FLASH_SIZE> flash(16, "/tmp/flash_test.bin");
  DatabaseRawSequential db(flash);

  db.Init();

  std::array<uint32_t, 1> key1_data = {1};
  std::array<uint32_t, 2> key2_data = {11, 22};
  std::array<uint32_t, 3> key3_data = {111, 222, 333};
  std::array<uint32_t, 4> key4_data = {1111, 2222, 3333, 4444};

  key4_data[1] = 123456;

  DatabaseRawSequential::Key key1(db, "key1", key1_data);
  DatabaseRawSequential::Key key2(db, "keasdasy2", key2_data);
  DatabaseRawSequential::Key key3(db, "keaasdasdy3", key3_data);
  DatabaseRawSequential::Key key4(db, "keyaskdhasjh4", key4_data);

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
}
