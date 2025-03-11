#pragma once

#include "libxr.hpp"
#include "message.hpp"

namespace LibXR {

class CAN {
 public:
  enum class Type : uint8_t {
    STANDARD = 0,
    EXTENDED = 1,
    REMOTE = 2,
  };

  CAN(const char *name_tp = "can", Topic::Domain *domain = nullptr)
      : classic_tp_(name_tp, sizeof(ClassicPack), domain, false, true) {}

  typedef union {
    struct __attribute__((packed)) {
      uint32_t id;
      uint8_t data[8];
    };

    uint8_t raw[12];
  } ClassicPack;

  Topic classic_tp_;

  virtual ErrorCode AddMessage(const ClassicPack &pack, Type type) = 0;
};

class FDCAN : public CAN {
 public:
  FDCAN(const char *name_tp = "can", const char *name_fd_tp = "fdcan",
        Topic::Domain *domain = nullptr)
      : CAN(name_tp, domain),
        fd_tp_(name_fd_tp, sizeof(FDPack), domain, false, false) {}

  typedef union {
    struct __attribute__((packed)) {
      uint32_t id;
      uint8_t len;
      uint8_t data[64];
    };

    uint8_t raw[69];
  } FDPack;

  virtual ErrorCode AddMessage(const FDPack &pack, Type type) = 0;

  Topic fd_tp_;
};

}  // namespace LibXR
