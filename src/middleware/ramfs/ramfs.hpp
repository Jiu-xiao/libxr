#pragma once

#include <cstring>
#include <type_traits>
#include <utility>

#include "libxr_assert.hpp"
#include "libxr_type.hpp"
#include "rbt.hpp"

namespace LibXR
{
/**
 * @class RamFS
 * @brief 轻量级内存文件系统 / Lightweight in-memory file system
 *
 * RamFS 组织外部内存文件、可执行文件、目录和自定义节点 / RamFS organizes
 * external-memory files, executable files, directories, and custom nodes.
 * 文件数据由调用方持有 / File payload storage is owned by the caller.
 */
class RamFS
{
 public:
  /**
   * @brief 构造 RamFS，并创建根目录和 `bin` 目录 / Construct RamFS with root and `bin`
   * directories
   * @param name 根目录名称 / Root directory name
   *
   * @note 包含动态内存分配 / Contains dynamic memory allocation
   */
  RamFS(const char* name = "ramfs");

  /**
   * @brief 文件系统节点类型 / File-system node type
   */
  enum class FsNodeType : uint8_t
  {
    FILE,    ///< 文件 / File
    DIR,     ///< 目录 / Directory
    CUSTOM,  ///< 用户自定义节点 / User-defined node
  };

  class Dir;

 private:
  using Tree = RBTree<const char*>;

  enum class FileType : uint8_t
  {
    READ_ONLY,
    READ_WRITE,
    EXEC,
  };

  static int CompareStr(const char* const& a, const char* const& b);

 public:
#include "fs_node.hpp"
#include "file.hpp"
#include "custom.hpp"
#include "dir.hpp"
#include "factory.hpp"

 private:
  static char* DuplicateName(const char* name);
};
}  // namespace LibXR
