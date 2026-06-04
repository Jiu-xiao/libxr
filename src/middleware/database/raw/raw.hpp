#pragma once

#include <cstdint>
#include <cstring>

#include "flash.hpp"
#include "interface.hpp"

namespace LibXR
{

/**
 * @brief 适用于最小写入单元受限的 Flash 存储的数据库实现
 *        (Database implementation for Flash storage with minimum write unit
 *        restrictions).
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
 *         storage).
 */
template <size_t MinWriteSize>
class DatabaseRaw : public Database
{
  static constexpr uint32_t FLASH_HEADER =
      0x12345678 + LIBXR_DATABASE_VERSION;  ///< Flash 头部标识 (Flash header identifier).

  static constexpr uint32_t CHECKSUM_BYTE = 0x9abcedf0;  ///< 校验字节 (Checksum byte).

#include "layout.hpp"

  size_t recycle_threshold_ = 0;  ///< 回收阈值 (Recycle threshold).
  Flash& flash_;                  ///< 目标 Flash 存储设备 (Target Flash storage device).
  uint32_t block_size_;           ///< Flash 块大小 (Flash block size).
  uint8_t write_buffer_[MinWriteSize];  ///< 写入缓冲区 (Write buffer).

#include "flash_io.hpp"
#include "block_ops.hpp"
#include "key_ops.hpp"

 public:
#include "lifecycle.hpp"
};

}  // namespace LibXR
