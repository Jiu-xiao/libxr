#pragma once

#include "libxr_assert.hpp"
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

  static int CompareStr(const char *const &a, const char *const &b) {
    return strcmp(a, b);
  }

  enum class FsNodeType : uint8_t {
    FILE,
    DIR,
    DEVICE,
    STORAGE,
    UNKNOW,
  };

  enum class FileType : uint8_t {
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

  typedef class FileNode : public FsNode {
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

    template <typename DataType, SizeLimitMode LimitMode = SizeLimitMode::MORE>
    const DataType &GetData() {
      LibXR::Assert::SizeLimitCheck<LimitMode>(sizeof(DataType), size);
      if (type == FileType::READ_WRITE) {
        return *reinterpret_cast<DataType *>(addr);
      } else if (type == FileType::READ_ONLY) {
        return *reinterpret_cast<const DataType *>(addr_const);
      } else {
        ASSERT(false);
        const void *addr = nullptr;
        return *reinterpret_cast<const DataType *>(addr);
      }
    }
  } FileNode;

  typedef RBTree<const char *>::Node<FileNode> File;

  struct DeviceNode : public FsNode {
    ReadPort read_port;
    WritePort write_port;
  };

  class Device : public RBTree<const char *>::Node<DeviceNode> {
   public:
    Device(const char *name, const ReadPort &read_port = ReadPort(),
           const WritePort &write_port = WritePort()) {
      char *name_buff = new char[strlen(name) + 1];
      strcpy(name_buff, name);
      data_.name = name_buff;
      data_.type = FsNodeType::DEVICE;

      UNUSED(read_port);
      UNUSED(write_port);
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

  class DirNode : public FsNode {
   public:
    DirNode() : rbt(RBTree<const char *>(CompareStr)) {}

    RBTree<const char *> rbt;
  };

  class Dir : public RBTree<const char *>::Node<DirNode> {
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

    File *FindFileRev(const char *name) {
      auto ans = FindFile(name);

      auto fun = [&]<typename T>(T &self,
                                 RBTree<const char *>::Node<FsNode> &item) {
        FsNode &node = item;
        if (node.type == FsNodeType::DIR) {
          Dir *dir = reinterpret_cast<Dir *>(&item);

          ans = dir->FindFile(name);
          if (ans) {
            return ErrorCode::FAILED;
          }

          dir->data_.rbt.Foreach<FsNode>(
              [&](RBTree<const char *>::Node<FsNode> &item) {
                return self(self, item);
              });

          return ans ? ErrorCode::FAILED : ErrorCode::OK;
        }
        return ErrorCode::OK;
      };

      if (ans == nullptr) {
        data_.rbt.Foreach<FsNode>(
            [&](RBTree<const char *>::Node<FsNode> &item) {
              return fun(fun, item);
            });
      }

      return ans;
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

    Dir *FindDirRev(const char *name) {
      auto ans = FindDir(name);

      auto fun = [&]<typename T>(T &self,
                                 RBTree<const char *>::Node<FsNode> &item) {
        FsNode &node = item;
        if (node.type == FsNodeType::DIR) {
          Dir *dir = reinterpret_cast<Dir *>(&item);
          if (strcmp(dir->data_.name, name) == 0) {
            ans = dir;
            return ErrorCode::OK;
          } else {
            dir->data_.rbt.Foreach<FsNode>(
                [&](RBTree<const char *>::Node<FsNode> &item) {
                  return self(self, item);
                });

            if (ans) {
              return ErrorCode::FAILED;
            } else {
              return ErrorCode::OK;
            }
          }
        } else {
          return ErrorCode::OK;
        }
      };

      if (ans == nullptr) {
        data_.rbt.Foreach<FsNode>(
            [&](RBTree<const char *>::Node<FsNode> &item) {
              return fun(fun, item);
            });
      }

      return ans;
    }

    Device *FindDeviceRev(const char *name) {
      auto ans = FindDevice(name);

      auto fun = [&]<typename T>(T &self,
                                 RBTree<const char *>::Node<FsNode> &item) {
        FsNode &node = item;
        if (node.type == FsNodeType::DIR) {
          Dir *dir = reinterpret_cast<Dir *>(&item);

          ans = dir->FindDevice(name);

          if (ans) {
            return ErrorCode::FAILED;
          }

          dir->data_.rbt.Foreach<FsNode>(
              [&](RBTree<const char *>::Node<FsNode> &item) {
                return self(self, item);
              });

          if (ans) {
            return ErrorCode::FAILED;
          } else {
            return ErrorCode::OK;
          }
        } else {
          return ErrorCode::OK;
        }
      };

      if (ans == nullptr) {
        data_.rbt.Foreach<FsNode>(
            [&](RBTree<const char *>::Node<FsNode> &item) {
              return fun(fun, item);
            });
      }
      return ans;
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

  File *FindFile(const char *name) { return root_.FindFileRev(name); }
  Dir *FindDir(const char *name) { return root_.FindDirRev(name); }
  Device *FindDevice(const char *name) { return root_.FindDeviceRev(name); }

  Dir root_, bin_, dev_;
};
}  // namespace LibXR
