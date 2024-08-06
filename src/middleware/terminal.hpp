#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "list.hpp"
#include "mutex.hpp"
#include "ramfs.hpp"
#include "rbt.hpp"
#include "semaphore.hpp"

namespace LibXR {
class Terminal {
public:
  Terminal(LibXR::RamFS &ramfs) : ramfs_(ramfs) {}

  ReadPort read;
  WritePort write;
  ReadOperation read_op;
  WriteOperation write_op;
  RamFS &ramfs_;
};
} // namespace LibXR
