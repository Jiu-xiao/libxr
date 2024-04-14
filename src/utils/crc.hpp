#include "libxr_assert.hpp"
#include <cstdint>
#include <sys/types.h>

namespace LibXR {
class CRC8 {
private:
  static const uint8_t INIT = 0xFF;

public:
  static uint8_t tab[256];
  static bool inited;
  CRC8() {}

  static void GenerateTable() {
    uint8_t crc = 0;

    for (int i = 0; i < 256; i++) {
      tab[i] = i;
    }

    for (int i = 0; i < 256; i++) {
      for (int j = 7; j >= 0; j--) {
        crc = tab[i] & 0x01;

        if (crc) {
          tab[i] = tab[i] >> 1;
          tab[i] ^= 0x8c;
        } else {
          tab[i] = tab[i] >> 1;
        }
      }
    }
    inited = true;
  }

  static uint8_t Calculate(const void *raw, size_t len) {
    const uint8_t *buf = reinterpret_cast<const uint8_t *>(raw);
    if (!inited) {
      GenerateTable();
    }

    uint8_t crc = INIT;

    while (len-- > 0) {
      crc = tab[(crc ^ *buf++) & 0xff];
    }

    return crc;
  }

  static bool Verify(const void *raw, size_t len) {
    const uint8_t *buf = reinterpret_cast<const uint8_t *>(raw);
    if (!inited) {
      GenerateTable();
    }

    if (len < 2) {
      return false;
    }
    uint8_t expected = Calculate(buf, len - sizeof(uint8_t));
    return expected == buf[len - sizeof(uint8_t)];
  }
};

class CRC16 {
private:
  static const uint16_t INIT = 0xFFFF;

public:
  static uint16_t tab[256];
  static bool inited;
  CRC16() {}

  static void GenerateTable() {
    uint16_t crc = 0;
    for (int i = 0; i < 256; ++i) {
      crc = i;
      for (int j = 0; j < 8; ++j) {
        if (crc & 1) {
          crc = (crc >> 1) ^ 0x8408;
        } else {
          crc >>= 1;
        }
      }
      tab[i] = crc;
    }
    inited = true;
  }

  static uint8_t Calculate(const void *raw, size_t len) {
    const uint8_t *buf = reinterpret_cast<const uint8_t *>(raw);
    if (!inited) {
      GenerateTable();
    }

    uint16_t crc = INIT;
    while (len--) {
      crc = tab[(crc ^ *buf++) & 0xff] ^ (crc >> 8);
    }
    return crc;
  }

  static bool Verify(const void *raw, size_t len) {
    const uint8_t *buf = reinterpret_cast<const uint8_t *>(raw);
    if (!inited) {
      GenerateTable();
    }

    if (len < 2)
      return false;

    uint16_t expected = Calculate(buf, len - sizeof(uint16_t));
    return expected ==
           ((const uint16_t *)((const uint8_t *)buf +
                               (len % 2)))[len / sizeof(uint16_t) - 1];
  }
};

class CRC32 {
private:
  static const uint32_t INIT = 0xFFFFFFFF;

public:
  static uint32_t tab[256];
  static bool inited;

  CRC32() {}

  static void GenerateTable() {
    uint32_t crc = 0;
    for (int i = 0; i < 256; ++i) {
      crc = i;
      for (int j = 0; j < 8; ++j) {
        if (crc & 1) {
          crc = (crc >> 1) ^ 0xEDB88320;
        } else {
          crc >>= 1;
        }
      }
      tab[i] = crc;
    }
    inited = true;
  }

  static uint8_t Calculate(const void *raw, size_t len) {
    const uint8_t *buf = reinterpret_cast<const uint8_t *>(raw);
    if (!inited) {
      GenerateTable();
    }

    uint32_t crc = INIT;
    while (len--) {
      crc = tab[(crc ^ *buf++) & 0xff] ^ (crc >> 8);
    }
    return crc;
  }

  static bool Verify(const void *raw, size_t len) {
    const uint8_t *buf = reinterpret_cast<const uint8_t *>(raw);
    if (!inited) {
      GenerateTable();
    }

    if (len < 2)
      return false;

    uint32_t expected = Calculate(buf, len - sizeof(uint32_t));
    return expected ==
           ((const uint32_t *)((const uint8_t *)buf +
                               (len % 4)))[len / sizeof(uint32_t) - 1];
  }
};
} // namespace LibXR