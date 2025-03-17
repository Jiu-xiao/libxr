#pragma once

#include <cstdint>

#include "libxr_type.hpp"

namespace LibXR
{

/**
 * @class Flash
 * @brief Abstract base class representing a flash memory interface.
 * 抽象基类，表示闪存接口。
 */
class Flash
{
 public:
  /**
   * @brief Constructs a Flash object with specified properties.
   * 构造函数，初始化闪存属性。
   * @param min_erase_size Minimum erasable block size in bytes.
   * 最小可擦除块大小（字节）。
   * @param min_write_size Minimum writable block size in bytes. 最小可写块大小（字节）。
   * @param flash_area Memory area allocated for flash operations.
   * 用于闪存操作的存储区域。
   */
  Flash(size_t min_erase_size, size_t min_write_size, RawData flash_area)
      : min_erase_size_(min_erase_size),
        min_write_size_(min_write_size),
        flash_area_(flash_area)
  {
  }

  size_t min_erase_size_ =
      0;  ///< Minimum erasable block size in bytes. 最小可擦除块大小（字节）。
  size_t min_write_size_ =
      0;  ///< Minimum writable block size in bytes. 最小可写块大小（字节）。
  RawData flash_area_;  ///< Memory area allocated for flash operations.
                        ///< 用于闪存操作的存储区域。

  /**
   * @brief Erases a section of the flash memory. 擦除闪存的指定区域。
   * @param offset The starting offset of the section to erase. 要擦除的起始偏移地址。
   * @param size The size of the section to erase. 要擦除的区域大小。
   * @return ErrorCode indicating success or failure. 返回操作结果的错误码。
   */
  virtual ErrorCode Erase(size_t offset, size_t size) = 0;

  /**
   * @brief Writes data to the flash memory. 向闪存写入数据。
   * @param offset The starting offset to write data. 数据写入的起始偏移地址。
   * @param data The data to be written. 需要写入的数据。
   * @return ErrorCode indicating success or failure. 返回操作结果的错误码。
   */
  virtual ErrorCode Write(size_t offset, ConstRawData data) = 0;
};

}  // namespace LibXR
