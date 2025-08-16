#include "database.hpp"

#include <cstddef>
#include <cstdint>

#include "libxr_def.hpp"

using namespace LibXR;

DatabaseRawSequential::DatabaseRawSequential(Flash& flash, size_t max_buffer_size)
    : flash_(flash), max_buffer_size_(max_buffer_size)
{
  ASSERT(flash.MinEraseSize() * 2 <= flash.Size());
  if (max_buffer_size * 2 > flash.Size())
  {
    max_buffer_size = flash.Size() / 2;
  }
  auto block_num = static_cast<size_t>(flash.Size() / flash.MinEraseSize());
  block_size_ = block_num / 2 * flash.MinEraseSize();
  buffer_ = new uint8_t[max_buffer_size];

  Init();
}

void DatabaseRawSequential::Init()
{
  memset(buffer_, 0xFF, max_buffer_size_);
  FlashInfo* flash_data_ = reinterpret_cast<FlashInfo*>(buffer_);
  flash_data_->header = FLASH_HEADER;
  flash_data_->key = {0, 0, 0};
  buffer_[max_buffer_size_ - 1] = CHECKSUM_BYTE;

  if (!IsBlockInited(BlockType::BACKUP) || IsBlockError(BlockType::BACKUP))
  {
    InitBlock(BlockType::BACKUP);
  }

  if (IsBlockError(BlockType::MAIN))
  {
    if (IsBlockEmpty(BlockType::BACKUP))
    {
      InitBlock(BlockType::MAIN);
    }
    else
    {
      flash_.Read(block_size_, {buffer_, max_buffer_size_});
      flash_.Erase(0, block_size_);
      flash_.Write(0, {buffer_, max_buffer_size_});
    }
  }

  if (!IsBlockEmpty(BlockType::BACKUP))
  {
    InitBlock(BlockType::BACKUP);
  }

  Load();
}

void DatabaseRawSequential::Save()
{
  flash_.Erase(block_size_, block_size_);
  flash_.Write(block_size_, {buffer_, max_buffer_size_});

  flash_.Erase(0, block_size_);
  flash_.Write(0, {buffer_, max_buffer_size_});
}

void DatabaseRawSequential::Load() { flash_.Read(0, {buffer_, max_buffer_size_}); }

void DatabaseRawSequential::Restore()
{
  memset(buffer_, 0xFF, max_buffer_size_);
  FlashInfo* flash_data_ = reinterpret_cast<FlashInfo*>(buffer_);
  flash_data_->header = FLASH_HEADER;
  flash_data_->key = {0, 0, 0};
  buffer_[max_buffer_size_ - 1] = CHECKSUM_BYTE;
  flash_.Write(0, {buffer_, max_buffer_size_});
  flash_.Write(block_size_, {buffer_, max_buffer_size_});
}

ErrorCode DatabaseRawSequential::Get(Database::KeyBase& key)
{
  auto ans = SearchKey(key.name_);
  if (!ans)
  {
    return ErrorCode::NOT_FOUND;
  }

  KeyInfo key_buffer;
  flash_.Read(ans, key_buffer);

  if (key.raw_data_.size_ != key_buffer.GetDataSize())
  {
    return ErrorCode::FAILED;
  }

  GetKeyData(ans, key.raw_data_);

  return ErrorCode::OK;
}

void DatabaseRawSequential::InitBlock(BlockType block)
{
  size_t offset = 0;
  if (block == BlockType::BACKUP)
  {
    offset = block_size_;
  }

  flash_.Erase(offset, block_size_);
  flash_.Write(offset, {buffer_, max_buffer_size_});
}

/**
 * @brief 判断块是否已初始化 (Check if block is initialized).
 * @param block 需要检查的块 (Block to check).
 * @return 如果已初始化返回 true，否则返回 false (Returns true if initialized, false
 * otherwise).
 */
bool DatabaseRawSequential::IsBlockInited(BlockType block)
{
  size_t offset = 0;
  if (block == BlockType::BACKUP)
  {
    offset = block_size_;
  }
  FlashInfo flash_data_;
  flash_.Read(offset, flash_data_);
  return flash_data_.header == FLASH_HEADER;
}

/**
 * @brief 判断块是否为空 (Check if block is empty).
 * @param block 需要检查的块 (Block to check).
 * @return 如果为空返回 true，否则返回 false (Returns true if empty, false otherwise).
 */
bool DatabaseRawSequential::IsBlockEmpty(BlockType block)
{
  size_t offset = 0;
  if (block == BlockType::BACKUP)
  {
    offset = block_size_;
  }
  FlashInfo flash_data_;
  flash_.Read(offset, flash_data_);
  return flash_data_.key.GetNameLength() == 0;
}

/**
 * @brief 判断块是否损坏 (Check if block has an error).
 * @param block 需要检查的块 (Block to check).
 * @return 如果损坏返回 true，否则返回 false (Returns true if corrupted, false otherwise).
 */
bool DatabaseRawSequential::IsBlockError(BlockType block)
{
  size_t offset = 0;
  if (block == BlockType::BACKUP)
  {
    offset = block_size_;
  }
  uint8_t checksum_byte = 0;
  flash_.Read(offset + max_buffer_size_ / sizeof(CHECKSUM_BYTE) - 1, checksum_byte);
  return checksum_byte != CHECKSUM_BYTE;
}

bool DatabaseRawSequential::HasLastKey(size_t offset)
{
  KeyInfo key;
  flash_.Read(offset, key);
  return key.GetNextKeyExist();
}

size_t DatabaseRawSequential::GetKeySize(size_t offset)
{
  KeyInfo key;
  flash_.Read(offset, key);
  return sizeof(KeyInfo) + key.GetNameLength() + key.GetDataSize();
}

size_t DatabaseRawSequential::GetNextKey(size_t offset)
{
  return offset + GetKeySize(offset);
}

size_t DatabaseRawSequential::GetLastKey(BlockType block)
{
  if (IsBlockEmpty(block))
  {
    return 0;
  }

  size_t offset = OFFSET_OF(FlashInfo, key);
  while (HasLastKey(offset))
  {
    offset = GetNextKey(offset);
  }
  return offset;
}

void DatabaseRawSequential::SetNestKeyExist(size_t offset, bool exist)
{
  KeyInfo key;
  flash_.Read(offset, key);
  key.SetNextKeyExist(exist);
  memcpy(buffer_ + offset, &key, sizeof(KeyInfo));
}

bool DatabaseRawSequential::KeyDataCompare(size_t offset, const void* data, size_t size)
{
  KeyInfo key;
  flash_.Read(offset, key);
  size_t key_data_offset = offset + sizeof(KeyInfo) + key.GetNameLength();
  uint8_t data_buffer;
  for (size_t i = 0; i < size; i++)
  {
    flash_.Read(key_data_offset + i, data_buffer);
    if (data_buffer != ((uint8_t*)data)[i])
    {
      return true;
    }
  }
  return false;
}

bool DatabaseRawSequential::KeyNameCompare(size_t offset, const char* name)
{
  KeyInfo key;
  flash_.Read(offset, key);
  for (size_t i = 0; i < key.GetNameLength(); i++)
  {
    uint8_t data_buffer;
    flash_.Read(offset + sizeof(KeyInfo) + i, data_buffer);
    if (data_buffer != name[i])
    {
      return true;
    }
  }
  return false;
}

ErrorCode DatabaseRawSequential::AddKey(const char* name, const void* data, size_t size)
{
  if (auto ans = SearchKey(name))
  {
    return SetKey(ans, data, size);
  }

  const uint32_t NAME_LEN = strlen(name) + 1;
  size_t last_key_offset = GetLastKey(BlockType::MAIN);
  size_t key_buf_offset =
      last_key_offset ? GetNextKey(last_key_offset) : OFFSET_OF(FlashInfo, key);

  size_t end_pos_offset = key_buf_offset + sizeof(KeyInfo) + NAME_LEN + size;
  if (end_pos_offset > max_buffer_size_ - 1)
  {
    return ErrorCode::FULL;
  }

  size_t data_ptr_offset = key_buf_offset + sizeof(KeyInfo);
  memcpy(buffer_ + data_ptr_offset, name, NAME_LEN);
  memcpy(buffer_ + data_ptr_offset + NAME_LEN, data, size);

  KeyInfo key_buf = {0, static_cast<uint8_t>(NAME_LEN), static_cast<uint32_t>(size)};
  memcpy(buffer_ + key_buf_offset, &key_buf, sizeof(KeyInfo));
  if (last_key_offset)
  {
    SetNestKeyExist(last_key_offset, 1);
  }

  Save();
  return ErrorCode::OK;
}

ErrorCode DatabaseRawSequential::SetKey(const char* name, const void* data, size_t size)
{
  if (size_t key_offset = SearchKey(name))
  {
    return SetKey(key_offset, data, size);
  }
  return ErrorCode::FAILED;
}

ErrorCode DatabaseRawSequential::SetKey(size_t offset, const void* data, size_t size)
{
  ASSERT(offset != 0);

  KeyInfo key;
  flash_.Read(offset, key);

  if (key.GetDataSize() == size)
  {
    if (KeyDataCompare(offset, data, size))
    {
      memcpy(buffer_ + offset + sizeof(KeyInfo) + key.GetNameLength(), data, size);
      Save();
    }
    return ErrorCode::OK;
  }

  return ErrorCode::FAILED;
}

ErrorCode DatabaseRawSequential::GetKeyData(size_t offset, RawData data)
{
  KeyInfo key;
  flash_.Read(offset, key);
  if (key.GetDataSize() > data.size_)
  {
    return ErrorCode::FAILED;
  }
  auto data_offset = offset + sizeof(KeyInfo) + key.GetNameLength();
  flash_.Read(data_offset, {data.addr_, key.GetDataSize()});
  return ErrorCode::OK;
}

size_t DatabaseRawSequential::SearchKey(const char* name)
{
  if (IsBlockEmpty(BlockType::MAIN))
  {
    return 0;
  }

  size_t key_offset = OFFSET_OF(FlashInfo, key);
  while (true)
  {
    if (KeyNameCompare(key_offset, name) == 0)
    {
      return key_offset;
    }
    if (!HasLastKey(key_offset))
    {
      break;
    }
    key_offset = GetNextKey(key_offset);
  }
  return 0;
}
