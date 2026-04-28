#include <sys/types.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "database.hpp"
#include "libxr_def.hpp"
#include "linux_flash.hpp"
#include "test.hpp"

using namespace LibXR;

namespace
{

constexpr size_t XR_DB_FLASH_SIZE = 4096;
constexpr size_t XR_DB_MIN_ERASE_SIZE = 512;
constexpr size_t XR_DB_MIN_WRITE_SIZE = 16;
constexpr size_t XR_DB_BLOCK_SIZE = XR_DB_FLASH_SIZE / 2;
constexpr size_t XR_DB_CHECKSUM_OFFSET = XR_DB_BLOCK_SIZE - XR_DB_MIN_WRITE_SIZE;
constexpr uint32_t XR_DB_FLASH_HEADER = 0x12345678 + LIBXR_DATABASE_VERSION;
constexpr uint32_t XR_DB_CHECKSUM = 0x9abcedf0;

enum class MainChecksum
{
  VALID,
  INVALID,
};

uint32_t ReadLe32(const std::vector<uint8_t>& bytes, size_t offset)
{
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

void WriteLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
  bytes[offset] = static_cast<uint8_t>(value & 0xFF);
  bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

std::vector<uint8_t> ReadAllBytes(const char* path)
{
  std::ifstream file(path, std::ios::binary);
  ASSERT(static_cast<bool>(file));

  std::vector<uint8_t> bytes(XR_DB_FLASH_SIZE, 0);
  file.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  ASSERT(file.gcount() == static_cast<std::streamsize>(bytes.size()));
  return bytes;
}

void WriteAllBytes(const char* path, const std::vector<uint8_t>& bytes)
{
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  ASSERT(static_cast<bool>(file));
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  ASSERT(static_cast<bool>(file));
}

void CraftPartialBackup(std::vector<uint8_t>& bytes, size_t partial_len)
{
  ASSERT(partial_len < XR_DB_BLOCK_SIZE);

  const size_t backup_offset = XR_DB_BLOCK_SIZE;
  for (size_t i = 0; i < partial_len; ++i)
  {
    bytes[backup_offset + i] = bytes[i];
  }

  WriteLe32(bytes, backup_offset + XR_DB_CHECKSUM_OFFSET, XR_DB_CHECKSUM);
}

void InvalidateMainChecksum(std::vector<uint8_t>& bytes)
{
  WriteLe32(bytes, XR_DB_CHECKSUM_OFFSET, 0);
}

void CreateSeedDatabase(const char* path)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  db.Restore();
  DatabaseRaw<16>::Key<uint32_t> key(db, "key", 1234);
  ASSERT(key.data_ == 1234);
}

uint32_t ReopenDatabaseValue(const char* path, uint32_t default_value)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  DatabaseRaw<16>::Key<uint32_t> key(db, "key", default_value);
  return key.data_;
}

void AssertMainValidBackupInvalid(const char* path)
{
  auto bytes = ReadAllBytes(path);
  ASSERT(ReadLe32(bytes, 0) == XR_DB_FLASH_HEADER);
  ASSERT(ReadLe32(bytes, XR_DB_CHECKSUM_OFFSET) == XR_DB_CHECKSUM);
  ASSERT(ReadLe32(bytes, XR_DB_BLOCK_SIZE + XR_DB_CHECKSUM_OFFSET) !=
         XR_DB_CHECKSUM);
}

void RunPartialBackupCase(const char* path, MainChecksum main_checksum,
                          uint32_t default_value, uint32_t expected_value)
{
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  CraftPartialBackup(bytes, 128);
  if (main_checksum == MainChecksum::INVALID)
  {
    InvalidateMainChecksum(bytes);
  }
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, default_value) == expected_value);
  AssertMainValidBackupInvalid(path);
}

void TestDatabasePartialBackupRecovery()
{
  RunPartialBackupCase("/tmp/flash_test_partial_valid_main.bin", MainChecksum::VALID, 0,
                       1234);
  RunPartialBackupCase("/tmp/flash_test_partial_broken_main.bin", MainChecksum::INVALID,
                       55, 55);
}

}  // namespace

void test_database()
{
  constexpr size_t FLASH_SIZE = XR_DB_FLASH_SIZE;

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

  TestDatabasePartialBackupRecovery();
}
