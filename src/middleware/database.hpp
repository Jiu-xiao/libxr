#pragma once

#include "flash.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"

namespace LibXR {
class Database {
 public:
  class KeyBase {
   public:
    const char* name_;
    RawData raw_data_;

    KeyBase(const char* name, RawData raw_data)
        : name_(name), raw_data_(raw_data) {}
  };

  template <typename Data>
  class Key : public KeyBase {
   public:
    Data data_;
    Database& database_;

    Key(Database& database, const char* name, Data init_value)
        : KeyBase(name, RawData(data_)), database_(database) {
      if (database.Get(*this) == ErrorCode::NOT_FOUND) {
        data_ = init_value;
        database.Add(*this);
      }
    }

    Key(Database& database, const char* name)
        : KeyBase(name, RawData(data_)), database_(database) {
      if (database.Get(*this) == ErrorCode::NOT_FOUND) {
        data_ = memset(&data_, 0, sizeof(Data));
        database.Add(*this);
      }
    }

    operator Data() { return data_; }

    ErrorCode Set(Data data) { return database_.Set(*this, RawData(data)); }

    ErrorCode Load() { return database_.Get(*this); }

    ErrorCode operator=(Data data) { return Set(data); }  // NOLINT
  };

 private:
  virtual ErrorCode Get(KeyBase& key) = 0;
  virtual ErrorCode Set(KeyBase& key, RawData data) = 0;
  virtual ErrorCode Add(KeyBase& key) = 0;
};

class DatabaseRawSequential : public Database {
 public:
  explicit DatabaseRawSequential(Flash& flash, size_t max_buffer_size = 256);
  void Init();
  void Save();
  void Load();
  void Restore();

 private:
  typedef struct __attribute__((packed)) {
    uint8_t nextKey;
    uint32_t nameLength : 24;
    size_t dataSize;
  } KeyInfo;

  struct FlashInfo {
    uint32_t header;
    KeyInfo key;
  };

  ErrorCode AddKey(const char* name, void* data, size_t size);
  ErrorCode SetKey(const char* name, const void* data, size_t size);
  ErrorCode SetKey(KeyInfo* key, const void* data, size_t size);

  uint8_t* GetKeyData(KeyInfo* key);
  const char* GetKeyName(KeyInfo* key);

  static constexpr uint32_t FLASH_HEADER = 0x12345678;
  static constexpr uint8_t CHECKSUM_BYTE = 0x56;

  Flash& flash_;
  uint8_t* data_buffer_;
  FlashInfo* flash_data_;
  FlashInfo* info_main_;
  FlashInfo* info_backup_;
  uint32_t block_size_;
  uint32_t max_buffer_size_;

  void InitBlock(FlashInfo* block);
  bool IsBlockInited(FlashInfo* block);
  bool IsBlockEmpty(FlashInfo* block);
  bool IsBlockError(FlashInfo* block);
  size_t GetKeySize(KeyInfo* key);
  KeyInfo* GetNextKey(KeyInfo* key);
  KeyInfo* GetLastKey(FlashInfo* block);
  KeyInfo* SearchKey(const char* name);

  template <typename Func>
  void Foreach(Func func) {
    if (IsBlockEmpty(flash_data_)) {
      return;
    }

    KeyInfo* key = &flash_data_->key;
    while (true) {
      if (!func(key, GetKeyName(key))) {
        break;
      }
      if (!key->nextKey) {
        break;
      }
      key = GetNextKey(key);
    }
  }

  ErrorCode Get(Database::KeyBase& key) override {
    auto ans = SearchKey(key.name_);
    if (!ans) {
      return ErrorCode::NOT_FOUND;
    }

    if (key.raw_data_.size_ != ans->dataSize) {
      return ErrorCode::FAILED;
    }

    memcpy(key.raw_data_.addr_, GetKeyData(ans), ans->dataSize);

    return ErrorCode::OK;
  }

  ErrorCode Set(KeyBase& key, RawData data) override {
    return SetKey(key.name_, data.addr_, data.size_);
  }

  ErrorCode Add(KeyBase& key) override {
    return AddKey(key.name_, key.raw_data_.addr_, key.raw_data_.size_);
  }
};

}  // namespace LibXR
