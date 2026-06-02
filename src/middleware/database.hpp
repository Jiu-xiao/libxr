#pragma once

#include <cstdint>
#include <cstring>

#include "flash.hpp"
#include "libxr.hpp"
#include "libxr_def.hpp"
#include "libxr_mem.hpp"
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
      ErrorCode status = database.Get(*this);
      if (status != ErrorCode::OK)
      {
        data_ = init_value;
        if (status == ErrorCode::NOT_FOUND)
        {
          REQUIRE(database.Add(*this) == ErrorCode::OK);
        }
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
      ErrorCode status = database.Get(*this);
      if (status != ErrorCode::OK)
      {
        Memory::FastSet(&data_, 0, sizeof(Data));
        if (status == ErrorCode::NOT_FOUND)
        {
          REQUIRE(database.Add(*this) == ErrorCode::OK);
        }
      }
    }

    /**
     * @brief 禁止拷贝数据库键对象 (Copy construction is disabled for database keys).
     * @param other 被拷贝的键对象 (Database key to copy from).
     */
    Key(const Key&) = delete;

    /**
     * @brief 禁止移动数据库键对象 (Move construction is disabled for database keys).
     * @param other 被转移的键对象 (Database key to move from).
     */
    Key(Key&&) = delete;

    /**
     * @brief 禁止拷贝赋值数据库键对象
     *        (Copy assignment is disabled for database keys).
     * @param other 被拷贝的键对象 (Database key to copy from).
     * @return 当前键对象引用 (Reference to the current key object).
     */
    Key& operator=(const Key&) = delete;

    /**
     * @brief 禁止移动赋值数据库键对象
     *        (Move assignment is disabled for database keys).
     * @param other 被转移的键对象 (Database key to move from).
     * @return 当前键对象引用 (Reference to the current key object).
     */
    Key& operator=(Key&&) = delete;

    /**
     * @brief 类型转换运算符，返回存储的数据 (Type conversion operator returning stored
     * data).
     * @return 存储的数据 (Stored data).
     */
    operator Data() { return data_; }

    /**
     * @brief 保存当前键值到数据库 (Save the current key value to the database).
     * @return 操作结果 (Operation result).
     */
    ErrorCode Save() { return database_.Set(*this, this->raw_data_); }

    /**
     * @brief 设置键的值并更新数据库 (Set the key's value and update the database).
     * @param data 需要存储的新值 (New value to store).
     * @return 操作结果 (Operation result).
     */
    ErrorCode Set(Data data)
    {
      data_ = data;
      return Save();
    }

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
 *
 * @note 若底层 Flash 读写擦失败，当前实现视为不可恢复故障并直接触发 `REQUIRE`。
 *       If the underlying Flash read, write, or erase operation fails, the
 *       current implementation treats it as an unrecoverable fault and triggers
 *       `REQUIRE` immediately.
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
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   * @note `max_buffer_size` 必须不超过整片 Flash 容量的一半。
   *       `max_buffer_size` must not exceed half of the total flash capacity.
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
  /**
   * @brief 存储块类型 (Storage block type).
   */
  /**
   * @brief 存储块类型 (Storage block type).
   */
  /**
   * @brief 存储块类型 (Storage block type).
   */
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

    /**
     * @brief 构造一个擦除态键头 (Construct one erased-state key header).
     */
    KeyInfo() : raw_data(0xFFFFFFFF) {}

    /**
     * @brief 构造一个指定元数据的键头 (Construct one key header with explicit metadata).
     * @param nextKey 是否还有后继键 (Whether another key follows this one).
     * @param nameLength 键名长度 (Key name length).
     * @param dataSize 数据字节数 (Payload size in bytes).
     */
    KeyInfo(bool nextKey, uint8_t nameLength, uint32_t dataSize) : raw_data(0)
    {
      SetNextKeyExist(nextKey);
      SetNameLength(nameLength);
      SetDataSize(dataSize);
    }

    /**
     * @brief 设置是否存在后继键 (Set whether another key follows).
     * @param value 是否存在后继键 (Whether another key follows).
     */
    void SetNextKeyExist(bool value)
    {
      raw_data = (raw_data & 0x7FFFFFFF) | (static_cast<uint32_t>(value & 0x1) << 31);
    }

    /**
     * @brief 获取是否存在后继键 (Get whether another key follows).
     * @return 若存在后继键则返回 `true` (Returns `true` when another key follows).
     */
    bool GetNextKeyExist() const { return (raw_data >> 31) & 0x1; }

    /**
     * @brief 设置键名长度 (Set the key name length).
     * @param len 键名长度 (Key name length).
     */
    void SetNameLength(uint8_t len)
    {
      raw_data = (raw_data & 0x80FFFFFF) | (static_cast<uint32_t>(len & 0x7F) << 24);
    }

    /**
     * @brief 获取键名长度 (Get the key name length).
     * @return 键名长度 (Key name length).
     */
    uint8_t GetNameLength() const { return (raw_data >> 24) & 0x7F; }

    /**
     * @brief 设置数据字节数 (Set the payload size in bytes).
     * @param size 数据字节数 (Payload size in bytes).
     */
    void SetDataSize(uint32_t size)
    {
      raw_data = (raw_data & 0xFF000000) | (size & 0x00FFFFFF);
    }

    /**
     * @brief 获取数据字节数 (Get the payload size in bytes).
     * @return 数据字节数 (Payload size in bytes).
     */
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

  /**
   * @brief 按名称新增一个键 (Add one key by name).
   * @param name 键名 (Key name).
   * @param data 键数据地址 (Address of the key payload).
   * @param size 键数据字节数 (Payload size in bytes).
   * @return 操作结果 (Operation result).
   */
  ErrorCode AddKey(const char* name, const void* data, size_t size);

  /**
   * @brief 按名称更新一个键 (Update one key by name).
   * @param name 键名 (Key name).
   * @param data 键数据地址 (Address of the key payload).
   * @param size 键数据字节数 (Payload size in bytes).
   * @return 操作结果 (Operation result).
   */
  ErrorCode SetKey(const char* name, const void* data, size_t size);

  /**
   * @brief 按存储偏移更新一个键 (Update one key by storage offset).
   * @param offset 键头偏移 (Key-header offset).
   * @param data 键数据地址 (Address of the key payload).
   * @param size 键数据字节数 (Payload size in bytes).
   * @return 操作结果 (Operation result).
   */
  ErrorCode SetKey(size_t offset, const void* data, size_t size);

  /**
   * @brief 读取一个键的数据区 (Read one key payload).
   * @param offset 键头偏移 (Key-header offset).
   * @param data 接收数据的缓冲区 (Destination buffer receiving the payload).
   * @return 操作结果 (Operation result).
   */
  ErrorCode GetKeyData(size_t offset, RawData data);

  /**
   * @brief 把指定块初始化为空数据库块 (Initialize one block as an empty database block).
   * @param block 目标块类型 (Target block type).
   */
  void InitBlock(BlockType block);

  /**
   * @brief 读取 Flash 数据，失败则直接触发强约束
   *        (Read flash data and fail fast on error).
   * @param offset 读取偏移 (Read offset).
   * @param data 接收数据的缓冲区 (Destination buffer receiving the data).
   */
  void ReadFlashOrExit(size_t offset, RawData data);

  /**
   * @brief 读取 Flash 数据到对象里，失败则直接触发强约束
   *        (Read flash data into one object and fail fast on error).
   * @tparam Data 接收对象类型 (Destination object type).
   * @param offset 读取偏移 (Read offset).
   * @param data 接收数据的对象 (Destination object receiving the data).
   */
  template <typename Data>
  void ReadFlashOrExit(size_t offset, Data& data)
  {
    ReadFlashOrExit(offset, RawData(data));
  }

  /**
   * @brief 写入 Flash 数据，失败则直接触发强约束
   *        (Write flash data and fail fast on error).
   * @param offset 写入偏移 (Write offset).
   * @param data 待写入数据 (Data to write).
   */
  void WriteFlashOrExit(size_t offset, ConstRawData data);

  /**
   * @brief 擦除 Flash 区域，失败则直接触发强约束
   *        (Erase a flash range and fail fast on error).
   * @param offset 擦除偏移 (Erase offset).
   * @param size 擦除字节数 (Erase size in bytes).
   */
  void EraseFlashOrExit(size_t offset, size_t size);

  /**
   * @brief 判断块头是否已初始化 (Check whether the block header is initialized).
   * @param block 目标块类型 (Target block type).
   * @return 若块头有效则返回 `true` (Returns `true` when the block header is valid).
   */
  bool IsBlockInited(BlockType block);

  /**
   * @brief 判断块当前是否为空 (Check whether the block is currently empty).
   * @param block 目标块类型 (Target block type).
   * @return 若块内没有有效键则返回 `true`
   *         (Returns `true` when the block contains no valid key).
   */
  bool IsBlockEmpty(BlockType block);

  /**
   * @brief 判断块尾校验是否损坏 (Check whether the block checksum is corrupted).
   * @param block 目标块类型 (Target block type).
   * @return 若块尾校验不符则返回 `true`
   *         (Returns `true` when the trailing checksum is invalid).
   */
  bool IsBlockError(BlockType block);

  /**
   * @brief 判断指定键后面是否还有下一键 (Check whether another key follows).
   * @param offset 当前键头偏移 (Current key-header offset).
   * @return 若后面还有下一键则返回 `true`
   *         (Returns `true` when another key follows this one).
   */
  bool HasLastKey(size_t offset);

  /**
   * @brief 计算一个键总共占用的字节数 (Compute the total byte span of one key).
   * @param offset 键头偏移 (Key-header offset).
   * @return 该键占用的总字节数 (Total byte size occupied by the key).
   */
  size_t GetKeySize(size_t offset);

  /**
   * @brief 计算下一键的起始偏移 (Compute the starting offset of the next key).
   * @param offset 当前键头偏移 (Current key-header offset).
   * @return 下一键的起始偏移 (Starting offset of the next key).
   */
  size_t GetNextKey(size_t offset);

  /**
   * @brief 计算当前块里最后一个键的偏移 (Locate the last key in the current block).
   * @param block 目标块类型 (Target block type).
   * @return 最后一个键的偏移；若块为空则返回 `0`
   *         (Offset of the last key, or `0` when the block is empty).
   */
  size_t GetLastKey(BlockType block);

  /**
   * @brief 回写上一键的“存在后继键”标志 (Rewrite the next-key-exists flag of one key).
   * @param offset 目标键头偏移 (Target key-header offset).
   * @param exist 是否存在后继键 (Whether another key follows).
   */
  void SetNestKeyExist(size_t offset, bool exist);

  /**
   * @brief 比较存储中的键数据和给定数据是否不同
   *        (Compare whether the stored payload differs from the given payload).
   * @param offset 键头偏移 (Key-header offset).
   * @param data 待比较数据地址 (Address of the candidate payload).
   * @param size 待比较数据字节数 (Payload size in bytes).
   * @return 若内容不同则返回 `true`
   *         (Returns `true` when the payloads differ).
   */
  bool KeyDataCompare(size_t offset, const void* data, size_t size);

  /**
   * @brief 比较存储中的键名和给定名称是否不同
   *        (Compare whether the stored key name differs from the given name).
   * @param offset 键头偏移 (Key-header offset).
   * @param name 待比较键名 (Key name to compare against).
   * @return 若名称不同则返回 `true`
   *         (Returns `true` when the names differ).
   */
  bool KeyNameCompare(size_t offset, const char* name);

  /**
   * @brief 在主块里按名称查找键 (Search one key by name in the main block).
   * @param name 待查找键名 (Key name to search for).
   * @return 找到时返回键头偏移，找不到返回 `0`
   *         (Returns the key-header offset when found, otherwise `0`).
   */
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
 * @note 若底层 Flash 读写擦失败，当前实现视为不可恢复故障并直接触发 `REQUIRE`。
 *       If the underlying Flash read, write, or erase operation fails, the
 *       current implementation treats it as an unrecoverable fault and triggers
 *       `REQUIRE` immediately.
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
  /**
   * @brief 按最小写入单元存放布尔位图块
   *        (Boolean flag block stored in one aligned write unit span).
   * @tparam BlockSize 位图块字节数 (Flag-block size in bytes).
   */
  template <size_t BlockSize>
  struct BlockBoolData
  {
    uint8_t data[BlockSize];
  };
#pragma pack(pop)

  /**
   * @brief 读写对齐布尔位图块的工具
   *        (Helpers for reading and writing aligned boolean flag blocks).
   * @tparam BlockSize 位图块字节数 (Flag-block size in bytes).
   */
  template <size_t BlockSize>
  class BlockBoolUtil
  {
   public:
    /**
     * @brief 把一个布尔值编码进位图块 (Encode one boolean value into a flag block).
     * @param obj 目标位图块 (Target flag block).
     * @param value 待编码布尔值 (Boolean value to encode).
     */
    static void SetFlag(BlockBoolData<BlockSize>& obj, bool value)
    {
      Memory::FastSet(obj.data, 0xFF, BlockSize);
      if (!value)
      {
        obj.data[BlockSize - 1] &= 0xF0;
      }
    }

    /**
     * @brief 从位图块读取布尔值 (Decode one boolean value from a flag block).
     * @param obj 待读取位图块 (Flag block to inspect).
     * @return 解码出的布尔值 (Decoded boolean value).
     */
    static bool ReadFlag(const BlockBoolData<BlockSize>& obj)
    {
      uint8_t last_4bits = obj.data[BlockSize - 1] & 0x0F;
      return last_4bits == 0x0F;
    }

    /**
     * @brief 检查位图块内容是否仍是合法编码 (Check whether a flag block still contains
     *        a valid encoding).
     * @param obj 待检查位图块 (Flag block to validate).
     * @return 若编码合法则返回 `true` (Returns `true` when the encoding is valid).
     */
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

    /**
     * @brief 构造一个默认可写的键头元数据
     *        (Construct one default writable key-header metadata object).
     */
    KeyInfo()
    {
      BlockBoolUtil<MinWriteSize>::SetFlag(no_next_key, true);
      BlockBoolUtil<MinWriteSize>::SetFlag(available_flag, true);
      BlockBoolUtil<MinWriteSize>::SetFlag(uninit, true);
    }

    /**
     * @brief 设置键名长度 (Set the key name length).
     * @param len 键名长度 (Key name length).
     */
    void SetNameLength(uint8_t len)
    {
      raw_info = (raw_info & 0x01FFFFFF) | ((len & 0x7F) << 25);
    }

    /**
     * @brief 获取键名长度 (Get the key name length).
     * @return 键名长度 (Key name length).
     */
    uint8_t GetNameLength() const { return (raw_info >> 25) & 0x7F; }

    /**
     * @brief 设置数据字节数 (Set the payload size in bytes).
     * @param size 数据字节数 (Payload size in bytes).
     */
    void SetDataSize(uint32_t size)
    {
      raw_info = (raw_info & 0xFE000000) | (size & 0x01FFFFFF);
    }

    /**
     * @brief 获取数据字节数 (Get the payload size in bytes).
     * @return 数据字节数 (Payload size in bytes).
     */
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
    /**
     * @brief 构造一个擦除态 FlashInfo 缓冲对象
     *        (Construct one erased-state FlashInfo buffer object).
     */
    FlashInfo()
    {
      header = 0xFFFFFFFF;
      Memory::FastSet(padding, 0xFF, MinWriteSize);
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
   * @brief 读取 Flash 数据，失败则直接触发强约束
   *        (Read flash data and fail fast on error).
   * @param offset 读取偏移 (Read offset).
   * @param data 接收数据的缓冲区 (Destination buffer receiving the data).
   */
  void ReadFlashOrExit(size_t offset, RawData data)
  {
    REQUIRE(flash_.Read(offset, data) == ErrorCode::OK);
  }

  /**
   * @brief 读取 Flash 数据到对象里，失败则直接触发强约束
   *        (Read flash data into one object and fail fast on error).
   * @tparam Data 接收对象类型 (Destination object type).
   * @param offset 读取偏移 (Read offset).
   * @param data 接收数据的对象 (Destination object receiving the data).
   */
  template <typename Data>
  void ReadFlashOrExit(size_t offset, Data& data)
  {
    ReadFlashOrExit(offset, RawData(data));
  }

  /**
   * @brief 写入 Flash 数据，失败则直接触发强约束
   *        (Write flash data and fail fast on error).
   * @param offset 写入偏移 (Write offset).
   * @param data 待写入数据 (Data to write).
   */
  void WriteFlashOrExit(size_t offset, ConstRawData data)
  {
    REQUIRE(Write(offset, data) == ErrorCode::OK);
  }

  /**
   * @brief 写入一个对象到 Flash，失败则直接触发强约束
   *        (Write one object to flash and fail fast on error).
   * @tparam Data 待写入对象类型 (Object type to write).
   * @param offset 写入偏移 (Write offset).
   * @param data 待写入对象 (Object to write).
   */
  template <typename Data>
  void WriteFlashOrExit(size_t offset, const Data& data)
  {
    WriteFlashOrExit(offset, ConstRawData(data));
  }

  /**
   * @brief 擦除 Flash 区域，失败则直接触发强约束
   *        (Erase a flash range and fail fast on error).
   * @param offset 擦除偏移 (Erase offset).
   * @param size 擦除字节数 (Erase size in bytes).
   */
  void EraseFlashOrExit(size_t offset, size_t size)
  {
    REQUIRE(flash_.Erase(offset, size) == ErrorCode::OK);
  }

  /**
   * @brief 计算可用的存储空间大小
   *        (Calculate the available storage size).
   * @return 剩余的可用字节数 (Remaining available bytes).
   */
  size_t AvailableSize()
  {
    return GetChecksumOffset() - GetUsedBlockSize(BlockType::MAIN);
  }

  /**
   * @brief 为新增键预留空间并写入元数据头
   *        (Reserve space for one new key and write its metadata header).
   * @param name_len 键名长度 (Key name length).
   * @param size 数据字节数 (Payload size in bytes).
   * @param key_buf_offset 返回新键头偏移 (Receives the new key-header offset).
   * @return 操作结果 (Operation result).
   */
  ErrorCode AddKeyBody(size_t name_len, size_t size, size_t& key_buf_offset)
  {
    bool recycle = false;
  add_again:
    size_t last_key_offset = GetLastKey(BlockType::MAIN);
    key_buf_offset = 0;

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
      WriteFlashOrExit(0, flash_info);
      key_buf_offset = GetNextKey(LibXR::OffsetOf(&FlashInfo::key));
    }
    else
    {
      key_buf_offset = GetNextKey(last_key_offset);
    }

    KeyInfo new_key = {};
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.no_next_key, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.available_flag, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.uninit, true);
    new_key.SetNameLength(name_len);
    new_key.SetDataSize(size);

    WriteFlashOrExit(key_buf_offset, new_key);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.uninit, false);

    if (last_key_offset != 0)
    {
      KeyInfo last_key;
      ReadFlashOrExit(last_key_offset, last_key);
      KeyInfo new_last_key = {};
      BlockBoolUtil<MinWriteSize>::SetFlag(new_last_key.no_next_key, false);
      BlockBoolUtil<MinWriteSize>::SetFlag(
          new_last_key.available_flag,
          BlockBoolUtil<MinWriteSize>::ReadFlag(last_key.available_flag));
      BlockBoolUtil<MinWriteSize>::SetFlag(
          new_last_key.uninit, BlockBoolUtil<MinWriteSize>::ReadFlag(last_key.uninit));
      new_last_key.SetNameLength(last_key.GetNameLength());
      new_last_key.SetDataSize(last_key.GetDataSize());

      WriteFlashOrExit(last_key_offset, new_last_key);
    }

    WriteFlashOrExit(key_buf_offset, new_key);

    return ErrorCode::OK;
  }

  /**
   * @brief 使用现有名字数据新增一个键
   *        (Add one key using an existing name already stored in flash).
   * @param name_offset 已存键名在 Flash 中的偏移 (Flash offset of the already stored name).
   * @param name_len 键名长度 (Key name length).
   * @param data 键数据地址 (Address of the key payload).
   * @param size 键数据字节数 (Payload size in bytes).
   * @return 操作结果 (Operation result).
   */
  /**
   * @brief 使用现有名字数据新增一个键
   *        (Add one key using an existing name already stored in flash).
   * @param name_offset 已存键名在 Flash 中的偏移 (Flash offset of the already stored name).
   * @param name_len 键名长度 (Key name length).
   * @param data 键数据地址 (Address of the key payload).
   * @param size 键数据字节数 (Payload size in bytes).
   * @return 操作结果 (Operation result).
   */
  ErrorCode AddKey(size_t name_offset, size_t name_len, const void* data, size_t size)
  {
    size_t key_buf_offset = 0;
    ErrorCode ec = AddKeyBody(name_len, size, key_buf_offset);
    if (ec != ErrorCode::OK)
    {
      return ec;
    }
    CopyFlashData(GetKeyName(key_buf_offset), name_offset, name_len);
    WriteFlashOrExit(GetKeyData(key_buf_offset),
                     {reinterpret_cast<const uint8_t*>(data), size});
    return ErrorCode::OK;
  }

  /**
   * @brief 按名称新增一个键 (Add one key by name).
   * @param name 键名 (Key name).
   * @param data 键数据地址 (Address of the key payload).
   * @param size 键数据字节数 (Payload size in bytes).
   * @return 操作结果 (Operation result).
   */
  /**
   * @brief 按名称新增一个键 (Add one key by name).
   * @param name 键名 (Key name).
   * @param data 键数据地址 (Address of the key payload).
   * @param size 键数据字节数 (Payload size in bytes).
   * @return 操作结果 (Operation result).
   */
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
    WriteFlashOrExit(GetKeyName(key_buf_offset),
                     {reinterpret_cast<const uint8_t*>(name), name_len});
    WriteFlashOrExit(GetKeyData(key_buf_offset),
                     {reinterpret_cast<const uint8_t*>(data), size});
    return ErrorCode::OK;
  }

  /**
   * @brief 复用现有键名元数据并尝试写入新值
   *        (Reuse the existing key-name metadata and try to write a new value).
   * @param key_offset 目标键头偏移 (Target key-header offset).
   * @param name_len 键名长度 (Key name length).
   * @param data 新数据地址 (Address of the new payload).
   * @param size 新数据字节数 (Payload size in bytes).
   * @return 操作结果 (Operation result).
   */
  ErrorCode SetKeyCommon(size_t key_offset, size_t name_len, const void* data,
                         size_t size)
  {
    if (key_offset)
    {
      KeyInfo key;
      ReadFlashOrExit(key_offset, key);
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
          WriteFlashOrExit(key_offset, new_key);
          return AddKey(GetKeyName(key_offset), name_len, data, size);
        }
        return ErrorCode::OK;
      }
    }
    return ErrorCode::FAILED;
  }

  /**
   * @brief 按名称更新一个键，并在需要时触发回收
   *        (Update one key by name and recycle storage when needed).
   * @param name 键名 (Key name).
   * @param data 新数据地址 (Address of the new payload).
   * @param size 新数据字节数 (Payload size in bytes).
   * @param recycle 是否允许本次调用触发回收
   *                (Whether this call may trigger recycle).
   * @return 操作结果 (Operation result).
   */
  ErrorCode SetKey(const char* name, const void* data, size_t size, bool recycle = true)
  {
    size_t key_offset = SearchKey(name);
    if (key_offset)
    {
      KeyInfo key;
      ReadFlashOrExit(key_offset, key);
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
          WriteFlashOrExit(key_offset, new_key);
          return AddKey(GetKeyName(key_offset), key.GetNameLength(), data, size);
        }
        return ErrorCode::OK;
      }
    }
    return ErrorCode::FAILED;
  }

  /**
   * @brief 计算某个键的数据区起始偏移
   *        (Compute the starting offset of one key payload).
   * @param offset 键头偏移 (Key-header offset).
   * @return 数据区起始偏移 (Starting offset of the payload).
   */
  size_t GetKeyData(size_t offset)
  {
    KeyInfo key;
    ReadFlashOrExit(offset, key);
    return offset + AlignSize(sizeof(KeyInfo)) + AlignSize(key.GetNameLength());
  }

  /**
   * @brief 计算某个键名字区起始偏移
   *        (Compute the starting offset of one key name).
   * @param offset 键头偏移 (Key-header offset).
   * @return 名字区起始偏移 (Starting offset of the key name).
   */
  size_t GetKeyName(size_t offset) { return offset + AlignSize(sizeof(KeyInfo)); }

  /**
   * @brief 计算指定块的起始偏移 (Compute the starting offset of one block).
   * @param block 目标块类型 (Target block type).
   * @return 块起始偏移 (Starting offset of the block).
   */
  size_t GetBlockOffset(BlockType block)
  {
    return block == BlockType::BACKUP ? block_size_ : 0;
  }

  /**
   * @brief 计算块尾校验区起始偏移 (Compute the starting offset of the checksum area).
   * @return 块尾校验区起始偏移 (Starting offset of the checksum area).
   */
  size_t GetChecksumOffset() { return block_size_ - GetChecksumSize(); }

  /**
   * @brief 计算块尾校验区字节数 (Compute the byte size of the checksum area).
   * @return 块尾校验区字节数 (Byte size of the checksum area).
   */
  size_t GetChecksumSize() { return AlignSize(sizeof(CHECKSUM_BYTE)); }

  /**
   * @brief 把指定块初始化为空数据库块 (Initialize one block as an empty database block).
   * @param block 目标块类型 (Target block type).
   */
  void InitBlock(BlockType block)
  {
    const size_t offset = GetBlockOffset(block);
    EraseFlashOrExit(offset, block_size_);

    FlashInfo info;  // padding filled with 0xFF by constructor
    info.header = FLASH_HEADER;
    KeyInfo& tmp_key = info.key;
    BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.no_next_key, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.available_flag, true);
    BlockBoolUtil<MinWriteSize>::SetFlag(tmp_key.uninit, false);
    tmp_key.SetNameLength(0);
    tmp_key.SetDataSize(0);
    WriteFlashOrExit(offset, {reinterpret_cast<uint8_t*>(&info), sizeof(FlashInfo)});
    WriteFlashOrExit(offset + GetChecksumOffset(),
                     {&CHECKSUM_BYTE, sizeof(CHECKSUM_BYTE)});
  }

  /**
   * @brief 判断块头是否已初始化 (Check whether the block header is initialized).
   * @param block 目标块类型 (Target block type).
   * @return 若块头有效则返回 `true` (Returns `true` when the block header is valid).
   */
  bool IsBlockInited(BlockType block)
  {
    const size_t offset = GetBlockOffset(block);
    FlashInfo flash_data;
    ReadFlashOrExit(offset, flash_data);
    return flash_data.header == FLASH_HEADER;
  }

  /**
   * @brief 判断块当前是否为空 (Check whether the block is currently empty).
   * @param block 目标块类型 (Target block type).
   * @return 若块内没有有效键则返回 `true`
   *         (Returns `true` when the block contains no valid key).
   */
  bool IsBlockEmpty(BlockType block)
  {
    const size_t offset = GetBlockOffset(block);
    FlashInfo flash_data;
    ReadFlashOrExit(offset, flash_data);
    return BlockBoolUtil<MinWriteSize>::ReadFlag(flash_data.key.available_flag) == true;
  }

  /**
   * @brief 判断块尾校验是否损坏 (Check whether the block checksum is corrupted).
   * @param block 目标块类型 (Target block type).
   * @return 若块尾校验不符则返回 `true`
   *         (Returns `true` when the trailing checksum is invalid).
   */
  bool IsBlockError(BlockType block)
  {
    const size_t offset = GetBlockOffset(block);
    uint32_t checksum = 0;
    ReadFlashOrExit(offset + GetChecksumOffset(), checksum);
    return checksum != CHECKSUM_BYTE;
  }

  /**
   * @brief 判断块整体是否处于可用状态
   *        (Check whether the block as a whole is currently usable).
   * @param block 目标块类型 (Target block type).
   * @return 若块头和校验都有效则返回 `true`
   *         (Returns `true` when both header and checksum are valid).
   */
  bool IsBlockValid(BlockType block)
  {
    return IsBlockInited(block) && !IsBlockError(block);
  }

  /**
   * @brief 使指定块尾校验失效 (Invalidate the checksum of one block).
   * @param block 目标块类型 (Target block type).
   */
  void InvalidateBlock(BlockType block)
  {
    const uint32_t invalid_checksum = 0;
    // Keep startup cleanup erase-free so same-bank MCUs only pay a single program op.
    WriteFlashOrExit(GetBlockOffset(block) + GetChecksumOffset(),
                     {&invalid_checksum, sizeof(invalid_checksum)});
  }

  /**
   * @brief 计算一个键总共占用的字节数 (Compute the total byte span of one key).
   * @param offset 键头偏移 (Key-header offset).
   * @return 该键占用的总字节数 (Total byte size occupied by the key).
   */
  size_t GetKeySize(size_t offset)
  {
    KeyInfo key;
    ReadFlashOrExit(offset, key);
    return AlignSize(sizeof(KeyInfo)) + AlignSize(key.GetNameLength()) +
           AlignSize(key.GetDataSize());
  }

  /**
   * @brief 计算下一键的起始偏移 (Compute the starting offset of the next key).
   * @param offset 当前键头偏移 (Current key-header offset).
   * @return 下一键的起始偏移 (Starting offset of the next key).
   */
  size_t GetNextKey(size_t offset)
  {
    KeyInfo key;
    ReadFlashOrExit(offset, key);
    return offset + GetKeySize(offset);
  }

  /**
   * @brief 计算当前块里最后一个键的偏移 (Locate the last key in the current block).
   * @param block 目标块类型 (Target block type).
   * @return 最后一个键的偏移；若块为空则返回 `0`
   *         (Offset of the last key, or `0` when the block is empty).
   */
  size_t GetLastKey(BlockType block)
  {
    if (IsBlockEmpty(block))
    {
      return 0;
    }

    KeyInfo key;
    size_t key_offset = GetBlockOffset(block) + LibXR::OffsetOf(&FlashInfo::key);
    ReadFlashOrExit(key_offset, key);
    while (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.no_next_key))
    {
      key_offset = GetNextKey(key_offset);
      ReadFlashOrExit(key_offset, key);
    }
    return key_offset;
  }

  /**
   * @brief 比较存储中的键数据和给定数据是否不同
   *        (Compare whether the stored payload differs from the given payload).
   * @param offset 键头偏移 (Key-header offset).
   * @param data 待比较数据地址 (Address of the candidate payload).
   * @param size 待比较数据字节数 (Payload size in bytes).
   * @return 若内容不同则返回 `true`
   *         (Returns `true` when the payloads differ).
   */
  bool KeyDataCompare(size_t offset, const void* data, size_t size)
  {
    KeyInfo key;
    ReadFlashOrExit(offset, key);
    size_t key_data_offset = GetKeyData(offset);
    uint8_t data_buffer = 0;
    for (size_t i = 0; i < size; i++)
    {
      ReadFlashOrExit(key_data_offset + i, data_buffer);
      if (data_buffer != (reinterpret_cast<const uint8_t*>(data))[i])
      {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 比较存储中的键名和给定名称是否不同
   *        (Compare whether the stored key name differs from the given name).
   * @param offset 键头偏移 (Key-header offset).
   * @param name 待比较键名 (Key name to compare against).
   * @return 若名称不同则返回 `true`
   *         (Returns `true` when the names differ).
   */
  bool KeyNameCompare(size_t offset, const char* name)
  {
    KeyInfo key;
    ReadFlashOrExit(offset, key);
    for (size_t i = 0; i < key.GetNameLength(); i++)
    {
      uint8_t data_buffer = 0;
      ReadFlashOrExit(offset + AlignSize(sizeof(KeyInfo)) + i, data_buffer);
      if (data_buffer != name[i])
      {
        return true;
      }
    }
    return false;
  }

  /**
   * @brief 试算一个块里已用空间，并在发现布局损坏时提前失败
   *        (Try to compute used block size and fail early on invalid layout).
   * @param block 目标块类型 (Target block type).
   * @param used_size 输出已用字节数 (Receives the used byte size).
   * @return 若成功计算则返回 `true`
   *         (Returns `true` when the used size is computed successfully).
   */
  bool TryGetUsedBlockSize(BlockType block, size_t& used_size)
  {
    const size_t block_offset = GetBlockOffset(block);
    const size_t checksum_offset = block_offset + GetChecksumOffset();
    size_t key_offset = block_offset + LibXR::OffsetOf(&FlashInfo::key);

    while (true)
    {
      if (key_offset + AlignSize(sizeof(KeyInfo)) > checksum_offset)
      {
        return false;
      }

      KeyInfo key;
      ReadFlashOrExit(key_offset, key);

      // Recovery cannot trust erased or half-written key metadata to bound copies.
      if (!BlockBoolUtil<MinWriteSize>::Valid(key.no_next_key) ||
          !BlockBoolUtil<MinWriteSize>::Valid(key.available_flag) ||
          !BlockBoolUtil<MinWriteSize>::Valid(key.uninit))
      {
        return false;
      }

      const size_t next_key_offset =
          key_offset + AlignSize(sizeof(KeyInfo)) + AlignSize(key.GetNameLength()) +
          AlignSize(key.GetDataSize());
      if (next_key_offset > checksum_offset)
      {
        return false;
      }

      if (BlockBoolUtil<MinWriteSize>::ReadFlag(key.no_next_key))
      {
        used_size = next_key_offset - block_offset;
        return true;
      }

      key_offset = next_key_offset;
    }
  }

  /**
   * @brief 计算一个块当前已用的总字节数 (Compute the currently used byte span of one block).
   * @param block 目标块类型 (Target block type).
   * @return 当前已用的总字节数 (Currently used byte span of the block).
   */
  size_t GetUsedBlockSize(BlockType block)
  {
    const size_t block_offset = GetBlockOffset(block);
    const size_t first_key_offset = block_offset + LibXR::OffsetOf(&FlashInfo::key);

    if (IsBlockEmpty(block))
    {
      return GetNextKey(first_key_offset) - block_offset;
    }

    return GetNextKey(GetLastKey(block)) - block_offset;
  }

  /**
   * @brief 在两个块之间按最小写入单元复制数据
   *        (Copy data between blocks in minimum-write-size chunks).
   * @param dst_offset 目标偏移 (Destination offset).
   * @param src_offset 源偏移 (Source offset).
   * @param size 待复制字节数 (Byte count to copy).
   */
  void CopyFlashData(size_t dst_offset, size_t src_offset, size_t size)
  {
    for (size_t i = 0; i < size; i += MinWriteSize)
    {
      ReadFlashOrExit(src_offset + i, {write_buffer_, MinWriteSize});
      WriteFlashOrExit(dst_offset + i, {write_buffer_, MinWriteSize});
    }
  }

  /**
   * @brief 复制活跃键前缀和块尾校验 (Copy the live key prefix and trailing checksum).
   * @param dst_block 目标块类型 (Destination block type).
   * @param src_block 源块类型 (Source block type).
   * @param used_size 活跃前缀总字节数 (Byte size of the live prefix).
   */
  void CopyBlockPrefixAndChecksum(BlockType dst_block, BlockType src_block,
                                  size_t used_size)
  {
    const size_t dst_offset = GetBlockOffset(dst_block);
    const size_t src_offset = GetBlockOffset(src_block);
    const size_t checksum_offset = GetChecksumOffset();

    ASSERT(used_size <= checksum_offset);
    // Only the live key prefix and checksum are needed; erased tail bytes are irrelevant.
    CopyFlashData(dst_offset, src_offset, used_size);
    CopyFlashData(dst_offset + checksum_offset, src_offset + checksum_offset,
                  GetChecksumSize());
  }

  /**
   * @brief 在主块里按名称查找键，并在删除项过多时触发回收
   *        (Search one key by name in the main block and trigger recycle when
   *        too many tombstones are observed).
   * @param name 待查找键名 (Key name to search for).
   * @return 找到时返回键头偏移，找不到返回 `0`
   *         (Returns the key-header offset when found, otherwise `0`).
   */
  size_t SearchKey(const char* name)
  {
    if (IsBlockEmpty(BlockType::MAIN))
    {
      return 0;
    }

    KeyInfo key;
    size_t key_offset = LibXR::OffsetOf(&FlashInfo::key);
    ReadFlashOrExit(key_offset, key);

    size_t ans = 0, need_cycle = 0;

    while (true)
    {
      if (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.available_flag))
      {
        key_offset = GetNextKey(key_offset);
        ReadFlashOrExit(key_offset, key);
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
      ReadFlashOrExit(key_offset, key);
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
        auto ec = flash_.Write(offset, {data.addr_, final_block_index});
        if (ec != ErrorCode::OK)
        {
          return ec;
        }
      }
      Memory::FastSet(write_buffer_, 0xff, MinWriteSize);
      LibXR::Memory::FastCopy(
          write_buffer_, reinterpret_cast<const uint8_t*>(data.addr_) + final_block_index,
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
    ReadFlashOrExit(ans, key_buffer);

    if (key.raw_data_.size_ != key_buffer.GetDataSize())
    {
      return ErrorCode::FAILED;
    }

    ReadFlashOrExit(GetKeyData(ans), key.raw_data_);

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

  /**
   * @brief 添加新键到数据库 (Add a new key to the database).
   * @param key 需要添加的键 (Key to add).
   * @return 操作结果 (Operation result).
   */
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
    if (!IsBlockValid(BlockType::MAIN))
    {
      if (!IsBlockValid(BlockType::BACKUP) || IsBlockEmpty(BlockType::BACKUP))
      {
        InitBlock(BlockType::MAIN);
      }
      else
      {
        size_t used_size = 0;
        if (!TryGetUsedBlockSize(BlockType::BACKUP, used_size))
        {
          InvalidateBlock(BlockType::BACKUP);
          InitBlock(BlockType::MAIN);
        }
        else
        {
          EraseFlashOrExit(0, block_size_);
          CopyBlockPrefixAndChecksum(BlockType::MAIN, BlockType::BACKUP, used_size);
          InvalidateBlock(BlockType::BACKUP);
        }
      }
    }
    else if (IsBlockValid(BlockType::BACKUP) && !IsBlockEmpty(BlockType::BACKUP))
    {
      // A stale backup must not stay recoverable once the main block is already good.
      InvalidateBlock(BlockType::BACKUP);
    }

    KeyInfo key;
    size_t key_offset = LibXR::OffsetOf(&FlashInfo::key);
    ReadFlashOrExit(key_offset, key);
    size_t need_cycle = 0;
    while (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.no_next_key))
    {
      key_offset = GetNextKey(key_offset);

      if (key_offset + AlignSize(sizeof(KeyInfo)) > GetChecksumOffset())
      {
        InitBlock(BlockType::MAIN);
        break;
      }

      ReadFlashOrExit(key_offset, key);

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
    size_t key_offset = LibXR::OffsetOf(&FlashInfo::key);
    ReadFlashOrExit(key_offset, key);

    if (!IsBlockValid(BlockType::BACKUP) || !IsBlockEmpty(BlockType::BACKUP))
    {
      InitBlock(BlockType::BACKUP);
    }

    size_t write_buff_offset = LibXR::OffsetOf(&FlashInfo::key) + block_size_;

    auto new_key = KeyInfo{};
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.uninit, false);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.available_flag, false);
    BlockBoolUtil<MinWriteSize>::SetFlag(new_key.no_next_key, false);
    WriteFlashOrExit(write_buff_offset, new_key);

    write_buff_offset += GetKeySize(write_buff_offset);

    do
    {
      key_offset = GetNextKey(key_offset);
      ReadFlashOrExit(key_offset, key);

      if (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.available_flag))
      {
        continue;
      }

      WriteFlashOrExit(write_buff_offset, key);
      write_buff_offset += AlignSize(sizeof(KeyInfo));
      CopyFlashData(write_buff_offset, GetKeyName(key_offset), key.GetNameLength());
      write_buff_offset += AlignSize(key.GetNameLength());
      CopyFlashData(write_buff_offset, GetKeyData(key_offset), key.GetDataSize());
      write_buff_offset += AlignSize(key.GetDataSize());
    } while (!BlockBoolUtil<MinWriteSize>::ReadFlag(key.no_next_key));

    const size_t used_size = write_buff_offset - block_size_;
    EraseFlashOrExit(0, block_size_);
    CopyBlockPrefixAndChecksum(BlockType::MAIN, BlockType::BACKUP, used_size);

    InitBlock(BlockType::BACKUP);

    return ErrorCode::OK;
  }
};

}  // namespace LibXR
