#include "libxr_type.hpp"

using namespace LibXR;

RawData::RawData(void* addr, size_t size) : addr_(addr), size_(size) {}

RawData::RawData() : addr_(nullptr), size_(0) {}

RawData::RawData(char* data) : addr_(data), size_(data ? strlen(data) : 0) {}

ConstRawData::ConstRawData(const void* addr, size_t size) : addr_(addr), size_(size) {}

ConstRawData::ConstRawData() : addr_(nullptr), size_(0) {}

ConstRawData::ConstRawData(const RawData& data) : addr_(data.addr_), size_(data.size_) {}

ConstRawData::ConstRawData(char* data) : addr_(data), size_(data ? strlen(data) : 0) {}

ConstRawData::ConstRawData(const char* data) : addr_(data), size_(data ? strlen(data) : 0)
{
}
