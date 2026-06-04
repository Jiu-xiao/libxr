  /**
   * @brief `DatabaseRaw` 的生命周期与主流程片段 / Lifecycle and main-flow fragment of
   *        `DatabaseRaw`
   *
   * @note 这一组函数组成后端真正对外可见的行为：构造时约束检查、启动恢复、运行期
   *       读写、显式还原，以及回收流程。
   *       This group forms the backend's externally visible behavior: contract
   *       checks during construction, startup recovery, runtime read/write,
   *       explicit restore, and recycling flow.
   */

  /**
   * @brief 获取数据库中的键值
   *        (Retrieve the key's value from the database).
   * @param key 需要获取的键 (Key to retrieve).
   * @return 操作结果，如果找到则返回 `ErrorCode::OK`，否则返回 `ErrorCode::NOT_FOUND`
   *         (Operation result, returns `ErrorCode::OK` if found, otherwise
   *         `ErrorCode::NOT_FOUND`).
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
   * @note 启动策略是：优先相信合法主块；主块损坏时才尝试从合法且非空的备份块恢复；若
   *       主块已好而备份块仍残留，则把备份块失效掉，避免它继续被当作恢复来源。
   *       The startup policy is: trust a valid main block first; recover from a
   *       valid non-empty backup block only when the main block is broken; and
   *       invalidate any stale backup once the main block is already good so it
   *       cannot remain a recovery source.
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

      // 当前策略仍然是“看到未完成键就整块重建”，还没有实现更细粒度的坏数据恢复。
      // The current policy still rebuilds the whole block when it sees an
      // incomplete key; finer-grained damaged-data recovery is not implemented
      // yet.
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
   * @note 这里会把主块和备份块都重新初始化为空块，不保留任何旧键。
   *       This reinitializes both the main block and the backup block into
   *       empty blocks and keeps no previous key.
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
   * block.
   * 将主存储块中的有效键移动到备份块，并擦除主存储块。
   *
   * @return 操作结果 (Operation result).
   * @note 当前流程把备份块当作临时整理区：先把主块中的活跃键顺序搬过去，再用备份块的
   *       活跃前缀回写主块，最后把备份块重新清为空块。
   *       The current flow treats the backup block as a temporary compaction
   *       area: it first copies live keys from the main block into the backup
   *       block in order, then rewrites the main block from the backup's live
   *       prefix, and finally clears the backup block back to empty.
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
