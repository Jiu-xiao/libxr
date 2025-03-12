#pragma once

#include <cstdint>
#include <cstring>

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
    uint8_t nextKey : 1;
    uint32_t nameLength : 7;
    size_t dataSize : 24;
  } KeyInfo;

  struct FlashInfo {
    uint32_t header;
    KeyInfo key;
  };

  ErrorCode AddKey(const char* name, const void* data, size_t size);
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

  ErrorCode Add(KeyBase& key) override {
    return AddKey(key.name_, key.raw_data_.addr_, key.raw_data_.size_);
  }

 public:
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
};

template <size_t MinWriteSize>
class DatabaseRaw : public Database {
 public:
  explicit DatabaseRaw(Flash& flash)
      : flash_(flash), write_buffer_(new uint8_t[flash_.min_write_size_]) {
    ASSERT(flash.min_erase_size_ * 2 <= flash.flash_area_.size_);
    ASSERT(flash_.min_write_size_ == MinWriteSize);
    auto block_num =
        static_cast<size_t>(flash.flash_area_.size_ / flash.min_erase_size_);
    block_size_ = block_num / 2 * flash.min_erase_size_;
    info_main_ = reinterpret_cast<FlashInfo*>(flash.flash_area_.addr_);
    info_backup_ = reinterpret_cast<FlashInfo*>(
        reinterpret_cast<uint8_t*>(flash.flash_area_.addr_) + block_size_);
    Init();
  }

  void Init() {
    if (!IsBlockInited(info_backup_) || IsBlockError(info_backup_)) {
      InitBlock(info_backup_);
    }

    if (IsBlockError(info_main_)) {
      if (IsBlockEmpty(info_backup_)) {
        InitBlock(info_main_);
      } else {
        flash_.Erase(0, block_size_);
        Write(0, {reinterpret_cast<uint8_t*>(info_backup_), block_size_});
      }
    } else if (!IsBlockEmpty(info_backup_)) {
      InitBlock(info_backup_);
    }

    KeyInfo* key = &info_main_->key;
    while (!key->no_next_key) {
      key = GetNextKey(key);
      if (key->uninit) {
        InitBlock(info_main_);
        break;
      }
    }
  }

  void Restore() {}
  ErrorCode Recycle() {
    if (IsBlockEmpty(info_main_)) {
      return ErrorCode::OK;
    }

    KeyInfo* key = &info_main_->key;

    if (!IsBlockEmpty(info_backup_)) {
      ASSERT(false);
      return ErrorCode::FAILED;
    }

    uint8_t* write_buff = reinterpret_cast<uint8_t*>(&info_backup_->key);

    auto new_key = KeyInfo{0, false, 0, 0, 0};
    Write(write_buff - reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          new_key);

    write_buff += GetKeySize(&info_backup_->key);

    do {
      key = GetNextKey(key);

      if (!key->available_flag) {
        continue;
      }

      Write(write_buff - reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
            *key);
      write_buff += AlignSize(sizeof(KeyInfo));
      Write(write_buff - reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
            {GetKeyName(key), key->nameLength});
      write_buff += AlignSize(key->nameLength);
      Write(write_buff - reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
            {GetKeyData(key), key->dataSize});
      write_buff += AlignSize(key->dataSize);
    } while (!key->no_next_key);

    flash_.Erase(0, block_size_);
    Write(0, {reinterpret_cast<uint8_t*>(info_backup_), block_size_});
    InitBlock(info_backup_);

    return ErrorCode::OK;
  }

 private:
  typedef struct __attribute__((packed)) {
    uint8_t no_next_key : 1;
    uint8_t available_flag : 1;
    uint8_t uninit : 1;
    uint32_t nameLength : 6;
    size_t dataSize : 23;
  } KeyInfo;

  struct FlashInfo {
   private:
    static constexpr size_t BASE_SIZE = sizeof(uint32_t);
    // NOLINTNEXTLINE
    static constexpr size_t ALIGNED_SIZE =
        (BASE_SIZE + MinWriteSize - 1) / MinWriteSize * MinWriteSize;
    // NOLINTNEXTLINE
    static constexpr size_t PADDING_SIZE = ALIGNED_SIZE - BASE_SIZE;

   public:
    uint32_t header;
    uint8_t res[PADDING_SIZE];
    KeyInfo key;

  } __attribute__((packed));

  size_t AvailableSize() {
    return block_size_ - sizeof(CHECKSUM_BYTE) -
           (reinterpret_cast<uint8_t*>(GetNextKey(GetLastKey(info_main_))) -
            reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_));
  }

  ErrorCode AddKey(const char* name, const void* data, size_t size) {
    if (auto ans = SearchKey(name)) {
      return SetKey(name, data, size);
    }

    bool recycle = false;

  add_again:
    const uint32_t NAME_LEN = strlen(name) + 1;
    KeyInfo* last_key = GetLastKey(info_main_);
    KeyInfo* key_buf = nullptr;

    if (!last_key) {
      auto flash_info = FlashInfo{FLASH_HEADER, {}, KeyInfo{0, false, 0, 0, 0}};
      Write(0, flash_info);
      key_buf = GetNextKey(&info_main_->key);
    } else {
      key_buf = GetNextKey(last_key);
    }

    if (AvailableSize() <
        AlignSize(sizeof(KeyInfo)) + AlignSize(NAME_LEN) + AlignSize(size)) {
      if (!recycle) {
        Recycle();
        recycle = true;
        goto add_again;  // NOLINT
      } else {
        ASSERT(false);
        return ErrorCode::FULL;
      }
    }

    if (last_key) {
      KeyInfo new_last_key = {0, last_key->available_flag, last_key->uninit,
                              last_key->nameLength, last_key->dataSize};
      Write(reinterpret_cast<uint8_t*>(last_key) -
                reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
            new_last_key);
    }

    auto new_key = KeyInfo{1, true, true, NAME_LEN, size};

    Write(reinterpret_cast<uint8_t*>(key_buf) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          new_key);
    Write(reinterpret_cast<const uint8_t*>(GetKeyName(key_buf)) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          {reinterpret_cast<const uint8_t*>(name), NAME_LEN});
    Write(reinterpret_cast<uint8_t*>(GetKeyData(key_buf)) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          {reinterpret_cast<const uint8_t*>(data), size});
    new_key.uninit = 0;
    Write(reinterpret_cast<uint8_t*>(key_buf) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          new_key);

    return ErrorCode::OK;
  }
  ErrorCode SetKey(const char* name, const void* data, size_t size,
                   bool recycle = true) {
    if (KeyInfo* key = SearchKey(name)) {
      if (key->dataSize == size) {
        if (memcmp(GetKeyData(key), data, size) != 0) {
          if (AvailableSize() < AlignSize(size) + AlignSize(sizeof(KeyInfo)) +
                                    AlignSize(key->nameLength)) {
            if (recycle) {
              Recycle();
              return SetKey(name, data, size, false);
            } else {
              ASSERT(false);
              return ErrorCode::FULL;
            }
          }

          KeyInfo new_key = {key->no_next_key, false, key->uninit,
                             key->nameLength, key->dataSize};
          Write(reinterpret_cast<uint8_t*>(key) -
                    reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
                new_key);
          return AddKey(GetKeyName(key), data, size);
        }
        return ErrorCode::OK;
      }
    }

    return ErrorCode::FAILED;
  }

  uint8_t* GetKeyData(KeyInfo* key) {
    return reinterpret_cast<uint8_t*>(key) + AlignSize(sizeof(KeyInfo)) +
           AlignSize(key->nameLength);
  }
  const char* GetKeyName(KeyInfo* key) {
    return reinterpret_cast<const char*>(key) + AlignSize(sizeof(KeyInfo));
  }

  static constexpr uint32_t FLASH_HEADER = 0x12345678;
  static constexpr uint32_t CHECKSUM_BYTE = 0x9abcedf0;

  Flash& flash_;
  FlashInfo* info_main_;
  FlashInfo* info_backup_;
  uint32_t block_size_;
  uint8_t* write_buffer_;

  void InitBlock(FlashInfo* block) {
    flash_.Erase(reinterpret_cast<uint8_t*>(block) -
                     reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
                 block_size_);
    FlashInfo info = {FLASH_HEADER, {}, {1, 1, 0, 0, 0}};
    Write(reinterpret_cast<uint8_t*>(block) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          {reinterpret_cast<uint8_t*>(&info), sizeof(FlashInfo)});
    Write(reinterpret_cast<uint8_t*>(block) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_) +
              block_size_ - AlignSize(sizeof(CHECKSUM_BYTE)),
          {&CHECKSUM_BYTE, sizeof(CHECKSUM_BYTE)});
  }

  bool IsBlockInited(FlashInfo* block) { return block->header == FLASH_HEADER; }
  bool IsBlockEmpty(FlashInfo* block) { return block->key.available_flag == 1; }
  bool IsBlockError(FlashInfo* block) {
    auto checksum_byte_addr = reinterpret_cast<uint8_t*>(block) + block_size_ -
                              AlignSize(sizeof(CHECKSUM_BYTE));
    return *reinterpret_cast<uint32_t*>(checksum_byte_addr) != CHECKSUM_BYTE;
  }
  size_t GetKeySize(KeyInfo* key) {
    return AlignSize(sizeof(KeyInfo)) + AlignSize(key->nameLength) +
           AlignSize(key->dataSize);
  }
  KeyInfo* GetNextKey(KeyInfo* key) {
    return reinterpret_cast<KeyInfo*>(reinterpret_cast<uint8_t*>(key) +
                                      GetKeySize(key));
  }
  KeyInfo* GetLastKey(FlashInfo* block) {
    if (IsBlockEmpty(block)) {
      return nullptr;
    }

    KeyInfo* key = &block->key;
    while (!key->no_next_key) {
      key = GetNextKey(key);
    }
    return key;
  }
  KeyInfo* SearchKey(const char* name) {
    if (IsBlockEmpty(info_main_)) {
      return nullptr;
    }

    KeyInfo* key = &info_main_->key;

    while (true) {
      if (!key->available_flag) {
        key = GetNextKey(key);
        continue;
      }

      if (strcmp(name, GetKeyName(key)) == 0) {
        return key;
      }
      if (key->no_next_key) {
        break;
      }

      key = GetNextKey(key);
    }

    return nullptr;
  }

  ErrorCode Add(KeyBase& key) override {
    return AddKey(key.name_, key.raw_data_.addr_, key.raw_data_.size_);
  }

  size_t AlignSize(size_t size) {
    return static_cast<size_t>((size + MinWriteSize - 1) / MinWriteSize) *
           MinWriteSize;
  }

  ErrorCode Write(size_t offset, ConstRawData data) {
    if (data.size_ == 0) {
      return ErrorCode::OK;
    }

    if (data.size_ % MinWriteSize == 0) {
      return flash_.Write(offset, data);
    } else {
      auto final_block_index = data.size_ - data.size_ % MinWriteSize;
      if (final_block_index != 0) {
        flash_.Write(offset, {data.addr_, final_block_index});
      }
      memset(write_buffer_, 0xff, MinWriteSize);
      memcpy(write_buffer_,
             reinterpret_cast<const uint8_t*>(data.addr_) + final_block_index,
             data.size_ % MinWriteSize);
      return flash_.Write(offset + final_block_index,
                          {write_buffer_, MinWriteSize});
    }
  }

 public:
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
    memcpy(key.raw_data_.addr_, data.addr_, data.size_);
    return SetKey(key.name_, data.addr_, data.size_);
  }
};

}  // namespace LibXR
