#include <sys/types.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "database.hpp"
#include "libxr_def.hpp"
#include "linux_flash.hpp"
#include "test.hpp"

using namespace LibXR;

void test_database()
{
  constexpr size_t FLASH_SIZE = 4096;

  // 1. 初始化 Flash 和 DB
  LinuxBinaryFileFlash<FLASH_SIZE> test_flash("/tmp/flash_test.bin", 512, 8, true, true);
  DatabaseRawSequential test_db(test_flash);

  // 2. 定义数据和 Key 对象
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
    k1 = data_k1;  // 写入
    k2 = data_k2;  // 写入
    k3 = data_k3;  // 写入
    k4 = data_k4;  // 写入

    k1.Load();  // 读取
    k2.Load();  // 读取
    k3.Load();  // 读取
    k4.Load();  // 读取

    ASSERT(memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
    ASSERT(memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
    ASSERT(memcmp(data_k3.data(), k3.data_.data(), sizeof(data_k3)) == 0);
    ASSERT(memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);

    for (int i = 0; i < Thread::GetTime() % 100; i++)
    {  // 3. 覆盖 key4 并校验
      data_k4[1] = Thread::GetTime() + i;
      k4 = data_k4;  // 写入
      k4.Load();     // 读取
      ASSERT(memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);
    }

    for (int i = 0; i < Thread::GetTime() % 100; i++)
    {  // 4. key1 覆盖-读取-校验
      data_k1[0] = Thread::GetTime() + i;
      k1 = data_k1;
      k1.Load();
      ASSERT(memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
    }

    // 5. 多次循环 Load/校验
    for (int i = 0; i < Thread::GetTime() % 100; ++i)
    {
      k1.Load();
      k2.Load();
      k3.Load();
      k4.Load();
      ASSERT(memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
      ASSERT(memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
      ASSERT(memcmp(data_k3.data(), k3.data_.data(), sizeof(data_k3)) == 0);
      ASSERT(memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);
    }

    // 6. 更新 key2 数据，写入并校验
    for (int i = 0; i < Thread::GetTime() % 100; i++)
    {
      data_k2[0] = LibXR::Timebase::GetMicroseconds();
      data_k2[1] = LibXR::Timebase::GetMilliseconds();
      k2 = data_k2;
      k2.Load();
      ASSERT(memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
    }

    // 7. 最后统一校验
    ASSERT(memcmp(data_k1.data(), k1.data_.data(), sizeof(data_k1)) == 0);
    ASSERT(memcmp(data_k2.data(), k2.data_.data(), sizeof(data_k2)) == 0);
    ASSERT(memcmp(data_k3.data(), k3.data_.data(), sizeof(data_k3)) == 0);
    ASSERT(memcmp(data_k4.data(), k4.data_.data(), sizeof(data_k4)) == 0);
  }

  LinuxBinaryFileFlash<FLASH_SIZE> flash_2("/tmp/flash_test_2.bin", 512, 16, false, true);
  DatabaseRaw<16> test_db_2(flash_2, 5);

  DatabaseRaw<16>::Key k1_2(test_db_2, "key1", data_k1);
  DatabaseRaw<16>::Key k2_2(test_db_2, "keasdasy2", data_k2);
  DatabaseRaw<16>::Key k3_2(test_db_2, "keaasdasdy3", data_k3);
  DatabaseRaw<16>::Key k4_2(test_db_2, "keyaskdhasjh4", data_k4);

  data_k4[1] = 1234567;

  k1_2 = data_k1;
  k2_2 = data_k2;
  k3_2 = data_k3;
  k4_2 = data_k4;

  k1_2.Load();
  k2_2.Load();
  k3_2.Load();
  k4_2.Load();

  ASSERT(memcmp(&data_k1[0], &k1_2.data_[0], sizeof(data_k1)) == 0);
  ASSERT(memcmp(&data_k2[0], &k2_2.data_[0], sizeof(data_k2)) == 0);
  ASSERT(memcmp(&data_k3[0], &k3_2.data_[0], sizeof(data_k3)) == 0);
  ASSERT(memcmp(&data_k4[0], &k4_2.data_[0], sizeof(data_k4)) == 0);

  for (size_t i = 0; i < 1000; i++)
  {
    for (uint32_t j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k1[0] = Thread::GetTime() + j;
      k1_2 = data_k1;
    }
    for (uint32_t j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k2[0] = Thread::GetTime() + j;
      k2_2 = data_k2;
    }
    for (uint32_t j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k3[0] = Thread::GetTime() + j;
      k3_2 = data_k3;
    }
    for (uint32_t j = 0; j < Thread::GetTime() % 100; j++)
    {
      data_k4[0] = Thread::GetTime() + j;
      k4_2 = data_k4;
    }

    k1_2.Load();
    k2_2.Load();
    k3_2.Load();
    k4_2.Load();
    ASSERT(memcmp(&data_k1[0], &k1_2.data_[0], sizeof(data_k1)) == 0);
    ASSERT(memcmp(&data_k2[0], &k2_2.data_[0], sizeof(data_k2)) == 0);
    ASSERT(memcmp(&data_k3[0], &k3_2.data_[0], sizeof(data_k3)) == 0);
    ASSERT(memcmp(&data_k4[0], &k4_2.data_[0], sizeof(data_k4)) == 0);
  }
}
