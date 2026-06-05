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
  /**
   * @brief RamFS 目录索引树类型 / Directory index tree type used by RamFS
   */
  using Tree = RBTree<const char*>;

  /**
   * @brief 文件内部存储形态 / Internal storage kind of one file node
   */
  enum class FileType : uint8_t
  {
    READ_ONLY,   ///< 只读外部数据映射 / Read-only external-data view
    READ_WRITE,  ///< 可写外部数据映射 / Read-write external-data view
    EXEC,        ///< 可执行命令入口 / Executable command entry
  };

  /**
   * @brief RamFS 名称比较函数 / Name comparator used by RamFS trees
   * @param a 左侧名称 / Left name
   * @param b 右侧名称 / Right name
   * @return 比较结果，遵循 `strcmp()` 语义 / Comparison result following `strcmp()`
   *         semantics
   */
  static int CompareStr(const char* const& a, const char* const& b);

 public:
  /**
   * @brief 节点基类片段 / Base-node fragment
   */
#include "fs_node.hpp"
  /**
   * @brief 文件节点片段 / File-node fragment
   */
#include "file.hpp"
  /**
   * @brief 自定义节点片段 / Custom-node fragment
   */
#include "custom.hpp"
  /**
   * @brief 目录节点片段 / Directory-node fragment
   */
#include "dir.hpp"
  /**
   * @brief 工厂与根入口片段 / Factory and root-entry fragment
   */
#include "factory.hpp"

 private:
  /**
   * @brief 复制并持有一个节点名称 / Duplicate and retain one node name
   * @param name 原始名称 / Source name
   * @return 新分配的名称缓冲区 / Newly allocated name buffer
   *
   * @note 当前 RamFS 通过复制名称来保证节点名在文件系统生命周期内稳定存在。
   *       The current RamFS duplicates names so each node keeps a stable name
   *       pointer for the lifetime of the filesystem structure.
   */
  static char* DuplicateName(const char* name);
};
}  // namespace LibXR
