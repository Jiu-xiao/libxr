#pragma once

#include <cstdint>
#include <cstring>

#include "flash.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_type.hpp"

namespace LibXR
{

static constexpr uint16_t LIBXR_DATABASE_VERSION = 3;

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

  /**
   * @brief 获取数据库中的键值
   *        (Retrieve the key's value from the database).
   * @param key 需要获取的键 (Key to retrieve).
   * @return 操作结果，如果找到则返回 `ErrorCode::OK`，否则返回 `ErrorCode::NOT_FOUND`
   *         (Operation result, returns `ErrorCode::OK` if found, otherwise
   * `ErrorCode::NOT_FOUND`).
   */
  ErrorCode Get(Database::KeyBase& key) override;

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

 private:
  enum class BlockType : uint8_t
  {
    MAIN = 0,   ///< 主块 (Main block).
    BACKUP = 1  ///< 备份块 (Backup block).
  };

#pragma pack(push, 1)
  /**
   * @brief 键信息结构，存储键的元数据
   *        (Structure containing key metadata).
   */
  struct KeyInfo
  {
    uint32_t raw_data;

    KeyInfo() : raw_data(0xFFFFFFFF) {}

    KeyInfo(bool nextKey, uint8_t nameLength, uint32_t dataSize) : raw_data(0)
    {
      SetNextKeyExist(nextKey);
      SetNameLength(nameLength);
      SetDataSize(dataSize);
    }

    void SetNextKeyExist(bool value)
    {
      raw_data = (raw_data & 0x7FFFFFFF) | (static_cast<uint32_t>(value & 0x1) << 31);
    }
    bool GetNextKeyExist() const { return (raw_data >> 31) & 0x1; }

    void SetNameLength(uint8_t len)
    {
      raw_data = (raw_data & 0x80FFFFFF) | (static_cast<uint32_t>(len & 0x7F) << 24);
    }
    uint8_t GetNameLength() const { return (raw_data >> 24) & 0x7F; }

    void SetDataSize(uint32_t size)
    {
      raw_data = (raw_data & 0xFF000000) | (size & 0x00FFFFFF);
    }
    uint32_t GetDataSize() const { return raw_data & 0x00FFFFFF; }
  };

  static_assert(sizeof(KeyInfo) == 4, "KeyInfo size must be 4 bytes");
#pragma pack(pop)

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
  ErrorCode SetKey(size_t offset, const void* data, size_t size);
  ErrorCode GetKeyData(size_t offset, RawData data);
  void InitBlock(BlockType block);
  bool IsBlockInited(BlockType block);
  bool IsBlockEmpty(BlockType block);
  bool IsBlockError(BlockType block);
  bool HasLastKey(size_t offset);
  size_t GetKeySize(size_t offset);
  size_t GetNextKey(size_t offset);
  size_t GetLastKey(BlockType block);
  void SetNestKeyExist(size_t offset, bool exist);
  bool KeyDataCompare(size_t offset, const void* data, size_t size);
  bool KeyNameCompare(size_t offset, const char* name);
  size_t SearchKey(const char* name);

  static constexpr uint32_t FLASH_HEADER =
      0x12345678 + LIBXR_DATABASE_VERSION;  ///< Flash 头部标识 (Flash header identifier).
  static constexpr uint8_t CHECKSUM_BYTE = 0x56;  ///< 校验字节 (Checksum byte).

  Flash& flash_;              ///< 目标 Flash 存储设备 (Target Flash storage device).
  uint8_t* buffer_;           ///< 数据缓冲区 (Data buffer).
  uint32_t block_size_;       ///< Flash 块大小 (Flash block size).
  uint32_t max_buffer_size_;  ///< 最大缓冲区大小 (Maximum buffer size).
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
  static constexpr uint32_t FLASH_HEADER =
      0x12345678 + LIBXR_DATABASE_VERSION;  ///< Flash 头部标识 (Flash header identifier).

  static constexpr uint32_t CHECKSUM_BYTE = 0x9abcedf0;  ///< 校验字节 (Checksum byte).

  enum class BlockType : uint8_t
  {
    MAIN = 0,   ///< 主块 (Main block).
    BACKUP = 1  ///< 备份块 (Backup block).
  };

#pragma pack(push, 1)
  template <size_t BlockSize>
  struct BlockBoolData
  {
    uint8_t data[BlockSize];
  };
#pragma pack(pop)

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

#pragma pack(push, 1)
  /**
   * @brief 键信息结构，存储键的元数据
   *        (Structure containing key metadata).
   */
  struct KeyInfo
  {
    BlockBoolData<MinWriteSize> no_next_key;     ///< 是否是最后一个键
    BlockBoolData<MinWriteSize> available_flag;  ///< 该键是否有效
    BlockBoolData<MinWriteSize> uninit;          ///< 该键是否未初始化

    uint32_t raw_info = 0;  ///< 高7位为 nameLength，低25位为 dataSize

    // 默认构造
    KeyInfo()
    {
      BlockBoolUtil<MinWriteSize>::SetFlag(no_next_key, true);
      BlockBoolUtil<MinWriteSize>::SetFlag(available_flag, true);
      BlockBoolUtil<MinWriteSize>::SetFlag(uninit, true);
    }

    void SetNameLength(uint8_t len)
    {
      raw_info = (raw_info & 0x01FFFFFF) | ((len & 0x7F) << 25);
    }

    uint8_t GetNameLength() const { return (raw_info >> 25) & 0x7F; }

    void SetDataSize(uint32_t size)
    {
      raw_info = (raw_info & 0xFE000000) | (size & 0x01FFFFFF);
    }

    uint32_t GetDataSize() const { return raw_info & 0x01FFFFFF; }
  };
#pragma pack(pop)

#pragma pack(push, 1)
  /**
   * @brief Flash 存储的块信息结构
   *        (Structure representing a Flash storage block).
   */
  struct FlashInfo
  {
    FlashInfo()
    {
      header = 0xFFFFFFFF;
      memset(padding, 0xFF, MinWriteSize);
    }

    union
    {
      uint32_t header;  ///< Flash block header
      uint8_t padding[MinWriteSize];
    };
    KeyInfo key;  ///< Align KeyInfo to MinWriteSize
  };
#pragma pack(pop)

  size_t recycle_threshold_ = 0;  ///< 回收阈值 (Recycle threshold).
  Flash& flash_;                  ///< 目标 Flash 存储设备 (Target Flash storage device).
  uint32_t block_size_;           ///< Flash 块大小 (Flash block size).
  uint8_t write_buffer_[MinWriteSize];  ///< 写入缓冲区 (Write buffer).

  /**
   * @brief 计算可用的存储空间大小
   *        (Calculate the available storage size).
   * @return 剩余的可用字节数 (Remaining available bytes).
   */
  size_t AvailableSize()
  {
    auto offset = GetNextKey(GetLastKey(BlockType::MAIN));
    return block_size_ - sizeof(CHECKSUM_BYTE) - offset;
  }

  ErrorCode AddKeyBody(size_t name_len, size_t size, size_t& key_buf_offset)
  {
    bool recycle = false;
  add_again:
    size_t last_key_offset = GetLastKey(BlockType::MAIN);
    key_buf_offset = 0;

    if (last_key_offset == 0)
    {
      FlashInfo flash_info;
      flash_info.header = FLASH_HEADER;
      KeyInfo& tmp_key = flash_info.key;
      BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.no_next_key, false);
      BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.available_flag, false);
      BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.uninit, false);
      tmp_key.SetNameLength(0);
      tmp_key.SetDataSize(0);
      Write(0, flash_info);
      key_buf_offset = GetNextKey(OFFSET_OF(FlashInfo, key));
    }
    else
    {
      key_buf_offset = GetNextKey(last_key_offset);
    }

    if (AvailableSize() <
        AlignSize(sizeof(KeyInfo)) + AlignSize(name_len) + AlignSize(size))
    {
      if (!recycle)
      {
        Recycle();
        recycle = true;
        // NOLINTNEXTLINE
        goto add_again;
      }
      else
      {
        ASSERT(false);
        return ErrorCode::FULL;
      }
    }

    KeyInfo new_key = {};
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.no_next_key, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.available_flag, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.uninit, true);
    new_key.SetNameLength(name_len);
    new_key.SetDataSize(size);

    Write(key_buf_offset, new_key);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.uninit, false);

    if (last_key_offset != 0)
    {
      KeyInfo last_key;
      flash_.Read(last_key_offset, last_key);
      KeyInfo new_last_key = {};
      BlockBoolUtil<MinWriteSize>::SetFlag(new_last_key.no_next_key, false);
      BlockBoolUtil<MinWriteSize>::SetFlag(
          new_last_key.available_flag,
          BlockBoolUtil<MinWriteSize>::ReadFlag(last_key.available_flag));
      BlockBoolUtil<MinWriteSize>::SetFlag(
          new_last_key.uninit, BlockBoolUtil<MinWriteSize>::ReadFlag(last_key.uninit));
      new_last_key.SetNameLength(last_key.GetNameLength());
      new_last_key.SetDataSize(last_key.GetDataSize());

      Write(last_key_offset, new_last_key);
    }

    Write(key_buf_offset, new_key);

    return ErrorCode::OK;
  }

  ErrorCode AddKey(size_t name_offset, size_t name_len, const void* data, size_t size)
  {
    size_t key_buf_offset = 0;
    ErrorCode ec = AddKeyBody(name_len, size, key_buf_offset);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
    CopyFlashData(GetKeyName(key_buf_offset), name_offset, name_len);
    Write(GetKeyData(key_buf_offset), {reinterpret_cast<const uint8_t*>(data), size});
    return ErrorCode::OK;
  }

  ErrorCode AddKey(const char* name, const void* data, size_t size)
  {
    size_t name_len = strlen(name) + 1;
    if (auto ans = SearchKey(name))
    {
      return SetKey(name, data, size);
    }
    size_t key_buf_offset = 0;
    ErrorCode ec = AddKeyBody(name_len, size, key_buf_offset);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
    Write(GetKeyName(key_buf_offset), {reinterpret_cast<const uint8_t*>(name), name_len});
    Write(GetKeyData(key_buf_offset), {reinterpret_cast<const uint8_t*>(data), size});
    return ErrorCode::OK;
  }

  ErrorCode SetKeyCommon(size_t key_offset, size_t name_len, const void* data,
                         size_t size)
  {
    if (key_offset)
    {
      KeyInfo key;
      flash_.Read(key_offset, key);
      if (key.GetDataSize() == size)
      {
        if (KeyDataCompare(key_offset, data, size))
        {
          KeyInfo new_key = {};
          BlockBoolUtil<MinWriteSize>::SetFlag(
              new_key.no_next_key,
              BlockBoolUtil<MinWriteSize>::ReadFlag(key.no_next_key));
          BlockBoolUtil<MinWriteSize>::SetFlag(new_key.available_flag, false);
          BlockBoolUtil<MinWriteSize>::SetFlag(
              new_key.uninit, BlockBoolUtil<MinWriteSize>::ReadFlag(key.uninit));
          new_key.SetNameLength(name_len);
          new_key.SetDataSize(size);
          Write(key_offset, new_key);
          return AddKey(GetKeyName(key_offset), name_len, data, size);
        }
        return ErrorCode::OK;
      }
    }
    return ErrorCode::FAILED;
  }

  ErrorCode SetKey(size_t name_offset, size_t name_length, const void* data, size_t size)
  {
    size_t key_offset = SearchKey(name_offset, name_length);
    return SetKeyCommon(key_offset, name_length, data, size);
  }

  ErrorCode SetKey(const char* name, const void* data, size_t size, bool recycle = true)
  {
    size_t key_offset = SearchKey(name);
    if (key_offset)
    {
      KeyInfo key;
      flash_.Read(key_offset, key);
      if (key.GetDataSize() == size)
      {
        if (KeyDataCompare(key_offset, data, size))
        {
          if (AvailableSize() < AlignSize(size) + AlignSize(sizeof(KeyInfo)) +
                                    AlignSize(key.GetNameLength()))
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
              BlockBoolUtil<MinWriteSize>::ReadFlag(key.no_next_key));
          BlockBoolUtil<MinWriteSize>::SetFlag(new_key.available_flag, false);
          BlockBoolUtil<MinWriteSize>::SetFlag(
              new_key.uninit, BlockBoolUtil<MinWriteSize>::ReadFlag(key.uninit));
          new_key.SetNameLength(key.GetNameLength());
          new_key.SetDataSize(size);
          Write(key_offset, new_key);
          return AddKey(GetKeyName(key_offset), key.GetNameLength(), data, size);
        }
        return ErrorCode::OK;
      }
    }
    return ErrorCode::FAILED;
  }

  size_t GetKeyData(size_t offset)
  {
    KeyInfo key;
    flash_.Read(offset, key);
    return offset + AlignSize(sizeof(KeyInfo)) + AlignSize(key.GetNameLength());
  }

  size_t GetKeyName(size_t offset) { return offset + AlignSize(sizeof(KeyInfo)); }

  void InitBlock(BlockType block)
  {
    size_t offset = 0;
    if (block == BlockType::BACKUP)
    {
      offset = block_size_;
    }
    flash_.Erase(offset, block_size_);

    FlashInfo info;  // padding filled with 0xFF by constructor
    info.header = FLASH_HEADER;
    KeyInfo& tmp_key = info.key;
    BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.no_next_key, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.available_flag, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.uninit, false);
    tmp_key.SetNameLength(0);
    tmp_key.SetDataSize(0);
    Write(offset, {reinterpret_cast<uint8_t*>(&info), sizeof(FlashInfo)});
    Write(offset + block_size_ - AlignSize(sizeof(CHECKSUM_BYTE)),
          {&CHECKSUM_BYTE, sizeof(CHECKSUM_BYTE)});
  }

  bool IsBlockInited(BlockType block)
  {
    size_t offset = 0;
    if (block == BlockType::BACKUP)
    {
      offset = block_size_;
    }
    FlashInfo flash_data;
    flash_.Read(offset, flash_data);
    return flash_data.header == FLASH_HEADER;
  }

  bool IsBlockEmpty(BlockType block)
  {
    size_t offset = 0;
    if (block == BlockType::BACKUP)
    {
      offset = block_size_;
    }
    FlashInfo flash_data;
    flash_.Read(offset, flash_data);
    return BlockBoolUtil<MinWriteSize>::ReadFlag(flash_data.key.available_flag) == true;
  }

  bool IsBlockError(BlockType block)
  {
    size_t offset = 0;
    if (block == BlockType::BACKUP)
    {
      offset = block_size_;
    }
    uint32_t checksum = 0;
    flash_.Read(offset + block_size_ - AlignSize(sizeof(CHECKSUM_BYTE)), checksum);
    return checksum != CHECKSUM_BYTE;
  }

  size_t GetKeySize(size_t offset)
  {
    KeyInfo key;
    flash_.Read(offset, key);
    return AlignSize(sizeof(KeyInfo)) + AlignSize(key.GetNameLength()) +
           AlignSize(key.GetDataSize());
  }

  size_t GetNextKey(size_t offset)
  {
    KeyInfo key;
    flash_.Read(offset, key);
    return offset + GetKeySize(offset);
  }

  size_t GetLastKey(BlockType block)
  {
    if (IsBlockEmpty(block))
    {
      return 0;
    }

    KeyInfo key;
    size_t key_offset = OFFSET_OF(FlashInfo, key);
    if (block != BlockType::MAIN)
    {
      key_offset += block_size_;
    }
    flash_.Read(key_offset, key);
    while (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.no_next_key))
    {
      key_offset = GetNextKey(key_offset);
      flash_.Read(key_offset, key);
    }
    return key_offset;
  }

  bool KeyDataCompare(size_t offset, const void* data, size_t size)
  {
    KeyInfo key;
    flash_.Read(offset, key);
    size_t key_data_offset = GetKeyData(offset);
    uint8_t data_buffer = 0;
    for (size_t i = 0; i < size; i++)
    {
      flash_.Read(key_data_offset + i, data_buffer);
      if (data_buffer != (reinterpret_cast<const uint8_t*>(data))[i])
      {
        return true;
      }
    }
    return false;
  }

  bool KeyNameCompare(size_t offset, const char* name)
  {
    KeyInfo key;
    flash_.Read(offset, key);
    for (size_t i = 0; i < key.GetNameLength(); i++)
    {
      uint8_t data_buffer = 0;
      flash_.Read(offset + AlignSize(sizeof(KeyInfo)) + i, data_buffer);
      if (data_buffer != name[i])
      {
        return true;
      }
    }
    return false;
  }

  bool KeyNameCompare(size_t offset_a, size_t offset_b)
  {
    KeyInfo key_a, key_b;
    flash_.Read(offset_a, key_a);
    flash_.Read(offset_b, key_b);
    if (key_a.GetNameLength() != key_b.GetNameLength())
    {
      return true;
    }
    for (size_t i = 0; i < key_a.GetNameLength(); i++)
    {
      uint8_t data_buffer_a = 0, data_buffer_b = 0;
      flash_.Read(offset_a + sizeof(KeyInfo) + i, data_buffer_a);
      flash_.Read(offset_b + sizeof(KeyInfo) + i, data_buffer_b);
      if (data_buffer_a != data_buffer_b)
      {
        return true;
      }
    }
    return false;
  }

  void CopyFlashData(size_t dst_offset, size_t src_offset, size_t size)
  {
    for (size_t i = 0; i < size; i += MinWriteSize)
    {
      flash_.Read(src_offset + i, {write_buffer_, MinWriteSize});
      flash_.Write(dst_offset + i, {write_buffer_, MinWriteSize});
    }
  }

  size_t SearchKey(const char* name)
  {
    if (IsBlockEmpty(BlockType::MAIN))
    {
      return 0;
    }

    KeyInfo key;
    size_t key_offset = OFFSET_OF(FlashInfo, key);
    flash_.Read(key_offset, key);

    size_t ans = 0, need_cycle = 0;

    while (true)
    {
      if (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.available_flag))
      {
        key_offset = GetNextKey(key_offset);
        flash_.Read(key_offset, key);
        need_cycle++;
        continue;
      }

      if (!KeyNameCompare(key_offset, name))
      {
        ans = key_offset;
        break;
      }

      if (BlockBoolUtil<MinWriteSize>::ReadFlag(key.no_next_key))
      {
        break;
      }

      key_offset = GetNextKey(key_offset);
      flash_.Read(key_offset, key);
    }

    if (need_cycle > recycle_threshold_)
    {
      Recycle();
      return SearchKey(name);
    }

    return ans;
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

    KeyInfo key_buffer;
    flash_.Read(ans, key_buffer);

    if (key.raw_data_.size_ < key_buffer.GetDataSize())
    {
      return ErrorCode::FAILED;
    }

    flash_.Read(GetKeyData(ans), key.raw_data_);

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

  ErrorCode Add(KeyBase& key) override
  {
    return AddKey(key.name_, key.raw_data_.addr_, key.raw_data_.size_);
  }

  /**
   * @brief 构造函数，初始化 Flash 存储和缓冲区
   *        (Constructor to initialize Flash storage and buffer).
   *
   * @param flash 目标 Flash 存储设备 (Target Flash storage device).
   * @param recycle_threshold 回收阈值 (Recycle threshold).
   */
  explicit DatabaseRaw(Flash& flash, size_t recycle_threshold = 128)
      : recycle_threshold_(recycle_threshold), flash_(flash)
  {
    ASSERT(flash.MinEraseSize() * 2 <= flash_.Size());
    ASSERT(flash_.MinWriteSize() <= MinWriteSize);
    auto block_num = static_cast<size_t>(flash_.Size() / flash.MinEraseSize());
    block_size_ = block_num / 2 * flash.MinEraseSize();
    Init();
  }

  /**
   * @brief 初始化数据库存储区，确保主备块正确
   *        (Initialize database storage, ensuring main and backup blocks are valid).
   */
  void Init()
  {
    if (!IsBlockInited(BlockType::BACKUP) || IsBlockError(BlockType::BACKUP))
    {
      InitBlock(BlockType::BACKUP);
    }

    if (!IsBlockInited(BlockType::MAIN) || IsBlockError(BlockType::MAIN))
    {
      if (IsBlockEmpty(BlockType::BACKUP))
      {
        InitBlock(BlockType::MAIN);
      }
      else
      {
        flash_.Erase(0, block_size_);
        for (uint32_t i = 0; i < block_size_; i += MinWriteSize)
        {
          flash_.Read(i + block_size_, {write_buffer_, MinWriteSize});
          flash_.Write(i, {write_buffer_, MinWriteSize});
        }
      }
    }

    if (!IsBlockEmpty(BlockType::BACKUP))
    {
      InitBlock(BlockType::BACKUP);
    }

    KeyInfo key;
    size_t key_offset = OFFSET_OF(FlashInfo, key);
    flash_.Read(key_offset, key);
    size_t need_cycle = 0;
    while (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.no_next_key))
    {
      key_offset = GetNextKey(key_offset);
      flash_.Read(key_offset, key);
      // TODO: 恢复损坏数据
      if (BlockBoolUtil<MinWriteSize>::ReadFlag(key.uninit))
      {
        InitBlock(BlockType::MAIN);
        break;
      }
      if (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.available_flag))
      {
        need_cycle += 1;
      }
    }

    if (need_cycle > recycle_threshold_)
    {
      Recycle();
    }
  }

  /**
   * @brief 还原存储数据，清空 Flash 区域
   *        (Restore storage data, clearing Flash memory area).
   */
  void Restore()
  {
    InitBlock(BlockType::MAIN);
    InitBlock(BlockType::BACKUP);
  }

  /**
   * @brief 回收 Flash 空间，整理数据
   *        (Recycle Flash storage space and organize data).
   *
   * Moves valid keys from the main block to the backup block and erases the main
   block.
   * 将主存储块中的有效键移动到备份块，并擦除主存储块。
   *
   * @return 操作结果 (Operation result).
   */
  ErrorCode Recycle()
  {
    if (IsBlockEmpty(BlockType::MAIN))
    {
      return ErrorCode::OK;
    }

    KeyInfo key;
    size_t key_offset = OFFSET_OF(FlashInfo, key);
    flash_.Read(key_offset, key);

    if (!IsBlockEmpty(BlockType::BACKUP))
    {
      ASSERT(false);
      return ErrorCode::FAILED;
    }

    size_t write_buff_offset = OFFSET_OF(FlashInfo, key) + block_size_;

    auto new_key = KeyInfo{};
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.uninit, false);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.available_flag, false);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.no_next_key, false);
    Write(write_buff_offset, new_key);

    write_buff_offset += GetKeySize(write_buff_offset);

    do
    {
      key_offset = GetNextKey(key_offset);
      flash_.Read(key_offset, key);

      if (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.available_flag))
      {
        continue;
      }

      Write(write_buff_offset, key);
      write_buff_offset += AlignSize(sizeof(KeyInfo));
      CopyFlashData(write_buff_offset, GetKeyName(key_offset), key.GetNameLength());
      write_buff_offset += AlignSize(key.GetNameLength());
      CopyFlashData(write_buff_offset, GetKeyData(key_offset), key.GetDataSize());
      write_buff_offset += AlignSize(key.GetDataSize());
    } while (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.no_next_key));

    flash_.Erase(0, block_size_);
    for (uint32_t i = 0; i < block_size_; i += MinWriteSize)
    {
      flash_.Read(i + block_size_, {write_buffer_, MinWriteSize});
      flash_.Write(i, {write_buffer_, MinWriteSize});
    }

    InitBlock(BlockType::BACKUP);

    return ErrorCode::OK;
  }
};

}  // namespace LibXR
