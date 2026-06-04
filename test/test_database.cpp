#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
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
constexpr uint8_t XR_DB_SEQ_CHECKSUM = 0x56;
// White-box layout anchors for current DatabaseRaw<16> block format:
// - FlashInfo header padding occupies one MinWriteSize block
// - sentinel key occupies one aligned KeyInfo block with empty name/data
// - KeyInfo stores 3 flag blocks + 4-byte raw_info, rounded to 4 * MinWriteSize
constexpr size_t XR_DB_RAW_KEYINFO_ALIGNED_SIZE = XR_DB_MIN_WRITE_SIZE * 4;
constexpr size_t XR_DB_RAW_SENTINEL_KEY_OFFSET = XR_DB_MIN_WRITE_SIZE;
constexpr size_t XR_DB_RAW_FIRST_KEY_OFFSET =
    XR_DB_RAW_SENTINEL_KEY_OFFSET + XR_DB_RAW_KEYINFO_ALIGNED_SIZE;
constexpr size_t XR_DB_RAW_AVAILABLE_FLAG_OFFSET =
    XR_DB_RAW_FIRST_KEY_OFFSET + XR_DB_MIN_WRITE_SIZE;
constexpr size_t XR_DB_RAW_UNINIT_FLAG_LAST_BYTE_OFFSET =
    XR_DB_RAW_FIRST_KEY_OFFSET + XR_DB_MIN_WRITE_SIZE * 3 - 1;
constexpr size_t XR_DB_RAW_FIRST_KEY_RAW_INFO_OFFSET =
    XR_DB_RAW_FIRST_KEY_OFFSET + XR_DB_MIN_WRITE_SIZE * 3;
constexpr int XR_DB_FATAL_KEY_ADD = 97;
constexpr int XR_DB_FATAL_SEQ_READ = 91;
constexpr int XR_DB_FATAL_SEQ_WRITE = 92;
constexpr int XR_DB_FATAL_SEQ_ERASE = 93;
constexpr int XR_DB_FATAL_RAW_READ = 94;
constexpr int XR_DB_FATAL_RAW_WRITE = 95;
constexpr int XR_DB_FATAL_RAW_ERASE = 96;

enum class MainChecksum
{
  VALID,
  INVALID,
};

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

class FailingFlash : public Flash
{
 public:
  enum class FailOp
  {
    NONE,
    READ,
    WRITE,
    ERASE,
  };

  explicit FailingFlash(size_t min_erase_size = 512, size_t min_write_size = 8,
                        size_t sequential_buffer_size = 256)
      : Flash(min_erase_size, min_write_size,
              RawData(flash_area_.data(), flash_area_.size())),
        sequential_buffer_size_(sequential_buffer_size)
  {
    SeedValidSequentialBlocks();
  }

  void SetFailOp(FailOp op) { fail_op_ = op; }

  ErrorCode Erase(size_t offset, size_t size) override
  {
    if (fail_op_ == FailOp::ERASE)
    {
      return ErrorCode::FAILED;
    }
    ASSERT(offset + size <= flash_area_.size());
    std::memset(flash_area_.data() + offset, 0xFF, size);
    return ErrorCode::OK;
  }

  ErrorCode Write(size_t offset, ConstRawData data) override
  {
    if (fail_op_ == FailOp::WRITE)
    {
      return ErrorCode::FAILED;
    }
    ASSERT(offset + data.size_ <= flash_area_.size());
    std::memcpy(flash_area_.data() + offset, data.addr_, data.size_);
    return ErrorCode::OK;
  }

  ErrorCode Read(size_t offset, RawData data) override
  {
    if (fail_op_ == FailOp::READ)
    {
      return ErrorCode::FAILED;
    }
    return Flash::Read(offset, data);
  }

 private:
  void SeedValidSequentialBlocks()
  {
    std::memset(flash_area_.data(), 0xFF, flash_area_.size());

    const size_t block_size = flash_area_.size() / 2;
    const uint32_t empty_key = 0;

    std::memcpy(flash_area_.data(), &XR_DB_FLASH_HEADER, sizeof(XR_DB_FLASH_HEADER));
    std::memcpy(flash_area_.data() + sizeof(XR_DB_FLASH_HEADER), &empty_key,
                sizeof(empty_key));
    flash_area_[sequential_buffer_size_ - 1] = XR_DB_SEQ_CHECKSUM;

    std::memcpy(flash_area_.data() + block_size, &XR_DB_FLASH_HEADER,
                sizeof(XR_DB_FLASH_HEADER));
    std::memcpy(flash_area_.data() + block_size + sizeof(XR_DB_FLASH_HEADER), &empty_key,
                sizeof(empty_key));
    flash_area_[block_size + sequential_buffer_size_ - 1] = XR_DB_SEQ_CHECKSUM;
  }

  std::array<uint8_t, XR_DB_FLASH_SIZE> flash_area_{};
  size_t sequential_buffer_size_ = 0;
  FailOp fail_op_ = FailOp::NONE;
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

template <typename Func>
void ExpectFatalExit(int exit_code, Func&& func)
{
  pid_t child = fork();
  ASSERT(child >= 0);

  if (child == 0)
  {
    auto cb = LibXR::Assert::FatalCallback::Create(
        [](bool in_isr, int code, const char*, uint32_t)
        {
          UNUSED(in_isr);
          _exit(code);
        },
        exit_code);
    LibXR::Assert::RegisterFatalErrorCallback(cb);
    func();
    _exit(0);
  }

  int status = 0;
  ASSERT(waitpid(child, &status, 0) == child);
  ASSERT(WIFEXITED(status));
  ASSERT(WEXITSTATUS(status) == exit_code);
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

void MirrorMainBlockToBackup(std::vector<uint8_t>& bytes)
{
  for (size_t i = 0; i < XR_DB_BLOCK_SIZE; ++i)
  {
    bytes[XR_DB_BLOCK_SIZE + i] = bytes[i];
  }
}

void InvalidateMainChecksum(std::vector<uint8_t>& bytes)
{
  WriteLe32(bytes, XR_DB_CHECKSUM_OFFSET, 0);
}

void MarkMainFirstKeyAsUninitialized(std::vector<uint8_t>& bytes)
{
  bytes[XR_DB_RAW_UNINIT_FLAG_LAST_BYTE_OFFSET] = 0xFF;
}

void CorruptBackupFirstKeyAvailableFlag(std::vector<uint8_t>& bytes)
{
  bytes[XR_DB_BLOCK_SIZE + XR_DB_RAW_AVAILABLE_FLAG_OFFSET] = 0x00;
}

void CorruptMainFirstKeyRawInfo(std::vector<uint8_t>& bytes, uint32_t raw_info)
{
  WriteLe32(bytes, XR_DB_RAW_FIRST_KEY_RAW_INFO_OFFSET, raw_info);
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

void CreateTwoKeyDatabase(const char* path)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  db.Restore();
  DatabaseRaw<16>::Key<uint32_t> key1(db, "key1", 1111);
  DatabaseRaw<16>::Key<uint32_t> key2(db, "key2", 2222);
  ASSERT(key1.data_ == 1111);
  ASSERT(key2.data_ == 2222);
}

uint32_t ReopenDatabaseValue(const char* path, uint32_t default_value)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  DatabaseRaw<16>::Key<uint32_t> key(db, "key", default_value);
  return key.data_;
}

uint32_t ReopenDatabaseValue(const char* path, uint32_t default_value,
                             const char* key_name)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  DatabaseRaw<16>::Key<uint32_t> key(db, key_name, default_value);
  return key.data_;
}

uint32_t ReopenSequentialDatabaseValue(const char* path, uint32_t default_value,
                                       const char* key_name)
{
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, 512, 8, true, true);
  DatabaseRawSequential db(flash);
  DatabaseRawSequential::Key<uint32_t> key(db, key_name, default_value);
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

void TestDatabaseKeyAddFailureRequires()
{
#if defined(LIBXR_SYSTEM_Linux)
  ExpectFatalExit(
      XR_DB_FATAL_KEY_ADD,
      []
      {
        MemoryDatabase db;
        db.get_result = ErrorCode::NOT_FOUND;
        db.add_result = ErrorCode::FAILED;
        Database::Key<uint32_t> key(db, "mock", 123);
        UNUSED(key);
      });
#endif
}

void TestDatabaseSequentialSaveCurrentValue()
{
  const char* path = "/tmp/flash_test_seq_save_current.bin";
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, 512, 8, true, true);
  DatabaseRawSequential db(flash);
  db.Restore();

  DatabaseRawSequential::Key<uint32_t> key(db, "seq", 1);
  key.data_ = 2;
  ASSERT(key.Save() == ErrorCode::OK);
  ASSERT(ReopenSequentialDatabaseValue(path, 0, "seq") == 2);
}

void TestDatabaseSequentialReadFailureRequires()
{
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

void TestDatabaseRawReadFailureRequires()
{
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

void TestDatabaseRawWriteFailureRequires()
{
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

void TestDatabaseRawEraseFailureRequires()
{
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

void TestDatabaseRawSaveCurrentValue()
{
  const char* path = "/tmp/flash_test_raw_save_current.bin";
  LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                               XR_DB_MIN_WRITE_SIZE, false, true);
  DatabaseRaw<16> db(flash, 5);
  db.Restore();

  DatabaseRaw<16>::Key<uint32_t> key(db, "raw", 1);
  key.data_ = 2;
  ASSERT(key.Save() == ErrorCode::OK);
  ASSERT(ReopenDatabaseValue(path, 0, "raw") == 2);
}

void TestDatabaseRawRequiresExactStoredSize()
{
  const char* path = "/tmp/flash_test_raw_exact_size.bin";
  {
    LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                                 XR_DB_MIN_WRITE_SIZE, false, true);
    DatabaseRaw<16> db(flash, 5);
    db.Restore();
    DatabaseRaw<16>::Key<uint32_t> key(db, "shape", 0x11223344U);
    ASSERT(key.data_ == 0x11223344U);
  }

  {
    LinuxBinaryFileFlash<XR_DB_FLASH_SIZE> flash(path, XR_DB_MIN_ERASE_SIZE,
                                                 XR_DB_MIN_WRITE_SIZE, false, true);
    DatabaseRaw<16> db(flash, 5);
    DatabaseRaw<16>::Key<uint64_t> wider_key(db, "shape", 0ULL);
    ASSERT(wider_key.data_ == 0ULL);
    ASSERT(wider_key.Load() == ErrorCode::FAILED);
  }

  ASSERT(ReopenDatabaseValue(path, 0, "shape") == 0x11223344U);
}

void TestDatabaseRawInvalidMainKeyMetadataReinitializes()
{
  const char* path = "/tmp/flash_test_raw_invalid_main_key_metadata.bin";
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  MarkMainFirstKeyAsUninitialized(bytes);
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, 77) == 77);
  auto repaired = ReadAllBytes(path);
  ASSERT(ReadLe32(repaired, 0) == XR_DB_FLASH_HEADER);
  ASSERT(ReadLe32(repaired, XR_DB_CHECKSUM_OFFSET) == XR_DB_CHECKSUM);
}

void TestDatabaseRawInvalidBackupMetadataDoesNotRestore()
{
  const char* path = "/tmp/flash_test_raw_invalid_backup_metadata.bin";
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  MirrorMainBlockToBackup(bytes);
  CorruptBackupFirstKeyAvailableFlag(bytes);
  InvalidateMainChecksum(bytes);
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, 55) == 55);
  AssertMainValidBackupInvalid(path);
}

void TestDatabaseRawRestoresFromValidBackup()
{
  const char* path = "/tmp/flash_test_raw_restore_from_valid_backup.bin";
  CreateSeedDatabase(path);

  auto bytes = ReadAllBytes(path);
  MirrorMainBlockToBackup(bytes);
  InvalidateMainChecksum(bytes);
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, 55) == 1234);
  AssertMainValidBackupInvalid(path);
}

void TestDatabaseRawCorruptFirstKeySizeInMultiKeyDatabaseReinitializes()
{
  const char* path = "/tmp/flash_test_raw_corrupt_first_key_size.bin";
  CreateTwoKeyDatabase(path);

  auto bytes = ReadAllBytes(path);
  // Keep the first key's flags but blow up its name/data metadata so the next-key
  // walk crosses the checksum boundary on the next iteration.
  CorruptMainFirstKeyRawInfo(bytes, 0x7FFFFFFFU);
  WriteAllBytes(path, bytes);

  ASSERT(ReopenDatabaseValue(path, 77, "key1") == 77);
  ASSERT(ReopenDatabaseValue(path, 88, "key2") == 88);
  auto repaired = ReadAllBytes(path);
  ASSERT(ReadLe32(repaired, 0) == XR_DB_FLASH_HEADER);
  ASSERT(ReadLe32(repaired, XR_DB_CHECKSUM_OFFSET) == XR_DB_CHECKSUM);
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
  TestDatabaseKeySaveUsesCurrentData();
  TestDatabaseKeySetUpdatesCurrentValueBeforeSave();
  TestDatabaseKeyUsesDefaultOnGetFailure();
  TestDatabaseKeyAddFailureRequires();
  TestDatabaseSequentialSaveCurrentValue();
  TestDatabaseSequentialReadFailureRequires();
  TestDatabaseSequentialWriteFailureRequires();
  TestDatabaseSequentialEraseFailureRequires();
  TestDatabaseRawReadFailureRequires();
  TestDatabaseRawWriteFailureRequires();
  TestDatabaseRawEraseFailureRequires();
  TestDatabaseRawSaveCurrentValue();
  TestDatabaseRawRequiresExactStoredSize();
  TestDatabaseRawInvalidMainKeyMetadataReinitializes();
  TestDatabaseRawInvalidBackupMetadataDoesNotRestore();
  TestDatabaseRawRestoresFromValidBackup();
  TestDatabaseRawCorruptFirstKeySizeInMultiKeyDatabaseReinitializes();
}
