#pragma once

#include <functional>

#include "libxr_assert.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "rbt.hpp"

namespace LibXR
{
/**
 * @class RamFS
 * @brief  轻量级的内存文件系统，实现基本的文件、目录和设备管理
 *         A lightweight in-memory file system implementing basic file, directory, and
 * device management
 */
class RamFS
{
 public:
  /**
   * @brief  构造函数，初始化内存文件系统的根目录
   *         Constructor that initializes the root directory of the in-memory file system
   * @param  name 根目录的名称（默认为 "ramfs"）
   *         Name of the root directory (default: "ramfs")
   */
  RamFS(const char *name = "ramfs");

  /**
   * @brief  比较两个字符串
   *         Compares two strings
   * @param  a 字符串 A String A
   * @param  b 字符串 B String B
   * @return int 比较结果 Comparison result
   */
  static int CompareStr(const char *const &a, const char *const &b);

  /**
   * @enum FsNodeType
   * @brief  文件系统节点类型
   *         Types of file system nodes
   */
  enum class FsNodeType : uint8_t
  {
    FILE,     ///< 文件 File
    DIR,      ///< 目录 Directory
    DEVICE,   ///< 设备 Device
    STORAGE,  ///< 存储 Storage
    UNKNOWN,   ///< 未知 Unknown
  };

  /**
   * @enum FileType
   * @brief  文件类型
   *         Types of files
   */
  enum class FileType : uint8_t
  {
    READ_ONLY,   ///< 只读 Read-only
    READ_WRITE,  ///< 读写 Read/Write
    EXEC,        ///< 可执行 Executable
  };

  class Dir;

  /**
   * @class FsNode
   * @brief  文件系统节点基类，所有文件和目录均继承自该类
   *         Base class for file system nodes; all files and directories inherit from this
   */
  class FsNode
  {
   public:
    const char *name;
    FsNodeType type;
    Dir *parent;
  };

  /**
   * @class FileNode
   * @brief  文件节点类，继承自 FsNode，表示文件
   *         File node class, inheriting from FsNode, representing a file
   */
  typedef class FileNode : public FsNode
  {
   public:
    union
    {
      void *addr;              ///< 读写地址 Read/Write address
      const void *addr_const;  ///< 只读地址 Read-only address
      int (*exec)(void *raw, int argc,
                  char **argv);  ///< 可执行文件指针 Executable function pointer
    };

    union
    {
      size_t size;  ///< 文件大小 File size
      void *arg;    ///< 可执行文件参数 Executable file argument
    };

    FileType type;  ///< 文件类型 File type

    /**
     * @brief  运行可执行文件
     *         Runs an executable file
     * @param  argc 参数数量 Number of arguments
     * @param  argv 参数列表 Argument list
     * @return int 执行结果 Execution result
     */
    int Run(int argc, char **argv);

    /**
     * @brief  获取文件数据
     *         Retrieves file data
     * @tparam DataType 数据类型 Data type
     * @tparam LimitMode 大小限制模式 Size limit mode (默认：MORE)
     * @return const DataType& 数据引用 Reference to the data
     */
    template <typename DataType, SizeLimitMode LimitMode = SizeLimitMode::MORE>
    const DataType &GetData()
    {
      LibXR::Assert::SizeLimitCheck<LimitMode>(sizeof(DataType), size);
      if (type == FileType::READ_WRITE)
      {
        return *reinterpret_cast<DataType *>(addr);
      }
      else if (type == FileType::READ_ONLY)
      {
        return *reinterpret_cast<const DataType *>(addr_const);
      }
      else
      {
        ASSERT(false);
        const void *addr = nullptr;
        return *reinterpret_cast<const DataType *>(addr);
      }
    }
  } FileNode;

  typedef RBTree<const char *>::Node<FileNode> File;

  /**
   * @class DeviceNode
   * @brief  设备节点，继承自 FsNode
   *         Device node, inheriting from FsNode
   */
  struct DeviceNode : public FsNode
  {
    ReadPort read_port;    ///< 读端口 Read port
    WritePort write_port;  ///< 写端口 Write port
  };

  /**
   * @class Device
   * @brief  设备类，继承自红黑树节点 DeviceNode
   *         Device class inheriting from Red-Black tree node DeviceNode
   */
  class Device : public RBTree<const char *>::Node<DeviceNode>
  {
   public:
    /**
     * @brief  设备构造函数
     *         Device constructor
     * @param  name 设备名称 Device name
     * @param  read_port 读取端口（默认 ReadPort()）Read port (default: ReadPort())
     * @param  write_port 写入端口（默认 WritePort()）Write port (default: WritePort())
     */
    Device(const char *name, const ReadPort &read_port = ReadPort(),
           const WritePort &write_port = WritePort());

    /**
     * @brief  读取设备数据
     *         Reads data from the device
     * @tparam ReadOperation 读取操作类型 Read operation type
     * @param  op 读取操作 Read operation
     * @param  data 读取数据 Data to be read
     * @return ErrorCode 错误码 Error code
     */
    template <typename ReadOperation>
    ErrorCode Read(ReadOperation &&op, RawData data)
    {
      return data_.read_port(data, std::forward<ReadOperation>(op));
    }

    /**
     * @brief  向设备写入数据
     *         Writes data to the device
     * @tparam WriteOperation 写入操作类型 Write operation type
     * @param  op 写入操作 Write operation
     * @param  data 写入数据 Data to be written
     * @return ErrorCode 错误码 Error code
     */
    template <typename WriteOperation>
    ErrorCode Write(WriteOperation &&op, ConstRawData data)
    {
      return data_.write_port(data, std::forward<WriteOperation>(op));
    }

    uint32_t device_type;  ///< 设备类型 Device type
  };

  typedef struct
  {
    // TODO:
    uint32_t res;
  } StorageBlock;

  /**
   * @class DirNode
   * @brief  目录节点，继承自 FsNode
   *         Directory node, inheriting from FsNode
   */
  class DirNode : public FsNode
  {
   public:
    DirNode() : rbt(RBTree<const char *>(CompareStr)) {}

    RBTree<const char *> rbt;  ///< 目录中的文件树 File tree in the directory
  };

  /**
   * @class Dir
   * @brief  目录类，继承自 RBTree 节点，用于管理文件、子目录和设备
   *         Directory class, inheriting from RBTree node, used for managing files,
   * subdirectories, and devices
   */
  class Dir : public RBTree<const char *>::Node<DirNode>
  {
   public:
    /**
     * @brief  添加文件到当前目录
     *         Adds a file to the current directory
     * @param  file 要添加的文件 The file to be added
     */
    void Add(File &file)
    {
      (*this)->rbt.Insert(file, file->name);
      file->parent = this;
    }
    /**
     * @brief  添加子目录到当前目录
     *         Adds a subdirectory to the current directory
     * @param  dir 要添加的子目录 The subdirectory to be added
     */
    void Add(Dir &dir)
    {
      (*this)->rbt.Insert(dir, dir->name);
      dir->parent = this;
    }
    /**
     * @brief  添加设备到当前目录
     *         Adds a device to the current directory
     * @param  dev 要添加的设备 The device to be added
     */
    void Add(Device &dev)
    {
      (*this)->rbt.Insert(dev, dev->name);
      dev->parent = this;
    }

    /**
     * @brief  查找当前目录中的文件
     *         Finds a file in the current directory
     * @param  name 文件名 The name of the file
     * @return File* 指向文件的指针，如果未找到则返回 nullptr
     *         Pointer to the file, returns nullptr if not found
     */
    File *FindFile(const char *name);

    /**
     * @brief  递归查找文件
     *         Recursively searches for a file
     * @param  name 文件名 The name of the file
     * @return File* 指向文件的指针，如果未找到则返回 nullptr
     *         Pointer to the file, returns nullptr if not found
     */
    File *FindFileRev(const char *name);

    /**
     * @brief  查找当前目录中的子目录
     *         Finds a subdirectory in the current directory
     * @param  name 目录名 The name of the directory
     * @return Dir* 指向目录的指针，如果未找到则返回 nullptr
     *         Pointer to the directory, returns nullptr if not found
     */
    Dir *FindDir(const char *name);

    /**
     * @brief  递归查找子目录
     *         Recursively searches for a subdirectory
     * @param  name 目录名 The name of the directory
     * @return Dir* 指向目录的指针，如果未找到则返回 nullptr
     *         Pointer to the directory, returns nullptr if not found
     */
    Dir *FindDirRev(const char *name);

    /**
     * @brief  递归查找设备
     *         Recursively searches for a device
     * @param  name 设备名 The name of the device
     * @return Device* 指向设备的指针，如果未找到则返回 nullptr
     *         Pointer to the device, returns nullptr if not found
     */
    Device *FindDeviceRev(const char *name);

    /**
     * @brief  在当前目录中查找设备
     *         Finds a device in the current directory
     * @param  name 设备名 The name of the device
     * @return Device* 指向设备的指针，如果未找到则返回 nullptr
     *         Pointer to the device, returns nullptr if not found
     */
    Device *FindDevice(const char *name);
  };

  /**
   * @brief  创建一个新的文件
   *         Creates a new file
   * @tparam DataType 文件存储的数据类型 Data type stored in the file
   * @param  name 文件名 The name of the file
   * @param  raw 文件存储的数据 Data stored in the file
   * @return File 创建的文件对象 The created file object
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  template <typename DataType>
  static File CreateFile(const char *name, DataType &raw)
  {
    File file;
    char *name_buff = new char[strlen(name) + 1];
    strcpy(name_buff, name);
    file->name = name_buff;

    if (std::is_const<DataType>())
    {
      file->type = FileType::READ_ONLY;
      file->addr_const = &raw;
    }
    else
    {
      file->type = FileType::READ_WRITE;
      file->addr = &raw;
    }

    file->size = sizeof(DataType);

    return file;
  }

  /**
   * @brief  创建一个可执行文件
   *         Creates an executable file
   * @tparam ArgType 可执行文件的参数类型 The argument type for the executable file
   * @param  name 文件名 The name of the file
   * @param  exec 可执行函数 The executable function
   * @param  arg 可执行文件的参数 The argument for the executable file
   * @return File 创建的可执行文件对象 The created executable file object
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  template <typename ArgType>
  static File CreateFile(const char *name,
                         int (*exec)(ArgType arg, int argc, char **argv), ArgType &&arg)
  {
    typedef struct
    {
      ArgType arg;
      decltype(exec) exec_fun;
    } FileBlock;

    File file;

    char *name_buff = new char[strlen(name) + 1];
    strcpy(name_buff, name);
    file->name = name_buff;
    file->type = FileType::EXEC;

    auto block = new FileBlock;
    block->arg = std::forward<ArgType>(arg);
    block->exec_fun = exec;
    file->arg = block;

    auto fun = [](void *arg, int argc, char **argv)
    {
      auto block = reinterpret_cast<FileBlock *>(arg);
      return block->exec_fun(block->arg, argc, argv);
    };

    file->exec = fun;

    return file;
  }

  /**
   * @brief  创建一个新的目录
   *         Creates a new directory
   * @param  name 目录名称 The name of the directory
   * @return Dir 创建的目录对象 The created directory object
   *
   * @note 包含动态内存分配。
   *       Contains dynamic memory allocation.
   */
  static Dir CreateDir(const char *name)
  {
    Dir dir;

    char *name_buff = new char[strlen(name) + 1];
    strcpy(name_buff, name);
    dir->name = name_buff;
    dir->type = FsNodeType::DIR;

    return dir;
  }

  /**
   * @brief  向文件系统的根目录添加文件
   *         Adds a file to the root directory of the file system
   * @param  file 要添加的文件 The file to be added
   */
  void Add(File &file) { root_.Add(file); }
  /**
   * @brief  向文件系统的根目录添加子目录
   *         Adds a subdirectory to the root directory of the file system
   * @param  dir 要添加的子目录 The subdirectory to be added
   */
  void Add(Dir &dir) { root_.Add(dir); }
  /**
   * @brief  向文件系统的根目录添加设备
   *         Adds a device to the root directory of the file system
   * @param  dev 要添加的设备 The device to be added
   */
  void Add(Device &dev) { root_.Add(dev); }

  /**
   * @brief  在整个文件系统中查找文件
   *         Finds a file in the entire file system
   * @param  name 文件名 The name of the file
   * @return File* 指向找到的文件的指针，如果未找到则返回 nullptr
   *         Pointer to the found file, or nullptr if not found
   */
  File *FindFile(const char *name) { return root_.FindFileRev(name); }
  /**
   * @brief  在整个文件系统中查找目录
   *         Finds a directory in the entire file system
   * @param  name 目录名 The name of the directory
   * @return Dir* 指向找到的目录的指针，如果未找到则返回 nullptr
   *         Pointer to the found directory, or nullptr if not found
   */
  Dir *FindDir(const char *name) { return root_.FindDirRev(name); }
  /**
   * @brief  在整个文件系统中查找设备
   *         Finds a device in the entire file system
   * @param  name 设备名 The name of the device
   * @return Device* 指向找到的设备的指针，如果未找到则返回 nullptr
   *         Pointer to the found device, or nullptr if not found
   */
  Device *FindDevice(const char *name) { return root_.FindDeviceRev(name); }

  /**
   * @brief  文件系统的根目录
   *         Root directory of the file system
   */
  Dir root_;

  /**
   * @brief  `bin` 目录，用于存放可执行文件
   *         `bin` directory for storing executable files
   */
  Dir bin_;

  /**
   * @brief  `dev` 目录，用于存放设备文件
   *         `dev` directory for storing device files
   */
  Dir dev_;
};
}  // namespace LibXR
