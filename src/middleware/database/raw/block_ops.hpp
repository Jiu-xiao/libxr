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
