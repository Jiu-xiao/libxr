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
