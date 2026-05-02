#pragma once

#include <cstring>
#include <type_traits>
#include <utility>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
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
  /**
   * @class FsNode
   * @brief 文件系统节点基类 / Base class for all RamFS nodes
   */
  class FsNode
  {
   public:
    /**
     * @brief 获取节点类型 / Get the node type
     * @return 节点类型 / Node type
     */
    [[nodiscard]] FsNodeType GetNodeType() const { return type_; }

    /**
     * @brief 获取节点名称 / Get the node name
     * @return 节点名称 / Node name
     */
    [[nodiscard]] const char* GetName() const { return name_; }

   protected:
    const char* name_ = nullptr;
    FsNodeType type_;
    Dir* parent_ = nullptr;

    explicit FsNode(FsNodeType node_type);
    FsNode(const FsNode& other);
    FsNode& operator=(const FsNode&) = delete;

    Tree::Node<FsNode*> tree_node_;

    friend class Dir;
  };

  /**
   * @class File
   * @brief 内存文件或可执行文件 / Memory file or executable file
   */
  class File : public FsNode
  {
   public:
    /**
     * @brief 判断文件是否只读 / Check whether the file is read-only
     * @return 只读返回 true / True if the file is read-only
     */
    [[nodiscard]] bool IsReadOnly() const { return file_type_ == FileType::READ_ONLY; }

    /**
     * @brief 判断文件是否可写 / Check whether the file is writable
     * @return 可写返回 true / True if the file is writable
     */
    [[nodiscard]] bool IsReadWrite() const { return file_type_ == FileType::READ_WRITE; }

    /**
     * @brief 判断文件是否可执行 / Check whether the file is executable
     * @return 可执行返回 true / True if the file is executable
     */
    [[nodiscard]] bool IsExecutable() const { return file_type_ == FileType::EXEC; }

    /**
     * @brief 执行可执行文件 / Run an executable file
     * @param argc 参数数量 / Argument count
     * @param argv 参数数组 / Argument vector
     * @return 执行返回值 / Execution return value
     */
    int Run(int argc, char** argv);

    /**
     * @brief 访问类型化数据 / Access typed data
     *
     * `Data<T>()` 返回可写引用并要求 READ_WRITE / `Data<T>()` returns a writable
     * reference and requires READ_WRITE.
     * `Data<const T>()` 返回只读引用 / `Data<const T>()` returns a read-only
     * reference for both READ_ONLY and READ_WRITE.
     *
     * @tparam DataType 数据类型；使用 const T 表示只读访问 / Data type; use const T
     *                  for read-only access.
     * @tparam LimitMode 大小检查模式 / Size-check mode
     * @return 类型化数据引用 / Typed data reference
     */
    template <typename DataType, SizeLimitMode LimitMode = SizeLimitMode::MORE>
    decltype(auto) Data()
    {
      using RequestedType = std::remove_reference_t<DataType>;
      using StoredType = std::remove_cv_t<RequestedType>;
      static_assert(!std::is_reference_v<DataType>);
      static_assert(!std::is_volatile_v<RequestedType>);

      LibXR::Assert::SizeLimitCheck<LimitMode>(sizeof(StoredType), size_);
      if constexpr (std::is_const_v<RequestedType>)
      {
        if (file_type_ == FileType::READ_WRITE)
        {
          return *static_cast<const StoredType*>(addr_);
        }
        if (file_type_ == FileType::READ_ONLY)
        {
          return *static_cast<const StoredType*>(addr_const_);
        }

        ASSERT(false);
        const void* null_data = nullptr;
        return *static_cast<const StoredType*>(null_data);
      }
      else
      {
        ASSERT(file_type_ == FileType::READ_WRITE);
        return *static_cast<StoredType*>(addr_);
      }
    }

    /**
     * @brief 从 const 文件对象访问类型化只读数据 / Access typed read-only data from a
     * const file object
     *
     * @tparam DataType 数据类型 / Data type
     * @tparam LimitMode 大小检查模式 / Size-check mode
     * @return 类型化只读数据引用 / Typed read-only data reference
     */
    template <typename DataType, SizeLimitMode LimitMode = SizeLimitMode::MORE>
    decltype(auto) Data() const
    {
      using RequestedType = std::remove_reference_t<DataType>;
      using StoredType = std::remove_cv_t<RequestedType>;
      static_assert(!std::is_reference_v<DataType>);
      static_assert(!std::is_volatile_v<RequestedType>);

      LibXR::Assert::SizeLimitCheck<LimitMode>(sizeof(StoredType), size_);
      if (file_type_ == FileType::READ_WRITE)
      {
        return *static_cast<const StoredType*>(addr_);
      }
      if (file_type_ == FileType::READ_ONLY)
      {
        return *static_cast<const StoredType*>(addr_const_);
      }

      ASSERT(false);
      const void* null_data = nullptr;
      return *static_cast<const StoredType*>(null_data);
    }

    /**
     * @brief 访问可写原始数据，要求文件为 READ_WRITE / Access writable raw data;
     * requires READ_WRITE
     * @return 可写原始数据视图 / Writable raw data view
     */
    [[nodiscard]] RawData Data()
    {
      ASSERT(file_type_ == FileType::READ_WRITE);
      return RawData(addr_, size_);
    }

    /**
     * @brief 访问只读原始数据 / Access read-only raw data
     * @return 只读原始数据视图 / Read-only raw data view
     */
    [[nodiscard]] ConstRawData Data() const
    {
      if (file_type_ == FileType::READ_WRITE)
      {
        return ConstRawData(addr_, size_);
      }
      if (file_type_ == FileType::READ_ONLY)
      {
        return ConstRawData(addr_const_, size_);
      }
      ASSERT(false);
      return ConstRawData();
    }

   private:
    using ExecFun = int (*)(void* raw, int argc, char** argv);

    File();
    explicit File(const char* name);

    union
    {
      void* addr_;
      const void* addr_const_;
      ExecFun exec_;
    };

    void* arg_ = nullptr;
    size_t size_ = 0;
    FileType file_type_ = FileType::READ_ONLY;

    friend class RamFS;
  };

  /**
   * @class Custom
   * @brief 用户自定义节点，RamFS 仅负责命名和查找 / User-defined node; RamFS only
   * stores and finds it by name
   */
  class Custom : public FsNode
  {
   public:
    /**
     * @brief 构造自定义节点 / Construct a custom node
     * @param name 节点名称 / Node name
     * @param kind 用户定义类型 / User-defined kind
     * @param context 用户上下文指针 / User context pointer
     *
     * @note 包含动态内存分配 / Contains dynamic memory allocation
     */
    explicit Custom(const char* name, uint32_t kind = 0, void* context = nullptr);

    uint32_t kind_ = 0;        ///< 用户定义类型 / User-defined kind
    void* context_ = nullptr;  ///< 用户上下文指针 / User context pointer

   private:
    Custom();
  };

  /**
   * @class Dir
   * @brief 目录节点，管理直属子节点 / Directory node that owns a child namespace
   */
  class Dir : public FsNode
  {
   public:
    /**
     * @brief 添加直属文件节点 / Add a direct child file node
     * @param file 文件节点 / File node
     */
    void Add(File& file) { AddNode(file); }

    /**
     * @brief 添加直属目录节点 / Add a direct child directory node
     * @param dir 目录节点 / Directory node
     */
    void Add(Dir& dir) { AddNode(dir); }

    /**
     * @brief 添加直属自定义节点 / Add a direct child custom node
     * @param custom 自定义节点 / Custom node
     */
    void Add(Custom& custom) { AddNode(custom); }

    /**
     * @brief 查找直属子节点 / Find a direct child node
     * @param name 节点名称 / Node name
     * @return 子节点指针；未找到返回 nullptr / Child node pointer, or nullptr
     */
    FsNode* FindNode(const char* name);

    /**
     * @brief 查找直属文件 / Find a direct child file
     * @param name 文件名 / File name
     * @return 文件指针；未找到返回 nullptr / File pointer, or nullptr
     */
    File* FindFile(const char* name);

    /**
     * @brief 递归查找文件 / Find a file recursively
     * @param name 文件名 / File name
     * @return 文件指针；未找到返回 nullptr / File pointer, or nullptr
     */
    File* FindFileRev(const char* name);

    /**
     * @brief 查找直属目录，支持 "." 和 ".." / Find a direct child directory,
     * supporting "." and ".."
     * @param name 目录名 / Directory name
     * @return 目录指针；未找到返回 nullptr / Directory pointer, or nullptr
     */
    Dir* FindDir(const char* name);

    /**
     * @brief 递归查找目录，支持 "." 和 ".." / Find a directory recursively,
     * supporting "." and ".."
     * @param name 目录名 / Directory name
     * @return 目录指针；未找到返回 nullptr / Directory pointer, or nullptr
     */
    Dir* FindDirRev(const char* name);

    /**
     * @brief 查找直属自定义节点 / Find a direct child custom node
     * @param name 节点名称 / Node name
     * @return 自定义节点指针；未找到返回 nullptr / Custom node pointer, or nullptr
     */
    Custom* FindCustom(const char* name);

    /**
     * @brief 递归查找自定义节点 / Find a custom node recursively
     * @param name 节点名称 / Node name
     * @return 自定义节点指针；未找到返回 nullptr / Custom node pointer, or nullptr
     */
    Custom* FindCustomRev(const char* name);

    /**
     * @brief 遍历直属子节点 / Iterate over direct child nodes
     * @tparam Func 回调类型 / Callback type
     * @param func 回调函数，返回非 OK 时停止遍历 / Callback; non-OK stops iteration
     * @return 遍历结果 / Iteration result
     */
    template <typename Func>
    ErrorCode Foreach(Func func)
    {
      return rbt_.Foreach<FsNode*>([&](Tree::Node<FsNode*>& node)
                                   { return func(*node.data_); });
    }

   private:
    Dir();
    explicit Dir(const char* name);

    void AddNode(FsNode& node);
    FsNode* FindNodeByType(const char* name, FsNodeType type);
    FsNode* FindNodeRevByType(const char* name, FsNodeType type);

    Tree rbt_;

    friend class RamFS;
  };

  /**
   * @brief 创建引用外部数据的文件 / Create a file referencing external data
   * @tparam DataType 外部数据类型 / External data type
   * @param name 文件名 / File name
   * @param raw 外部数据引用 / External data reference
   * @return 文件节点 / File node
   *
   * @note 包含动态内存分配 / Contains dynamic memory allocation
   */
  template <typename DataType>
  static File CreateFile(const char* name, DataType& raw)
  {
    using StoredType = std::remove_reference_t<DataType>;

    File file(name);
    if constexpr (std::is_const_v<StoredType>)
    {
      file.file_type_ = FileType::READ_ONLY;
      file.addr_const_ = &raw;
    }
    else
    {
      file.file_type_ = FileType::READ_WRITE;
      file.addr_ = &raw;
    }

    file.size_ = sizeof(StoredType);
    return file;
  }

  /**
   * @brief 创建可执行文件 / Create an executable file
   * @tparam ArgType 执行上下文参数类型 / Execution context argument type
   * @param name 文件名 / File name
   * @param exec 执行函数 / Execution function
   * @param arg 执行上下文参数 / Execution context argument
   * @return 可执行文件节点 / Executable file node
   *
   * @note 包含动态内存分配 / Contains dynamic memory allocation
   */
  template <typename ArgType>
  static File CreateFile(const char* name,
                         int (*exec)(ArgType arg, int argc, char** argv), ArgType&& arg)
  {
    using StoredArgType = std::remove_reference_t<ArgType>;
    struct ExecutableBlock
    {
      StoredArgType arg_;
      decltype(exec) exec_fun_;
    };

    File file(name);

    auto block = new ExecutableBlock{std::forward<ArgType>(arg), exec};
    file.file_type_ = FileType::EXEC;
    file.arg_ = block;

    file.exec_ = [](void* raw, int argc, char** argv)
    {
      auto* block = static_cast<ExecutableBlock*>(raw);
      return block->exec_fun_(block->arg_, argc, argv);
    };

    return file;
  }

  /**
   * @brief 创建命令兼容入口，返回可执行文件 / Create a command-compatible executable
   * file
   * @tparam ArgType 执行上下文参数类型 / Execution context argument type
   * @param name 文件名 / File name
   * @param exec 执行函数 / Execution function
   * @param arg 执行上下文参数 / Execution context argument
   * @return 可执行文件节点 / Executable file node
   *
   * @note 包含动态内存分配 / Contains dynamic memory allocation
   */
  template <typename ArgType>
  static File CreateCommand(const char* name,
                            int (*exec)(ArgType arg, int argc, char** argv),
                            ArgType&& arg)
  {
    return CreateFile(name, exec, std::forward<ArgType>(arg));
  }

  /**
   * @brief 创建目录节点 / Create a directory node
   * @param name 目录名称 / Directory name
   * @return 目录节点 / Directory node
   *
   * @note 包含动态内存分配 / Contains dynamic memory allocation
   */
  static Dir CreateDir(const char* name) { return Dir(name); }

  /**
   * @brief 添加文件节点到根目录 / Add a file node to the root directory
   * @param file 文件节点 / File node
   */
  void Add(File& file) { root_.Add(file); }

  /**
   * @brief 添加目录节点到根目录 / Add a directory node to the root directory
   * @param dir 目录节点 / Directory node
   */
  void Add(Dir& dir) { root_.Add(dir); }

  /**
   * @brief 添加自定义节点到根目录 / Add a custom node to the root directory
   * @param custom 自定义节点 / Custom node
   */
  void Add(Custom& custom) { root_.Add(custom); }

  /**
   * @brief 从整个 RamFS 递归查找文件 / Find a file recursively from the RamFS root
   * @param name 文件名 / File name
   * @return 文件指针；未找到返回 nullptr / File pointer, or nullptr
   */
  File* FindFile(const char* name) { return root_.FindFileRev(name); }

  /**
   * @brief 从整个 RamFS 递归查找目录 / Find a directory recursively from the RamFS root
   * @param name 目录名 / Directory name
   * @return 目录指针；未找到返回 nullptr / Directory pointer, or nullptr
   */
  Dir* FindDir(const char* name) { return root_.FindDirRev(name); }

  /**
   * @brief 从整个 RamFS 递归查找自定义节点 / Find a custom node recursively from the
   * RamFS root
   * @param name 节点名称 / Node name
   * @return 自定义节点指针；未找到返回 nullptr / Custom node pointer, or nullptr
   */
  Custom* FindCustom(const char* name) { return root_.FindCustomRev(name); }

  Dir root_;  ///< 根目录 / Root directory
  Dir bin_;   ///< 可执行文件目录 / Executable-file directory

 private:
  static char* DuplicateName(const char* name);
};
}  // namespace LibXR
