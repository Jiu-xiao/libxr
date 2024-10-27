#pragma once

#include "libxr_assert.hpp"
#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "rbt.hpp"

namespace LibXR {
class RamFS {
public:
  RamFS(const char *name = "ramfs")
      : root_(CreateDir(name)), bin_(CreateDir("bin")), dev_(CreateDir("dev")) {
    root_.Add(bin_);
    root_.Add(dev_);
  }

  static int _str_compare(const char *const &a, const char *const &b) {
    return strcmp(a, b);
  }

  enum class FsNodeType {
    FILE,
    DIR,
    DEVICE,
    STORAGE,
    UNKNOW,
  };

  enum class FileType {
    READ_ONLY,
    READ_WRITE,
    EXEC,
  };

  class Dir;

  class FsNode {
  public:
    const char *name;
    FsNodeType type;
    Dir *parent;
  };

  typedef class _File : public FsNode {
  public:
    union {
      void *addr;
      const void *addr_const;
      int (*exec)(void *raw, int argc, char **argv);
    };

    union {
      size_t size;
      void *arg;
    };

    FileType type;

    int Run(int argc, char **argv) {
      ASSERT(type == FileType::EXEC);
      return exec(arg, argc, argv);
    }

    template <typename DataType>
    const DataType &
    GetData(SizeLimitMode size_limit_mode = SizeLimitMode::MORE) {
      LibXR::Assert::SizeLimitCheck(sizeof(DataType), size, size_limit_mode);
      if (type == FileType::READ_WRITE) {
        return *reinterpret_cast<DataType *>(addr);
      } else if (type == FileType::READ_ONLY) {
        return *reinterpret_cast<const DataType *>(addr_const);
      } else {
        ASSERT(false);
        const void *addr = NULL;
        return *reinterpret_cast<const DataType *>(addr);
      }
    }
  } _File;

  typedef RBTree<const char *>::Node<_File> File;

  struct _Device : public FsNode {
    ReadPort read_port;
    WritePort write_port;
  };

  class Device : public RBTree<const char *>::Node<_Device> {
  public:
    Device(const char *name, ReadPort read_port = ReadPort(),
           WritePort write_port = WritePort()) {
      char *name_buff = new char[strlen(name) + 1];
      strcpy(name_buff, name);
      data_.name = name_buff;
      data_.type = FsNodeType::DEVICE;
    }

    template <typename ReadOperation>
    ErrorCode Read(ReadOperation &&op, RawData data) {
      return data_.read_port(data, std::forward<ReadOperation>(op));
    }

    template <typename WriteOperation>
    ErrorCode Write(WriteOperation &&op, ConstRawData data) {
      return data_.write_port(data, std::forward<WriteOperation>(op));
    }

    uint32_t device_type;
  };

  typedef struct {
    // TODO:
    uint32_t res;
  } StorageBlock;

  class _Dir : public FsNode {
  public:
    _Dir() : rbt(RBTree<const char *>(_str_compare)) {}

    RBTree<const char *> rbt;
  };

  class Dir : public RBTree<const char *>::Node<_Dir> {
  public:
    void Add(File &file) {
      (*this)->rbt.Insert(file, file->name);
      file->parent = this;
    }
    void Add(Dir &dir) {
      (*this)->rbt.Insert(dir, dir->name);
      dir->parent = this;
    }
    void Add(Device &dev) {
      (*this)->rbt.Insert(dev, dev->name);
      dev->parent = this;
    }

    File *FindFile(const char *name) {
      auto ans = (*this)->rbt.Search<FsNode>(name);
      if (ans && ans->data_.type == FsNodeType::FILE) {
        return reinterpret_cast<File *>(ans);
      } else {
        return nullptr;
      }
    }

    typedef struct _FindFileRecBlock {
      File *ans = nullptr;
      const char *name;
    } _FindFileRecBlock;

    static ErrorCode _FindFileRec(RBTree<const char *>::Node<FsNode> &item,
                                  _FindFileRecBlock *block) {
      FsNode &node = item;
      if (node.type == FsNodeType::DIR) {
        Dir *dir = reinterpret_cast<Dir *>(&item);

        block->ans = dir->FindFile(block->name);

        if (block->ans) {
          return ErrorCode::FAILED;
        }

        dir->data_.rbt.Foreach<FsNode>(_FindFileRec, block,
                                       SizeLimitMode::MORE);

        if (block->ans) {
          return ErrorCode::FAILED;
        } else {
          return ErrorCode::OK;
        }
      } else {
        return ErrorCode::OK;
      }
    };

    File *FindFileRec(const char *name) {
      _FindFileRecBlock block;

      block.name = name;

      block.ans = FindFile(name);
      if (block.ans == nullptr) {
        data_.rbt.Foreach<FsNode>(_FindFileRec, &block, SizeLimitMode::MORE);
      }

      return block.ans;
    }

    typedef struct _FindDirRecBlock {
      Dir *ans = nullptr;
      const char *name;
    } _FindDirRecBlock;

    static ErrorCode _FindDirRec(RBTree<const char *>::Node<FsNode> &item,
                                 _FindDirRecBlock *block) {
      FsNode &node = item;
      if (node.type == FsNodeType::DIR) {
        Dir *dir = reinterpret_cast<Dir *>(&item);
        if (strcmp(dir->data_.name, block->name) == 0) {
          block->ans = dir;
          return ErrorCode::OK;
        } else {
          dir->data_.rbt.Foreach<FsNode>(_FindDirRec, block,
                                         SizeLimitMode::MORE);

          if (block->ans) {
            return ErrorCode::FAILED;
          } else {
            return ErrorCode::OK;
          }
        }
      } else {
        return ErrorCode::OK;
      }
    }

    Dir *FindDir(const char *name) {
      if (name[0] == '.' && name[1] == '\0') {
        return this;
      }

      if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        return reinterpret_cast<Dir *>(data_.parent);
      }

      auto ans = (*this)->rbt.Search<RamFS::FsNode>(name);

      if (ans && (*ans)->type == FsNodeType::DIR) {
        return reinterpret_cast<Dir *>(ans);
      } else {
        return nullptr;
      }
    }

    Dir *FindDirRec(const char *name) {
      _FindDirRecBlock block;
      block.name = name;

      block.ans = FindDir(name);
      if (block.ans == nullptr) {
        data_.rbt.Foreach<FsNode>(_FindDirRec, &block, SizeLimitMode::MORE);
      }

      return block.ans;
    }

    typedef struct _FindDevRecBlock {
      Device *ans = nullptr;
      const char *name;
    } _FindDevRecBlock;

    static ErrorCode _FindDevRec(RBTree<const char *>::Node<FsNode> &item,
                                 _FindDevRecBlock *block) {
      FsNode &node = item;
      if (node.type == FsNodeType::DIR) {
        Dir *dir = reinterpret_cast<Dir *>(&item);

        block->ans = dir->FindDevice(block->name);

        if (block->ans) {
          return ErrorCode::FAILED;
        }

        dir->data_.rbt.Foreach<FsNode>(_FindDevRec, block, SizeLimitMode::MORE);

        if (block->ans) {
          return ErrorCode::FAILED;
        } else {
          return ErrorCode::OK;
        }
      } else {
        return ErrorCode::OK;
      }
    }

    Device *FindDeviceRec(const char *name) {
      _FindDevRecBlock block;
      block.name = name;
      block.ans = FindDevice(name);
      if (block.ans == nullptr) {
        data_.rbt.Foreach<FsNode>(_FindDevRec, &block, SizeLimitMode::MORE);
      }
      return block.ans;
    }

    Device *FindDevice(const char *name) {
      auto ans = (*this)->rbt.Search<FsNode>(name);
      if (ans && ans->data_.type == FsNodeType::DEVICE) {
        return reinterpret_cast<Device *>(ans);
      } else {
        return nullptr;
      }
    }
  };

  template <typename DataType>
  static File CreateFile(const char *name, DataType &raw) {
    File file;
    char *name_buff = new char[strlen(name) + 1];
    strcpy(name_buff, name);
    file->name = name_buff;

    if (std::is_const<DataType>()) {
      file->type = FileType::READ_ONLY;
      file->addr_const = &raw;
    } else {
      file->type = FileType::READ_WRITE;
      file->addr = &raw;
    }

    file->size = sizeof(DataType);

    return file;
  }

  template <typename ArgType>
  static File CreateFile(const char *name,
                         int (*exec)(ArgType &arg, int argc, char **argv),
                         ArgType &&arg) {
    typedef struct {
      ArgType arg;
      typeof(exec) exec_fun;
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

    auto fun = [](void *arg, int argc, char **argv) {
      auto block = reinterpret_cast<FileBlock *>(arg);
      return block->exec_fun(block->arg, argc, argv);
    };

    file->exec = fun;

    return file;
  }

  static Dir CreateDir(const char *name) {
    Dir dir;

    char *name_buff = new char[strlen(name) + 1];
    strcpy(name_buff, name);
    dir->name = name_buff;
    dir->type = FsNodeType::DIR;

    return dir;
  }

  void Add(File &file) { root_.Add(file); }
  void Add(Dir &dir) { root_.Add(dir); }
  void Add(Device &dev) { root_.Add(dev); }

  File *FindFile(const char *name) { return root_.FindFileRec(name); }
  Dir *FindDir(const char *name) { return root_.FindDirRec(name); }
  Device *FindDevice(const char *name) { return root_.FindDeviceRec(name); }

  Dir root_, bin_, dev_;
};
} // namespace LibXR
