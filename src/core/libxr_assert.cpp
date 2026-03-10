#include "libxr_assert.hpp"

using namespace LibXR;

template void LibXR::Assert::SizeLimitCheck<SizeLimitMode::EQUAL>(size_t, size_t);
template void LibXR::Assert::SizeLimitCheck<SizeLimitMode::MORE>(size_t, size_t);
template void LibXR::Assert::SizeLimitCheck<SizeLimitMode::LESS>(size_t, size_t);
template void LibXR::Assert::SizeLimitCheck<SizeLimitMode::NONE>(size_t, size_t);

void Assert::RegisterFatalErrorCallback(const Callback& cb)
{
  libxr_fatal_error_callback_ = cb;
}

void Assert::RegisterFatalErrorCallback(Callback&& cb)
{
  libxr_fatal_error_callback_ = std::move(cb);
}
