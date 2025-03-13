#include "database.hpp"

#include <cstddef>
#include <cstdint>

#include "libxr_def.hpp"

using namespace LibXR;

DatabaseRawSequential::DatabaseRawSequential(Flash& flash,
                                             size_t max_buffer_size)
    : flash_(flash), max_buffer_size_(max_buffer_size) {
  ASSERT(flash.min_erase_size_ * 2 <= flash.flash_area_.size_);
  if (max_buffer_size * 2 > flash.flash_area_.size_) {
    max_buffer_size = flash.flash_area_.size_ / 2;
  }
  auto block_num =
      static_cast<size_t>(flash.flash_area_.size_ / flash.min_erase_size_);
  block_size_ = block_num / 2 * flash.min_erase_size_;
  data_buffer_ = new uint8_t[max_buffer_size];
  flash_data_ = reinterpret_cast<FlashInfo*>(data_buffer_);
  info_main_ = reinterpret_cast<FlashInfo*>(flash.flash_area_.addr_);
  info_backup_ = reinterpret_cast<FlashInfo*>(
      reinterpret_cast<uint8_t*>(flash.flash_area_.addr_) + block_size_);
  Init();
}

void DatabaseRawSequential::Init() {
  memset(data_buffer_, 0xFF, max_buffer_size_);
  flash_data_->header = FLASH_HEADER;
  flash_data_->key = {0, 0, 0};
  data_buffer_[max_buffer_size_ - 1] = CHECKSUM_BYTE;

  if (!IsBlockInited(info_backup_) || IsBlockError(info_backup_)) {
    InitBlock(info_backup_);
  }

  if (IsBlockError(info_main_)) {
    if (IsBlockEmpty(info_backup_)) {
      InitBlock(info_main_);
    } else {
      flash_.Erase(0, block_size_);
      flash_.Write(
          0, {reinterpret_cast<uint8_t*>(info_backup_), max_buffer_size_});
    }
  }

  memcpy(data_buffer_, info_main_, max_buffer_size_);
}

void DatabaseRawSequential::Save() {
  flash_.Erase(block_size_, block_size_);
  flash_.Write(block_size_,
               {reinterpret_cast<uint8_t*>(info_main_), max_buffer_size_});

  flash_.Erase(0, block_size_);
  flash_.Write(0, {data_buffer_, max_buffer_size_});
}

void DatabaseRawSequential::Load() {
  memcpy(data_buffer_, info_main_, max_buffer_size_);
}

void DatabaseRawSequential::Restore() {
  flash_.Erase(0, max_buffer_size_);
  flash_.Erase(block_size_, max_buffer_size_);
}

void DatabaseRawSequential::InitBlock(FlashInfo* block) {
  flash_.Erase(reinterpret_cast<size_t>(block) -
                   reinterpret_cast<size_t>(flash_.flash_area_.addr_),
               block_size_);
  flash_.Write(reinterpret_cast<size_t>(block) -
                   reinterpret_cast<size_t>(flash_.flash_area_.addr_),
               {data_buffer_, block_size_});
}

bool DatabaseRawSequential::IsBlockInited(FlashInfo* block) {
  return block->header == FLASH_HEADER;
}

bool DatabaseRawSequential::IsBlockEmpty(FlashInfo* block) {
  return block->key.nameLength == 0;
}

bool DatabaseRawSequential::IsBlockError(FlashInfo* block) {
  return reinterpret_cast<uint8_t*>(block)[max_buffer_size_ - 1] !=
         CHECKSUM_BYTE;
}

size_t DatabaseRawSequential::GetKeySize(KeyInfo* key) {
  return sizeof(KeyInfo) + key->nameLength + key->dataSize;
}

DatabaseRawSequential::KeyInfo* DatabaseRawSequential::GetNextKey(
    KeyInfo* key) {
  return reinterpret_cast<KeyInfo*>(reinterpret_cast<uint8_t*>(key) +
                                    GetKeySize(key));
}

DatabaseRawSequential::KeyInfo* DatabaseRawSequential::GetLastKey(
    FlashInfo* block) {
  if (IsBlockEmpty(block)) {
    return nullptr;
  }

  KeyInfo* key = &block->key;
  while (key->nextKey) {
    key = GetNextKey(key);
  }
  return key;
}

ErrorCode DatabaseRawSequential::AddKey(const char* name, const void* data,
                                        size_t size) {
  if (auto ans = SearchKey(name)) {
    return SetKey(ans, data, size);
  }

  const uint32_t NAME_LEN = strlen(name) + 1;
  KeyInfo* last_key = GetLastKey(flash_data_);
  KeyInfo* key_buf = last_key ? GetNextKey(last_key) : &flash_data_->key;

  const uint8_t* end_pos =
      reinterpret_cast<uint8_t*>(key_buf) + sizeof(KeyInfo) + NAME_LEN + size;
  if (end_pos > data_buffer_ + max_buffer_size_ - 1) {
    return ErrorCode::FULL;
  }

  uint8_t* data_ptr = reinterpret_cast<uint8_t*>(key_buf) + sizeof(KeyInfo);
  memcpy(data_ptr, name, NAME_LEN);
  memcpy(data_ptr + NAME_LEN, data, size);

  *key_buf = KeyInfo{0, NAME_LEN, size};
  if (last_key) {
    last_key->nextKey = 1;
  }

  Save();

  return ErrorCode::OK;
}

ErrorCode DatabaseRawSequential::SetKey(const char* name, const void* data,
                                        size_t size) {
  if (KeyInfo* key = SearchKey(name)) {
    return SetKey(key, data, size);
  }

  return ErrorCode::FAILED;
}

ErrorCode DatabaseRawSequential::SetKey(KeyInfo* key, const void* data,
                                        size_t size) {
  ASSERT(key != nullptr);

  if (key->dataSize == size) {
    if (memcmp(GetKeyData(key), data, size) != 0) {
      memcpy(GetKeyData(key), data, size);
      Save();
    }
    return ErrorCode::OK;
  }

  return ErrorCode::FAILED;
}

uint8_t* DatabaseRawSequential::GetKeyData(KeyInfo* key) {
  return reinterpret_cast<uint8_t*>(key) + sizeof(KeyInfo) + key->nameLength;
}

const char* DatabaseRawSequential::GetKeyName(KeyInfo* key) {
  return reinterpret_cast<const char*>(key) + sizeof(KeyInfo);
}

DatabaseRawSequential::KeyInfo* DatabaseRawSequential::SearchKey(
    const char* name) {
  if (IsBlockEmpty(flash_data_)) {
    return nullptr;
  }

  KeyInfo* key = &flash_data_->key;

  while (true) {
    if (strcmp(name, GetKeyName(key)) == 0) {
      return key;
    }
    if (!key->nextKey) {
      break;
    }

    key = GetNextKey(key);
  }

  return nullptr;
}
