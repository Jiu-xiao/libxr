#pragma once

#include "libxr_rw.hpp"
#include "libxr_type.hpp"
#include "rbt.hpp"
#include <cstring>
#include <type_traits>

namespace LibXR {
class RamFS {
public:
  static int _str_compare(const char *const &a, const char *const &b) {
    return strcmp(a, b);
  }

  typedef enum {
    NODE_TYPE_FILE,
    NODE_TYPE_DIR,
    NODE_TYPE_DEVICE,
    NODE_TYPE_STORAGE,
    NODE_TYPE_ANY,
  } NodeType;

  typedef enum {
    FILE_READ_ONLY,
    FILE_READ_WRITE,
    FILE_EXEC,
  } FileType;

  typedef struct FileBlock {
    union {
      void *addr;
      const void *addr_const;
      int (*exec)(FileBlock *file, int argc, char **argv);
    };
    FileType type;
  } FileBlock;

  typedef struct DirBlock {
    RBTree<const char *> rbt = RBTree<const char *>(_str_compare);
  } DirBlock;

  class DeviceBlock {
  public:
    ReadPort read;
    WritePort write;

    Operation<ErrorCode, LibXR::RawData &> read_op;
    Operation<ErrorCode> write_op;
  };

  typedef struct {
    // TODO:
    uint32_t res;
  } StorageBlock;

  typedef struct {
    const char *name;
    NodeType type;
    void *block;
  } Node;

  typedef FileBlock *File;
  typedef DirBlock *Dir;
  typedef DeviceBlock *Device;
  typedef StorageBlock *Storage;

  template <typename FileType> File CreateFile(FileType raw) {
    File file = new FileBlock;
    if (std::is_const<FileType>()) {
      file->type = FILE_READ_ONLY;
    }
  }

  Node root_;
};
} // namespace LibXR