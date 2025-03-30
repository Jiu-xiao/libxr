#include "database.hpp"

#include <cstddef>
#include <cstdint>

#include "libxr_def.hpp"

using namespace LibXR;

/**
 * @brief 构造函数，初始化数据库存储，并设置缓冲区 (Constructor to initialize database
 * storage and set buffer).
 * @param flash 闪存对象 (Flash object).
 * @param max_buffer_size 最大缓冲区大小 (Maximum buffer size).
 */
DatabaseRawSequential::DatabaseRawSequential(Flash& flash, size_t max_buffer_size)
    : flash_(flash), max_buffer_size_(max_buffer_size)
{
  ASSERT(flash.min_erase_size_ * 2 <= flash.flash_area_.size_);
  if (max_buffer_size * 2 > flash.flash_area_.size_)
  {
    max_buffer_size = flash.flash_area_.size_ / 2;
  }
  auto block_num = static_cast<size_t>(flash.flash_area_.size_ / flash.min_erase_size_);
  block_size_ = block_num / 2 * flash.min_erase_size_;
  data_buffer_ = new uint8_t[max_buffer_size];
  flash_data_ = reinterpret_cast<FlashInfo*>(data_buffer_);
  info_main_ = reinterpret_cast<FlashInfo*>(flash.flash_area_.addr_);
  info_backup_ = reinterpret_cast<FlashInfo*>(
      reinterpret_cast<uint8_t*>(flash.flash_area_.addr_) + block_size_);
  Init();
}

/**
 * @brief 初始化数据库存储，确保主备块正确 (Initialize database storage, ensuring main and
 * backup blocks are valid).
 */
void DatabaseRawSequential::Init()
{
  memset(data_buffer_, 0xFF, max_buffer_size_);
  flash_data_->header = FLASH_HEADER;
  flash_data_->key = {0, 0, 0};
  data_buffer_[max_buffer_size_ - 1] = CHECKSUM_BYTE;

  if (!IsBlockInited(info_backup_) || IsBlockError(info_backup_))
  {
    InitBlock(info_backup_);
  }

  if (IsBlockError(info_main_))
  {
    if (IsBlockEmpty(info_backup_))
    {
      InitBlock(info_main_);
    }
    else
    {
      flash_.Erase(0, block_size_);
      flash_.Write(0, {reinterpret_cast<uint8_t*>(info_backup_), max_buffer_size_});
    }
  }

  memcpy(data_buffer_, info_main_, max_buffer_size_);
}

/**
 * @brief 保存数据到存储器 (Save data to storage).
 */
void DatabaseRawSequential::Save()
{
  flash_.Erase(block_size_, block_size_);
  flash_.Write(block_size_, {reinterpret_cast<uint8_t*>(info_main_), max_buffer_size_});

  flash_.Erase(0, block_size_);
  flash_.Write(0, {data_buffer_, max_buffer_size_});
}

/**
 * @brief 加载数据到缓冲区 (Load data into buffer).
 */
void DatabaseRawSequential::Load() { memcpy(data_buffer_, info_main_, max_buffer_size_); }

/**
 * @brief 还原存储数据 (Restore storage data).
 */
void DatabaseRawSequential::Restore()
{
  flash_.Erase(0, max_buffer_size_);
  flash_.Erase(block_size_, max_buffer_size_);
}

/**
 * @brief 初始化块数据 (Initialize block data).
 * @param block 需要初始化的块 (Block to initialize).
 */
void DatabaseRawSequential::InitBlock(FlashInfo* block)
{
  flash_.Erase(reinterpret_cast<size_t>(block) -
                   reinterpret_cast<size_t>(flash_.flash_area_.addr_),
               block_size_);
  flash_.Write(reinterpret_cast<size_t>(block) -
                   reinterpret_cast<size_t>(flash_.flash_area_.addr_),
               {data_buffer_, block_size_});
}

/**
 * @brief 判断块是否已初始化 (Check if block is initialized).
 * @param block 需要检查的块 (Block to check).
 * @return 如果已初始化返回 true，否则返回 false (Returns true if initialized, false
 * otherwise).
 */
bool DatabaseRawSequential::IsBlockInited(FlashInfo* block)
{
  return block->header == FLASH_HEADER;
}

/**
 * @brief 判断块是否为空 (Check if block is empty).
 * @param block 需要检查的块 (Block to check).
 * @return 如果为空返回 true，否则返回 false (Returns true if empty, false otherwise).
 */
bool DatabaseRawSequential::IsBlockEmpty(FlashInfo* block)
{
  return block->key.nameLength == 0;
}

/**
 * @brief 判断块是否损坏 (Check if block has an error).
 * @param block 需要检查的块 (Block to check).
 * @return 如果损坏返回 true，否则返回 false (Returns true if corrupted, false otherwise).
 */
bool DatabaseRawSequential::IsBlockError(FlashInfo* block)
{
  return reinterpret_cast<uint8_t*>(
             block)[max_buffer_size_ / sizeof(CHECKSUM_BYTE) - 1] != CHECKSUM_BYTE;
}

/**
 * @brief 计算键的总大小 (Calculate total size of a key).
 * @param key 键信息 (Key information).
 * @return 键的大小 (Size of the key).
 */
size_t DatabaseRawSequential::GetKeySize(KeyInfo* key)
{
  return sizeof(KeyInfo) + key->nameLength + key->dataSize;
}

/**
 * @brief 获取下一个键 (Get next key).
 * @param key 当前键 (Current key).
 * @return 下一个键 (Next key).
 */
DatabaseRawSequential::KeyInfo* DatabaseRawSequential::GetNextKey(KeyInfo* key)
{
  return reinterpret_cast<KeyInfo*>(reinterpret_cast<uint8_t*>(key) + GetKeySize(key));
}

/**
 * @brief 获取块中的最后一个键 (Get last key in block).
 * @param block 目标块 (Block to check).
 * @return 最后一个键的指针，如果块为空返回 nullptr (Pointer to last key, or nullptr if
 * block is empty).
 */
DatabaseRawSequential::KeyInfo* DatabaseRawSequential::GetLastKey(FlashInfo* block)
{
  if (IsBlockEmpty(block))
  {
    return nullptr;
  }

  KeyInfo* key = &block->key;
  while (key->nextKey)
  {
    key = GetNextKey(key);
  }
  return key;
}

/**
 * @brief 添加键值对到数据库 (Add key-value pair to database).
 * @param name 键名 (Key name).
 * @param data 数据 (Data value).
 * @param size 数据大小 (Size of data).
 * @return 操作结果 (Operation result).
 */
ErrorCode DatabaseRawSequential::AddKey(const char* name, const void* data, size_t size)
{
  if (auto ans = SearchKey(name))
  {
    return SetKey(ans, data, size);
  }

  const uint32_t NAME_LEN = strlen(name) + 1;
  KeyInfo* last_key = GetLastKey(flash_data_);
  KeyInfo* key_buf = last_key ? GetNextKey(last_key) : &flash_data_->key;

  const uint8_t* end_pos =
      reinterpret_cast<uint8_t*>(key_buf) + sizeof(KeyInfo) + NAME_LEN + size;
  if (end_pos > data_buffer_ + max_buffer_size_ - 1)
  {
    return ErrorCode::FULL;
  }

  uint8_t* data_ptr = reinterpret_cast<uint8_t*>(key_buf) + sizeof(KeyInfo);
  memcpy(data_ptr, name, NAME_LEN);
  memcpy(data_ptr + NAME_LEN, data, size);

  *key_buf = KeyInfo{0, NAME_LEN, size};
  if (last_key)
  {
    last_key->nextKey = 1;
  }

  Save();
  return ErrorCode::OK;
}

/**
 * @brief 设置键值 (Set key value).
 * @param name 键名 (Key name).
 * @param data 数据 (Data value).
 * @param size 数据大小 (Size of data).
 * @return 操作结果 (Operation result).
 */
ErrorCode DatabaseRawSequential::SetKey(const char* name, const void* data, size_t size)
{
  if (KeyInfo* key = SearchKey(name))
  {
    return SetKey(key, data, size);
  }
  return ErrorCode::FAILED;
}

/**
 * @brief 设置指定键的值 (Set the value of a specified key).
 *
 * This function updates the key's data if the new size matches the existing size.
 * If the data differs, it will be updated and saved.
 * 此函数在新数据大小与现有数据大小匹配时更新键的值。如果数据不同，则会进行更新并保存。
 *
 * @param key 指向要更新的键的指针 (Pointer to the key to update).
 * @param data 指向要写入的数据 (Pointer to the data to be written).
 * @param size 数据大小 (Size of the data).
 * @return 返回操作结果，成功返回 ErrorCode::OK，失败返回 ErrorCode::FAILED
 *         (Returns the operation result: ErrorCode::OK on success, ErrorCode::FAILED on
 * failure).
 */
ErrorCode DatabaseRawSequential::SetKey(KeyInfo* key, const void* data, size_t size)
{
  ASSERT(key != nullptr);

  if (key->dataSize == size)
  {
    if (memcmp(GetKeyData(key), data, size) != 0)
    {
      memcpy(GetKeyData(key), data, size);
      Save();
    }
    return ErrorCode::OK;
  }

  return ErrorCode::FAILED;
}

/**
 * @brief 获取键的数据信息 (Retrieve the data associated with a key).
 *
 * This function returns a pointer to the data section of the specified key.
 * 此函数返回指向指定键的数据部分的指针。
 *
 * @param key 指向目标键的指针 (Pointer to the key whose data is retrieved).
 * @return 返回数据的起始地址 (Returns a pointer to the start of the data).
 */
uint8_t* DatabaseRawSequential::GetKeyData(KeyInfo* key)
{
  return reinterpret_cast<uint8_t*>(key) + sizeof(KeyInfo) + key->nameLength;
}

/**
 * @brief 获取键的名称 (Retrieve the name of a key).
 *
 * This function returns a pointer to the name section of the specified key.
 * 此函数返回指向指定键名称部分的指针。
 *
 * @param key 指向目标键的指针 (Pointer to the key whose name is retrieved).
 * @return 返回键名的起始地址 (Returns a pointer to the start of the key name).
 */
const char* DatabaseRawSequential::GetKeyName(KeyInfo* key)
{
  return reinterpret_cast<const char*>(key) + sizeof(KeyInfo);
}

/**
 * @brief 查找键 (Search for key).
 * @param name 需要查找的键名 (Key name to search).
 * @return 指向找到的键的指针，若不存在则返回 nullptr (Pointer to found key, or nullptr if
 * not found).
 */
DatabaseRawSequential::KeyInfo* DatabaseRawSequential::SearchKey(const char* name)
{
  if (IsBlockEmpty(flash_data_))
  {
    return nullptr;
  }

  KeyInfo* key = &flash_data_->key;
  while (true)
  {
    if (strcmp(name, GetKeyName(key)) == 0)
    {
      return key;
    }
    if (!key->nextKey)
    {
      break;
    }
    key = GetNextKey(key);
  }
  return nullptr;
}
