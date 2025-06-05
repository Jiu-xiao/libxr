#pragma once

#include <cstdint>
#include <cstring>

#include "flash.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"

namespace LibXR
{

static constexpr uint16_t LIBXR_DATABASE_VERSION = 1;

/**
 * @brief 数据库接口，提供键值存储和管理功能 (Database interface providing key-value
 * storage and management).
 */
class Database
{
 public:
  /**
   * @brief 键的基类，存储键名及其数据 (Base class for keys, storing key name and
   * associated data).
   */
  class KeyBase
  {
   public:
    const char* name_;  ///< 键名 (Key name).
    RawData raw_data_;  ///< 原始数据 (Raw data associated with the key).

    /**
     * @brief 构造函数，初始化键名和原始数据 (Constructor to initialize key name and raw
     * data).
     * @param name 键名 (Key name).
     * @param raw_data 关联的原始数据 (Raw data associated with the key).
     */
    KeyBase(const char* name, RawData raw_data) : name_(name), raw_data_(raw_data) {}
  };

  /**
   * @brief 模板类，表示数据库中的具体键 (Template class representing a specific key in
   * the database).
   * @tparam Data 存储的数据类型 (The type of data stored).
   */
  template <typename Data>
  class Key : public KeyBase
  {
   public:
    Data data_;           ///< 键存储的数据 (The data stored in the key).
    Database& database_;  ///< 关联的数据库对象 (Reference to the associated database).

    /**
     * @brief 构造函数，初始化键并从数据库加载数据 (Constructor to initialize key and load
     * data from the database).
     *
     * If the key does not exist in the database, it is initialized with the provided
     * value. 如果键在数据库中不存在，则使用提供的值进行初始化。
     *
     * @param database 关联的数据库对象 (Reference to the associated database).
     * @param name 键名 (Key name).
     * @param init_value 初始化值 (Initial value for the key).
     */
    Key(Database& database, const char* name, Data init_value)
        : KeyBase(name, RawData(data_)), database_(database)
    {
      if (database.Get(*this) == ErrorCode::NOT_FOUND)
      {
        data_ = init_value;
        database.Add(*this);
      }
    }

    /**
     * @brief 构造函数，初始化键，并在数据库不存在时赋默认值 (Constructor to initialize
     * key, assigning default value if not found in the database).
     *
     * If the key does not exist in the database, it is initialized with zero.
     * 如果键在数据库中不存在，则初始化为零。
     *
     * @param database 关联的数据库对象 (Reference to the associated database).
     * @param name 键名 (Key name).
     */
    Key(Database& database, const char* name)
        : KeyBase(name, RawData(data_)), database_(database)
    {
      if (database.Get(*this) == ErrorCode::NOT_FOUND)
      {
        memset(&data_, 0, sizeof(Data));
        database.Add(*this);
      }
    }

    /**
     * @brief 类型转换运算符，返回存储的数据 (Type conversion operator returning stored
     * data).
     * @return 存储的数据 (Stored data).
     */
    operator Data() { return data_; }

    /**
     * @brief 设置键的值并更新数据库 (Set the key's value and update the database).
     * @param data 需要存储的新值 (New value to store).
     * @return 操作结果 (Operation result).
     */
    ErrorCode Set(Data data) { return database_.Set(*this, RawData(data)); }

    /**
     * @brief 从数据库加载键的值 (Load the key's value from the database).
     * @return 操作结果 (Operation result).
     */
    ErrorCode Load() { return database_.Get(*this); }

    /**
     * @brief 赋值运算符，设置键的值 (Assignment operator to set the key's value).
     * @param data 需要存储的新值 (New value to store).
     * @return 操作结果 (Operation result).
     */
    ErrorCode operator=(Data data) { return Set(data); }  // NOLINT
  };

 private:
  /**
   * @brief 从数据库获取键的值 (Retrieve the key's value from the database).
   * @param key 需要获取的键 (Key to retrieve).
   * @return 操作结果 (Operation result).
   */
  virtual ErrorCode Get(KeyBase& key) = 0;

  /**
   * @brief 设置数据库中的键值 (Set the key's value in the database).
   * @param key 目标键 (Target key).
   * @param data 需要存储的新值 (New value to store).
   * @return 操作结果 (Operation result).
   */
  virtual ErrorCode Set(KeyBase& key, RawData data) = 0;

  /**
   * @brief 添加新键到数据库 (Add a new key to the database).
   * @param key 需要添加的键 (Key to add).
   * @return 操作结果 (Operation result).
   */
  virtual ErrorCode Add(KeyBase& key) = 0;
};

/**
 * @brief 适用于不支持逆序写入的 Flash 存储的数据库实现
 *        (Database implementation for Flash storage that does not support reverse
 * writing).
 *
 * This class manages key-value storage in a Flash memory region where
 * data can only be written sequentially. It maintains a backup system
 * to prevent data corruption.
 * 此类管理 Flash 内存区域中的键值存储，其中数据只能顺序写入。
 * 它维护一个备份系统，以防止数据损坏。
 */
class DatabaseRawSequential : public Database
{
 public:
  /**
   * @brief 构造函数，初始化 Flash 存储和缓冲区
   *        (Constructor initializing Flash storage and buffer).
   *
   * @param flash 目标 Flash 存储设备 (Target Flash storage device).
   * @param max_buffer_size 最大缓冲区大小，默认 256 字节 (Maximum buffer size, default is
   * 256 bytes).
   */
  explicit DatabaseRawSequential(Flash& flash, size_t max_buffer_size = 256);

  /**
   * @brief 初始化数据库存储区，确保主备块正确
   *        (Initialize database storage, ensuring main and backup blocks are valid).
   */
  void Init();

  /**
   * @brief 保存当前缓冲区内容到 Flash
   *        (Save the current buffer content to Flash).
   */
  void Save();

  /**
   * @brief 从 Flash 加载数据到缓冲区
   *        (Load data from Flash into the buffer).
   */
  void Load();

  /**
   * @brief 还原存储数据，清空 Flash 区域
   *        (Restore storage data, clearing Flash memory area).
   */
  void Restore();

 private:
  /**
   * @brief 键信息结构，存储键的元数据
   *        (Structure containing key metadata).
   */
  typedef struct __attribute__((packed))
  {
    uint8_t nextKey : 1;      ///< 是否有下一个键 (Indicates if there is a next key).
    uint32_t nameLength : 7;  ///< 键名长度 (Length of the key name).
    size_t dataSize : 24;     ///< 数据大小 (Size of the stored data).
  } KeyInfo;

  /**
   * @brief Flash 存储的块信息结构
   *        (Structure representing a Flash storage block).
   */
  struct FlashInfo
  {
    uint32_t header;  ///< Flash 块头标识 (Flash block header identifier).
    KeyInfo key;      ///< 该块的键信息 (Key metadata in this block).
  };

  ErrorCode AddKey(const char* name, const void* data, size_t size);
  ErrorCode SetKey(const char* name, const void* data, size_t size);
  ErrorCode SetKey(KeyInfo* key, const void* data, size_t size);

  /**
   * @brief 获取指定键的数据信息
   *        (Retrieve the data associated with a key).
   * @param key 目标键 (Key to retrieve data from).
   * @return 指向数据的指针 (Pointer to the key's data).
   */
  uint8_t* GetKeyData(KeyInfo* key);

  /**
   * @brief 获取键的名称
   *        (Retrieve the name of a key).
   * @param key 目标键 (Key to retrieve the name from).
   * @return 指向键名的指针 (Pointer to the key name).
   */
  const char* GetKeyName(KeyInfo* key);

  static constexpr uint32_t FLASH_HEADER =
      0x12345678 + LIBXR_DATABASE_VERSION;  ///< Flash 头部标识 (Flash header identifier).
  static constexpr uint8_t CHECKSUM_BYTE = 0x56;  ///< 校验字节 (Checksum byte).

  Flash& flash_;              ///< 目标 Flash 存储设备 (Target Flash storage device).
  uint8_t* data_buffer_;      ///< 数据缓冲区 (Data buffer).
  FlashInfo* flash_data_;     ///< Flash 数据存储区 (Pointer to the Flash data storage).
  FlashInfo* info_main_;      ///< 主存储区信息 (Main storage block information).
  FlashInfo* info_backup_;    ///< 备份存储区信息 (Backup storage block information).
  uint32_t block_size_;       ///< Flash 块大小 (Flash block size).
  uint32_t max_buffer_size_;  ///< 最大缓冲区大小 (Maximum buffer size).

  void InitBlock(FlashInfo* block);
  bool IsBlockInited(FlashInfo* block);
  bool IsBlockEmpty(FlashInfo* block);
  bool IsBlockError(FlashInfo* block);
  size_t GetKeySize(KeyInfo* key);
  KeyInfo* GetNextKey(KeyInfo* key);
  KeyInfo* GetLastKey(FlashInfo* block);
  KeyInfo* SearchKey(const char* name);

  /**
   * @brief 添加新键到数据库
   *        (Add a new key to the database).
   * @param key 需要添加的键 (Key to add).
   * @return 操作结果 (Operation result).
   */
  ErrorCode Add(KeyBase& key) override
  {
    return AddKey(key.name_, key.raw_data_.addr_, key.raw_data_.size_);
  }

 public:
  /**
   * @brief 获取数据库中的键值
   *        (Retrieve the key's value from the database).
   * @param key 需要获取的键 (Key to retrieve).
   * @return 操作结果，如果找到则返回 `ErrorCode::OK`，否则返回 `ErrorCode::NOT_FOUND`
   *         (Operation result, returns `ErrorCode::OK` if found, otherwise
   * `ErrorCode::NOT_FOUND`).
   */
  ErrorCode Get(Database::KeyBase& key) override
  {
    auto ans = SearchKey(key.name_);
    if (!ans)
    {
      return ErrorCode::NOT_FOUND;
    }

    if (key.raw_data_.size_ != ans->dataSize)
    {
      return ErrorCode::FAILED;
    }

    memcpy(key.raw_data_.addr_, GetKeyData(ans), ans->dataSize);

    return ErrorCode::OK;
  }

  /**
   * @brief 设置数据库中的键值
   *        (Set the key's value in the database).
   * @param key 目标键 (Target key).
   * @param data 需要存储的新值 (New value to store).
   * @return 操作结果 (Operation result).
   */
  ErrorCode Set(KeyBase& key, RawData data) override
  {
    return SetKey(key.name_, data.addr_, data.size_);
  }
};

/**
 * @brief 适用于最小写入单元受限的 Flash 存储的数据库实现
 *        (Database implementation for Flash storage with minimum write unit
 * restrictions).
 *
 * This class provides key-value storage management for Flash memory that
 * requires data to be written in fixed-size blocks.
 * 此类提供适用于 Flash 存储的键值存储管理，该存储要求数据以固定大小块写入。
 *
 * @tparam MinWriteSize Flash 的最小写入单元大小 (Minimum write unit size for Flash
 * storage).
 */
template <size_t MinWriteSize>
class DatabaseRaw : public Database
{
 public:
  template <size_t BlockSize>
  struct __attribute__((packed)) BlockBoolData
  {
    uint8_t data[BlockSize];
  };

  template <size_t BlockSize>
  class BlockBoolUtil
  {
   public:
    static void SetFlag(BlockBoolData<BlockSize>& obj, bool value)
    {
      memset(obj.data, 0xFF, BlockSize);
      if (!value)
      {
        obj.data[BlockSize - 1] &= 0xF0;
      }
    }

    static bool ReadFlag(const BlockBoolData<BlockSize>& obj)
    {
      uint8_t last_4bits = obj.data[BlockSize - 1] & 0x0F;
      return last_4bits == 0x0F;
    }

    static bool Valid(const BlockBoolData<BlockSize>& obj)
    {
      if (BlockSize == 0)
      {
        return false;
      }

      for (size_t i = 0; i < BlockSize - 1; ++i)
      {
        if (obj.data[i] != 0xFF)
        {
          return false;
        }
      }

      uint8_t last_byte = obj.data[BlockSize - 1];
      if ((last_byte & 0xF0) != 0xF0)
      {
        return false;
      }

      uint8_t last_4bits = last_byte & 0x0F;
      if (!(last_4bits == 0x0F || last_4bits == 0x00))
      {
        return false;
      }

      return true;
    }
  };

  /**
   * @brief 构造函数，初始化 Flash 存储和缓冲区
   *        (Constructor to initialize Flash storage and buffer).
   *
   * @param flash 目标 Flash 存储设备 (Target Flash storage device).
   */
  explicit DatabaseRaw(Flash& flash)
      : flash_(flash), write_buffer_(new uint8_t[flash_.min_write_size_])
  {
    ASSERT(flash.min_erase_size_ * 2 <= flash.flash_area_.size_);
    ASSERT(flash_.min_write_size_ <= MinWriteSize);
    auto block_num = static_cast<size_t>(flash.flash_area_.size_ / flash.min_erase_size_);
    block_size_ = block_num / 2 * flash.min_erase_size_;
    info_main_ = reinterpret_cast<FlashInfo*>(flash.flash_area_.addr_);
    info_backup_ = reinterpret_cast<FlashInfo*>(
        reinterpret_cast<uint8_t*>(flash.flash_area_.addr_) + block_size_);
    Init();
  }

  /**
   * @brief 初始化数据库存储区，确保主备块正确
   *        (Initialize database storage, ensuring main and backup blocks are valid).
   */
  void Init()
  {
    if (!IsBlockInited(info_backup_) || IsBlockError(info_backup_))
    {
      InitBlock(info_backup_);
    }

    if (!IsBlockInited(info_main_) || IsBlockError(info_main_))
    {
      if (IsBlockEmpty(info_backup_))
      {
        InitBlock(info_main_);
      }
      else
      {
        flash_.Erase(0, block_size_);
        Write(0, {reinterpret_cast<uint8_t*>(info_backup_), block_size_});
      }
    }
    else if (!IsBlockEmpty(info_backup_))
    {
      InitBlock(info_backup_);
    }

    KeyInfo* key = &info_main_->key;
    while (!BlockBoolUtil<MinWriteSize>::ReadFlag(key->no_next_key))
    {
      key = GetNextKey(key);
      if (BlockBoolUtil<MinWriteSize>::ReadFlag(key->uninit))
      {
        InitBlock(info_main_);
        break;
      }
    }
  }

  /**
   * @brief 还原存储数据，清空 Flash 区域
   *        (Restore storage data, clearing Flash memory area).
   */
  void Restore() {}

  /**
   * @brief 回收 Flash 空间，整理数据
   *        (Recycle Flash storage space and organize data).
   *
   * Moves valid keys from the main block to the backup block and erases the main block.
   * 将主存储块中的有效键移动到备份块，并擦除主存储块。
   *
   * @return 操作结果 (Operation result).
   */
  ErrorCode Recycle()
  {
    if (IsBlockEmpty(info_main_))
    {
      return ErrorCode::OK;
    }

    KeyInfo* key = &info_main_->key;

    if (!IsBlockEmpty(info_backup_))
    {
      ASSERT(false);
      return ErrorCode::FAILED;
    }

    uint8_t* write_buff = reinterpret_cast<uint8_t*>(&info_backup_->key);

    auto new_key = KeyInfo{};
    Write(write_buff - reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_), new_key);

    write_buff += GetKeySize(&info_backup_->key);

    do
    {
      key = GetNextKey(key);

      if (!BlockBoolUtil<MinWriteSize>::ReadFlag(key->available_flag))
      {
        continue;
      }

      Write(write_buff - reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_), *key);
      write_buff += AlignSize(sizeof(KeyInfo));
      Write(write_buff - reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
            {GetKeyName(key), key->nameLength});
      write_buff += AlignSize(key->nameLength);
      Write(write_buff - reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
            {GetKeyData(key), key->dataSize});
      write_buff += AlignSize(key->dataSize);
    } while (!BlockBoolUtil<MinWriteSize>::ReadFlag(key->no_next_key));

    flash_.Erase(0, block_size_);
    Write(0, {reinterpret_cast<uint8_t*>(info_backup_), block_size_});
    InitBlock(info_backup_);

    return ErrorCode::OK;
  }

 private:
  /**
   * @brief 键信息结构，存储键的元数据
   *        (Structure containing key metadata).
   */
  typedef struct __attribute__((packed))
  {
    BlockBoolData<MinWriteSize>
        no_next_key;  ///< 是否是最后一个键 (Indicates if this is the last key).
    BlockBoolData<MinWriteSize>
        available_flag;  ///< 该键是否有效 (Indicates if this key is available).
    BlockBoolData<MinWriteSize>
        uninit;  ///< 该键是否未初始化 (Indicates if this key is uninitialized).
    uint32_t nameLength : 7;  ///< 键名长度 (Length of the key name).
    size_t dataSize : 25;     ///< 数据大小 (Size of the stored data).
  } KeyInfo;

  /**
   * @brief Flash 存储的块信息结构
   *        (Structure representing a Flash storage block).
   */
  struct FlashInfo
  {
    uint32_t header;                    ///< Flash block header
    alignas(MinWriteSize) KeyInfo key;  ///< Align KeyInfo to MinWriteSize
  } __attribute__((packed));

  /**
   * @brief 计算可用的存储空间大小
   *        (Calculate the available storage size).
   * @return 剩余的可用字节数 (Remaining available bytes).
   */
  size_t AvailableSize()
  {
    return block_size_ - sizeof(CHECKSUM_BYTE) -
           (reinterpret_cast<uint8_t*>(GetNextKey(GetLastKey(info_main_))) -
            reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_));
  }

  ErrorCode AddKey(const char* name, const void* data, size_t size)
  {
    if (auto ans = SearchKey(name))
    {
      return SetKey(name, data, size);
    }

    bool recycle = false;

  add_again:
    const uint32_t NAME_LEN = strlen(name) + 1;
    KeyInfo* last_key = GetLastKey(info_main_);
    KeyInfo* key_buf = nullptr;

    if (!last_key)
    {
      FlashInfo flash_info;
      memset(&flash_info, 0xff, sizeof(FlashInfo));
      flash_info.header = FLASH_HEADER;
      KeyInfo& tmp_key = flash_info.key;
      BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.no_next_key, false);
      BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.available_flag, false);
      BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.uninit, false);
      tmp_key.nameLength = 0;
      tmp_key.dataSize = 0;
      Write(0, flash_info);
      key_buf = GetNextKey(&info_main_->key);
    }
    else
    {
      key_buf = GetNextKey(last_key);
    }

    if (AvailableSize() <
        AlignSize(sizeof(KeyInfo)) + AlignSize(NAME_LEN) + AlignSize(size))
    {
      if (!recycle)
      {
        Recycle();
        recycle = true;
        goto add_again;  // NOLINT
      }
      else
      {
        ASSERT(false);
        return ErrorCode::FULL;
      }
    }

    if (last_key)
    {
      KeyInfo new_last_key = {};
      BlockBoolUtil<MinWriteSize>::SetFlag(new_last_key.no_next_key, false);
      BlockBoolUtil<MinWriteSize>::SetFlag(
          new_last_key.available_flag,
          BlockBoolUtil<MinWriteSize>::ReadFlag(last_key->available_flag));
      BlockBoolUtil<MinWriteSize>::SetFlag(
          new_last_key.uninit, BlockBoolUtil<MinWriteSize>::ReadFlag(last_key->uninit));
      new_last_key.nameLength = last_key->nameLength;
      new_last_key.dataSize = last_key->dataSize;

      Write(reinterpret_cast<uint8_t*>(last_key) -
                reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
            new_last_key);
    }

    KeyInfo new_key = {};
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.no_next_key, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.available_flag, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.uninit, true);
    new_key.nameLength = NAME_LEN;
    new_key.dataSize = size;

    Write(reinterpret_cast<uint8_t*>(key_buf) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          new_key);
    Write(reinterpret_cast<const uint8_t*>(GetKeyName(key_buf)) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          {reinterpret_cast<const uint8_t*>(name), NAME_LEN});
    Write(reinterpret_cast<uint8_t*>(GetKeyData(key_buf)) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          {reinterpret_cast<const uint8_t*>(data), size});
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.uninit, false);
    Write(reinterpret_cast<uint8_t*>(key_buf) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          new_key);

    return ErrorCode::OK;
  }

  ErrorCode SetKey(const char* name, const void* data, size_t size, bool recycle = true)
  {
    if (KeyInfo* key = SearchKey(name))
    {
      if (key->dataSize == size)
      {
        if (memcmp(GetKeyData(key), data, size) != 0)
        {
          if (AvailableSize() <
              AlignSize(size) + AlignSize(sizeof(KeyInfo)) + AlignSize(key->nameLength))
          {
            if (recycle)
            {
              Recycle();
              return SetKey(name, data, size, false);
            }
            else
            {
              ASSERT(false);
              return ErrorCode::FULL;
            }
          }

          KeyInfo new_key = {};
          BlockBoolUtil<MinWriteSize>::SetFlag(
              new_key.no_next_key,
              BlockBoolUtil<MinWriteSize>::ReadFlag(key->no_next_key));
          BlockBoolUtil<MinWriteSize>::SetFlag(new_key.available_flag, false);
          BlockBoolUtil<MinWriteSize>::SetFlag(
              new_key.uninit, BlockBoolUtil<MinWriteSize>::ReadFlag(key->uninit));
          new_key.nameLength = key->nameLength;
          new_key.dataSize = size;
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

  /**
   * @brief 获取指定键的数据信息
   *        (Retrieve the data associated with a key).
   * @param key 目标键 (Key to retrieve data from).
   * @return 指向数据的指针 (Pointer to the key's data).
   */
  uint8_t* GetKeyData(KeyInfo* key)
  {
    return reinterpret_cast<uint8_t*>(key) + AlignSize(sizeof(KeyInfo)) +
           AlignSize(key->nameLength);
  }

  /**
   * @brief 获取键的名称
   *        (Retrieve the name of a key).
   * @param key 目标键 (Key to retrieve the name from).
   * @return 指向键名的指针 (Pointer to the key name).
   */
  const char* GetKeyName(KeyInfo* key)
  {
    return reinterpret_cast<const char*>(key) + AlignSize(sizeof(KeyInfo));
  }

  static constexpr uint32_t FLASH_HEADER =
      0x12345678 + LIBXR_DATABASE_VERSION;  ///< Flash 头部标识 (Flash header identifier).
  static constexpr uint32_t CHECKSUM_BYTE = 0x9abcedf0;  ///< 校验字节 (Checksum byte).

  Flash& flash_;            ///< 目标 Flash 存储设备 (Target Flash storage device).
  FlashInfo* info_main_;    ///< 主存储区信息 (Main storage block information).
  FlashInfo* info_backup_;  ///< 备份存储区信息 (Backup storage block information).
  uint32_t block_size_;     ///< Flash 块大小 (Flash block size).
  uint8_t* write_buffer_;   ///< 写入缓冲区 (Write buffer).

  void InitBlock(FlashInfo* block)
  {
    flash_.Erase(reinterpret_cast<uint8_t*>(block) -
                     reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
                 block_size_);

    FlashInfo info;
    memset(&info, 0xff, sizeof(FlashInfo));
    info.header = FLASH_HEADER;
    KeyInfo& tmp_key = info.key;
    BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.no_next_key, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.available_flag, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.uninit, false);
    tmp_key.nameLength = 0;
    tmp_key.dataSize = 0;
    Write(reinterpret_cast<uint8_t*>(block) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_),
          {reinterpret_cast<uint8_t*>(&info), sizeof(FlashInfo)});
    Write(reinterpret_cast<uint8_t*>(block) -
              reinterpret_cast<uint8_t*>(flash_.flash_area_.addr_) + block_size_ -
              AlignSize(sizeof(CHECKSUM_BYTE)),
          {&CHECKSUM_BYTE, sizeof(CHECKSUM_BYTE)});
  }

  bool IsBlockInited(FlashInfo* block) { return block->header == FLASH_HEADER; }
  bool IsBlockEmpty(FlashInfo* block)
  {
    return BlockBoolUtil<MinWriteSize>::ReadFlag(block->key.available_flag) == true;
  }
  bool IsBlockError(FlashInfo* block)
  {
    auto checksum_byte_addr = reinterpret_cast<uint8_t*>(block) + block_size_ -
                              AlignSize(sizeof(CHECKSUM_BYTE));
    return *reinterpret_cast<uint32_t*>(checksum_byte_addr) != CHECKSUM_BYTE;
  }
  size_t GetKeySize(KeyInfo* key)
  {
    return AlignSize(sizeof(KeyInfo)) + AlignSize(key->nameLength) +
           AlignSize(key->dataSize);
  }
  KeyInfo* GetNextKey(KeyInfo* key)
  {
    return reinterpret_cast<KeyInfo*>(reinterpret_cast<uint8_t*>(key) + GetKeySize(key));
  }
  KeyInfo* GetLastKey(FlashInfo* block)
  {
    if (IsBlockEmpty(block))
    {
      return nullptr;
    }

    KeyInfo* key = &block->key;
    while (!BlockBoolUtil<MinWriteSize>::ReadFlag(key->no_next_key))
    {
      key = GetNextKey(key);
    }
    return key;
  }
  KeyInfo* SearchKey(const char* name)
  {
    if (IsBlockEmpty(info_main_))
    {
      return nullptr;
    }

    KeyInfo* key = &info_main_->key;

    while (true)
    {
      if (!BlockBoolUtil<MinWriteSize>::ReadFlag(key->available_flag))
      {
        key = GetNextKey(key);
        continue;
      }

      if (strcmp(name, GetKeyName(key)) == 0)
      {
        return key;
      }
      if (BlockBoolUtil<MinWriteSize>::ReadFlag(key->no_next_key))
      {
        break;
      }

      key = GetNextKey(key);
    }

    return nullptr;
  }

  ErrorCode Add(KeyBase& key) override
  {
    return AddKey(key.name_, key.raw_data_.addr_, key.raw_data_.size_);
  }

  /**
   * @brief 计算对齐后的大小
   *        (Calculate the aligned size).
   * @param size 需要对齐的大小 (Size to align).
   * @return 对齐后的大小 (Aligned size).
   */
  size_t AlignSize(size_t size)
  {
    return static_cast<size_t>((size + MinWriteSize - 1) / MinWriteSize) * MinWriteSize;
  }

  /**
   * @brief 以最小写入单元对齐的方式写入数据
   *        (Write data aligned to the minimum write unit).
   * @param offset 写入偏移量 (Write offset).
   * @param data 要写入的数据 (Data to write).
   * @return 操作结果 (Operation result).
   */
  ErrorCode Write(size_t offset, ConstRawData data)
  {
    if (data.size_ == 0)
    {
      return ErrorCode::OK;
    }

    if (data.size_ % MinWriteSize == 0)
    {
      return flash_.Write(offset, data);
    }
    else
    {
      auto final_block_index = data.size_ - data.size_ % MinWriteSize;
      if (final_block_index != 0)
      {
        flash_.Write(offset, {data.addr_, final_block_index});
      }
      memset(write_buffer_, 0xff, MinWriteSize);
      memcpy(write_buffer_,
             reinterpret_cast<const uint8_t*>(data.addr_) + final_block_index,
             data.size_ % MinWriteSize);
      return flash_.Write(offset + final_block_index, {write_buffer_, MinWriteSize});
    }
  }

 public:
  /**
   * @brief 获取数据库中的键值
   *        (Retrieve the key's value from the database).
   * @param key 需要获取的键 (Key to retrieve).
   * @return 操作结果，如果找到则返回 `ErrorCode::OK`，否则返回 `ErrorCode::NOT_FOUND`
   *         (Operation result, returns `ErrorCode::OK` if found, otherwise
   * `ErrorCode::NOT_FOUND`).
   */
  ErrorCode Get(Database::KeyBase& key) override
  {
    auto ans = SearchKey(key.name_);
    if (!ans)
    {
      return ErrorCode::NOT_FOUND;
    }

    if (key.raw_data_.size_ != ans->dataSize)
    {
      return ErrorCode::FAILED;
    }

    memcpy(key.raw_data_.addr_, GetKeyData(ans), ans->dataSize);

    return ErrorCode::OK;
  }

  /**
   * @brief 设置数据库中的键值
   *        (Set the key's value in the database).
   * @param key 目标键 (Target key).
   * @param data 需要存储的新值 (New value to store).
   * @return 操作结果 (Operation result).
   */
  ErrorCode Set(KeyBase& key, RawData data) override
  {
    memcpy(key.raw_data_.addr_, data.addr_, data.size_);
    return SetKey(key.name_, data.addr_, data.size_);
  }
};

}  // namespace LibXR
