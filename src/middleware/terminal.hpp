#include "libxr_cb.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "list.hpp"
#include "mutex.hpp"
#include "rbt.hpp"

namespace LibXR {
class Terminal {
public:
  Terminal() {}

  ReadPort read;
  WritePort write;
  ReadOperation read_op;
  WriteOperation write_op;
};
} // namespace LibXR
