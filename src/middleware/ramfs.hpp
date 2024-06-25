#pragma once

#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "rbt.hpp"
#include <cstring>
#include <type_traits>
#include <utility>

namespace LibXR {
class RamFS {
public:
  static int _str_compare(const char *const &a, const char *const &b) {
    return strcmp(a, b);
  }

  enum class NodeType {
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

  class Node {
  public:
    const char *name;
    NodeType type;
  };

  typedef class _File : public Node {
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

    const char *name;
  } _File;

  typedef RBTree<const char *>::Node<_File> File;

  class _Dir : public Node {
  public:
    RBTree<const char *> rbt = RBTree<const char *>(_str_compare);
  };

  class Dir : public RBTree<const char *>::Node<_Dir> {
  public:
  };

  struct _Device : public Node {
    ReadPort read;
    WritePort write;

    ReadOperation read_op;
    WriteOperation write_op;
  };

  class Device : public RBTree<const char *>::Node<_Device> {
    Device(const char *name, ReadPort read_port = NULL,
           WritePort write_port = NULL) {
      char *name_buff = new char[strlen(name) + 1];
      strcpy(name_buff, name);
      data_.name = name_buff;
      data_.write = write_port;
      data_.read = read_port;
    }

    ErrorCode Read(ReadOperation &op, ConstRawData data) {
      if (data_.read) {
        data_.read_op = op;
        return data_.read(data_.read_op, data);
      } else {
        return ErrorCode::NOT_SUPPORT;
      }
    }

    ErrorCode Write(WriteOperation &op, RawData data) {
      if (data_.write) {
        data_.write_op = op;
        return data_.write(data_.write_op, data);
      } else {
        return ErrorCode::NOT_SUPPORT;
      }
    }

    uint32_t device_type;
  };

  typedef struct {
    // TODO:
    uint32_t res;
  } StorageBlock;

  template <typename FileType>
  File CreateFile(const char *name, FileType &raw) {
    File file;
    char *name_buff = new char[strlen(name) + 1];
    strcpy(name_buff, name);
    file.data_.name = name_buff;

    if (std::is_const<FileType>()) {
      file.data_.type = FileType::READ_ONLY;
      file.data_.addr_const = raw;
    } else {
      file.data_.type = FileType::READ_WRITE;
      file.data_.addr = raw;
    }

    file.size = sizeof(raw);

    return file;
  }

  template <typename ArgType>
  File CreateFile(const char *name,
                  int (*exec)(ArgType &arg, int argc, char **argv),
                  ArgType arg) {
    typedef struct {
      ArgType arg;
      typeof(exec) exec_fun;
    } FileBlock;

    File file;

    char *name_buff = new char[strlen(name) + 1];
    strcpy(name_buff, name);
    file.data_.name = name_buff;

    auto block = new FileBlock;
    block->arg = arg;
    block->exec_fun = exec;
    file.data_.arg = block;

    auto fun = [](void *arg, int argc, char **argv) {
      auto block = reinterpret_cast<FileBlock *>(arg);
      block->exec_fun(block->arg, argc, argv);
    };

    file.data_.exec = fun;

    return file;
  }

  Dir CreateDir(const char *name) {
    Dir dir;

    char *name_buff = new char[strlen(name) + 1];
    strcpy(name_buff, name);
    dir.data_.name = name_buff;

    return dir;
  }

  _Dir root_;
};
} // namespace LibXR